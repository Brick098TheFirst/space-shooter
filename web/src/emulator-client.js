import create_mgba from "../../node_modules/romdev-platform-gba/wasm/mgba_libretro.js";

class GbaPlayer {
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.ctx = canvas.getContext("2d", { alpha: false });
        this.imgData = this.ctx.createImageData(240, 160);
        this.imgBuf = new Uint32Array(this.imgData.data.buffer);

        this.running = false;
        this.paused = false;
        this.volume = 0.8;
        this.muted = false;

        this.keys = new Set();
        this.audioContext = null;
        this.audioNode = null;
        // mGBA outputs the GBA's native 32,768 Hz mixed stream.  Keep a
        // bounded typed-array queue and resample it to the browser device
        // rate instead of shift()-ing a JavaScript array for every sample.
        this.audioInputRate = 32768;
        this.audioBufferFrames = 8192;
        this.audioRingBuffer = new Int16Array(this.audioBufferFrames * 2);
        this.audioReadFrame = 0;
        this.audioWriteFrame = 0;
        this.audioFramesAvailable = 0;
        this.audioSourcePosition = 0;

        this.fps = 0;
        this.frameCount = 0;
        this.lastFpsTime = performance.now();
        this.sramSaveCounter = 0;
        this.lastSavedSramB64 = null;
        this.sramPtr = 0;
        this.sramSize = 0;

        this.onFpsUpdate = options.onFpsUpdate || (() => {});
        this.onStatusChange = options.onStatusChange || (() => {});

        this.initInput();
        this.initSaveListeners();
    }

    initSaveListeners() {
        window.addEventListener("beforeunload", () => this.saveSram());
        window.addEventListener("pagehide", () => this.saveSram());
        document.addEventListener("visibilitychange", () => {
            if (document.visibilityState === "hidden") this.saveSram();
        });
    }

    initInput() {
        window.addEventListener("keydown", (e) => {
            const id = this.mapKeyCode(e.code);
            if (id !== null) {
                this.keys.add(id);
                e.preventDefault();
            }
        });

        window.addEventListener("keyup", (e) => {
            const id = this.mapKeyCode(e.code);
            if (id !== null) {
                this.keys.delete(id);
                e.preventDefault();
            }
        });
    }

    mapKeyCode(code) {
        switch (code) {
            case "ArrowUp":
            case "KeyW":
                return 4; // UP
            case "ArrowDown":
            case "KeyS":
                return 5; // DOWN
            case "ArrowLeft":
            case "KeyA":
                return 6; // LEFT
            case "ArrowRight":
            case "KeyD":
                return 7; // RIGHT
            case "Space":
            case "KeyZ":
            case "KeyJ":
                return 8; // A (Fire / Select / Buy)
            case "ShiftLeft":
            case "ShiftRight":
            case "KeyX":
            case "KeyK":
                return 0; // B (Beam / Back)
            case "KeyQ":
            case "KeyU":
                return 10; // L
            case "KeyE":
            case "KeyI":
                return 11; // R
            case "Enter":
            case "KeyP":
                return 3; // START (Pause / Menu)
            case "Backspace":
            case "Tab":
                return 2; // SELECT
            default:
                return null;
        }
    }

    setVirtualButton(buttonId, isPressed) {
        if (isPressed) {
            this.keys.add(buttonId);
        } else {
            this.keys.delete(buttonId);
        }
    }

    pollGamepad() {
        const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
        for (const gp of gamepads) {
            if (!gp || !gp.connected) continue;

            // D-Pad / Left Stick
            const dpadUp = gp.buttons[12]?.pressed || gp.axes[1] < -0.4;
            const dpadDown = gp.buttons[13]?.pressed || gp.axes[1] > 0.4;
            const dpadLeft = gp.buttons[14]?.pressed || gp.axes[0] < -0.4;
            const dpadRight = gp.buttons[15]?.pressed || gp.axes[0] > 0.4;

            // Action Buttons
            const btnA = gp.buttons[0]?.pressed || gp.buttons[7]?.pressed; // A or RT (Fire / Buy)
            const btnB = gp.buttons[2]?.pressed || gp.buttons[1]?.pressed || gp.buttons[5]?.pressed; // X, B or RB (Beam)
            const btnL = gp.buttons[4]?.pressed || gp.buttons[6]?.pressed; // LB or LT (Prev Tab)
            const btnR = gp.buttons[5]?.pressed || gp.buttons[7]?.pressed; // RB or RT (Next Tab)
            const btnStart = gp.buttons[9]?.pressed || gp.buttons[16]?.pressed; // Start / Menu
            const btnSelect = gp.buttons[8]?.pressed; // Back / Select

            if (dpadUp) this.keys.add(4); else if (!this.isKeyboardDown(4)) this.keys.delete(4);
            if (dpadDown) this.keys.add(5); else if (!this.isKeyboardDown(5)) this.keys.delete(5);
            if (dpadLeft) this.keys.add(6); else if (!this.isKeyboardDown(6)) this.keys.delete(6);
            if (dpadRight) this.keys.add(7); else if (!this.isKeyboardDown(7)) this.keys.delete(7);

            if (btnA) this.keys.add(8); else if (!this.isKeyboardDown(8)) this.keys.delete(8);
            if (btnB) this.keys.add(0); else if (!this.isKeyboardDown(0)) this.keys.delete(0);
            if (btnL) this.keys.add(10); else if (!this.isKeyboardDown(10)) this.keys.delete(10);
            if (btnR) this.keys.add(11); else if (!this.isKeyboardDown(11)) this.keys.delete(11);
            if (btnStart) this.keys.add(3); else if (!this.isKeyboardDown(3)) this.keys.delete(3);
            if (btnSelect) this.keys.add(2); else if (!this.isKeyboardDown(2)) this.keys.delete(2);

            break;
        }
    }

    isKeyboardDown(id) {
        return false;
    }

    initAudio() {
        if (this.audioContext) return;
        const AudioContext = window.AudioContext || window.webkitAudioContext;
        if (!AudioContext) return;

        this.audioContext = new AudioContext({ sampleRate: 44100 });
        const bufferSize = 2048;
        const outputRate = this.audioContext.sampleRate;
        const resampleStep = this.audioInputRate / outputRate;
        this.audioNode = this.audioContext.createScriptProcessor(bufferSize, 0, 2);

        this.audioNode.onaudioprocess = (e) => {
            const outL = e.outputBuffer.getChannelData(0);
            const outR = e.outputBuffer.getChannelData(1);
            const effectiveVol = this.muted ? 0 : this.volume;

            for (let i = 0; i < outL.length; i++) {
                if (this.audioFramesAvailable >= 2) {
                    const frame0 = this.audioReadFrame;
                    const frame1 = (frame0 + 1) % this.audioBufferFrames;
                    const base0 = frame0 * 2;
                    const base1 = frame1 * 2;
                    const fraction = this.audioSourcePosition;
                    const left0 = this.audioRingBuffer[base0] / 32768.0;
                    const right0 = this.audioRingBuffer[base0 + 1] / 32768.0;
                    const left1 = this.audioRingBuffer[base1] / 32768.0;
                    const right1 = this.audioRingBuffer[base1 + 1] / 32768.0;

                    outL[i] = (left0 + (left1 - left0) * fraction) * effectiveVol;
                    outR[i] = (right0 + (right1 - right0) * fraction) * effectiveVol;

                    this.audioSourcePosition += resampleStep;
                    const consumed = Math.floor(this.audioSourcePosition);
                    if (consumed > 0) {
                        this.audioReadFrame = (this.audioReadFrame + consumed) % this.audioBufferFrames;
                        this.audioFramesAvailable = Math.max(0, this.audioFramesAvailable - consumed);
                        this.audioSourcePosition -= consumed;
                    }
                } else {
                    outL[i] = 0;
                    outR[i] = 0;
                }
            }
        };

        this.audioNode.connect(this.audioContext.destination);
    }

    saveSram() {
        if (!this.m || !this.sramPtr || !this.sramSize) return;
        const saveLen = Math.min(this.sramSize, 512);
        const sramBytes = this.m.HEAPU8.subarray(this.sramPtr, this.sramPtr + saveLen);
        
        let hasData = false;
        for (let i = 0; i < 48; i++) {
            if (sramBytes[i] !== 0) { hasData = true; break; }
        }
        if (!hasData) return;

        let binary = "";
        for (let i = 0; i < sramBytes.length; i++) {
            binary += String.fromCharCode(sramBytes[i]);
        }
        const b64 = btoa(binary);
        if (b64 !== this.lastSavedSramB64) {
            this.lastSavedSramB64 = b64;
            try {
                localStorage.setItem("space_shooter_gba_sram_v3", b64);
            } catch (e) {
                console.warn("Could not save to localStorage:", e);
            }
        }
    }

    async loadRom(romBuffer) {
        this.onStatusChange("Initializing mGBA emulator engine...");

        this.m = await create_mgba({
            locateFile: (file) => new URL(file, import.meta.url).href
        });

        const m = this.m;

        // Setup Environment Callback
        let pixelFormat = 0; // 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565
        const envCb = m.addFunction((cmd, data) => {
            if (cmd === 3) { // RETRO_ENVIRONMENT_GET_CAN_DUPE
                m.setValue(data, 1, "i32");
                return 1;
            }
            if (cmd === 10) { // RETRO_ENVIRONMENT_SET_PIXEL_FORMAT
                pixelFormat = m.getValue(data, "i32");
                return 1;
            }
            return 0;
        }, "iii");
        m._retro_set_environment(envCb);

        m._retro_init();

        // Setup Video Callback
        const videoCb = m.addFunction((dataPtr, width, height, pitch) => {
            if (!dataPtr || width === 0 || height === 0) return;
            const pitchWords = pitch >> 1;
            const src16 = new Uint16Array(m.HEAPU8.buffer, dataPtr, pitchWords * height);
            const dst = this.imgBuf;

            for (let y = 0; y < 160; y++) {
                const srcRow = y * pitchWords;
                const dstRow = y * 240;
                for (let x = 0; x < 240; x++) {
                    const color = src16[srcRow + x];
                    let r, g, b;
                    if (pixelFormat === 2) { // RGB565
                        r = ((color >> 11) & 0x1F) << 3;
                        g = ((color >> 5) & 0x3F) << 2;
                        b = (color & 0x1F) << 3;
                    } else { // 0RGB1555
                        r = ((color >> 10) & 0x1F) << 3;
                        g = ((color >> 5) & 0x1F) << 3;
                        b = (color & 0x1F) << 3;
                    }
                    dst[dstRow + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
                }
            }

            this.ctx.putImageData(this.imgData, 0, 0);
            this.frameCount++;
        }, "viiii");
        m._retro_set_video_refresh(videoCb);

        // Setup Audio Callback
        const audioCb = m.addFunction((dataPtr, frames) => {
            const src16 = new Int16Array(m.HEAPU8.buffer, dataPtr, frames * 2);
            for (let frame = 0; frame < frames; frame++) {
                if (this.audioFramesAvailable >= this.audioBufferFrames) {
                    this.audioReadFrame = (this.audioReadFrame + 1) % this.audioBufferFrames;
                    this.audioFramesAvailable--;
                    this.audioSourcePosition = 0;
                }
                const base = this.audioWriteFrame * 2;
                this.audioRingBuffer[base] = src16[frame * 2];
                this.audioRingBuffer[base + 1] = src16[frame * 2 + 1];
                this.audioWriteFrame = (this.audioWriteFrame + 1) % this.audioBufferFrames;
                this.audioFramesAvailable++;
            }
            return frames;
        }, "iii");
        m._retro_set_audio_sample_batch(audioCb);

        // Setup Input Callback
        const inputPollCb = m.addFunction(() => {
            this.pollGamepad();
        }, "v");
        m._retro_set_input_poll(inputPollCb);

        const inputStateCb = m.addFunction((port, device, index, id) => {
            return this.keys.has(id) ? 1 : 0;
        }, "iiiii");
        m._retro_set_input_state(inputStateCb);

        // Load ROM into emulator memory
        const romPtr = m._malloc(romBuffer.byteLength);
        m.HEAPU8.set(new Uint8Array(romBuffer), romPtr);

        const infoPtr = m._malloc(16);
        m.setValue(infoPtr + 0, 0, "i32");
        m.setValue(infoPtr + 4, romPtr, "i32");
        m.setValue(infoPtr + 8, romBuffer.byteLength, "i32");
        m.setValue(infoPtr + 12, 0, "i32");

        const ok = m._retro_load_game(infoPtr);
        if (!ok) {
            throw new Error("Failed to load Game Boy Advance ROM into mGBA core.");
        }

        // Restore SRAM save data from localStorage if present
        this.sramPtr = m._retro_get_memory_data(0);
        this.sramSize = m._retro_get_memory_size(0);
        if (this.sramPtr && this.sramSize > 0) {
            const savedSram = localStorage.getItem("space_shooter_gba_sram_v3") ||
                              localStorage.getItem("space_shooter_gba_sram_v2") ||
                              localStorage.getItem("space_shooter_gba_sram");
            if (savedSram) {
                try {
                    const raw = atob(savedSram);
                    const u8 = new Uint8Array(raw.length);
                    for (let i = 0; i < raw.length; i++) u8[i] = raw.charCodeAt(i);
                    const copyLen = Math.min(this.sramSize, u8.length);
                    m.HEAPU8.set(u8.subarray(0, copyLen), this.sramPtr);
                    console.log(`[SRAM] Restored ${copyLen} bytes of save data from localStorage.`);
                } catch (e) {
                    console.warn("Failed to restore saved SRAM:", e);
                }
            }
        }

        this.running = true;
        this.paused = false;
        this.onStatusChange("Game Boy Advance ready!");
        this.startLoop();
    }

    startLoop() {
        let lastTime = performance.now();
        const frameInterval = 1000 / 60; // 60 FPS

        const loop = (now) => {
            if (!this.running) return;

            const delta = now - lastTime;
            if (!this.paused && delta >= frameInterval * 0.9) {
                lastTime = now - (delta % frameInterval);
                this.m._retro_run();
            }

            // Periodically sync SRAM to localStorage (~once per second)
            this.sramSaveCounter++;
            if (this.sramSaveCounter >= 60) {
                this.sramSaveCounter = 0;
                this.saveSram();
            }

            // FPS Meter
            if (now - this.lastFpsTime >= 1000) {
                this.fps = this.frameCount;
                this.frameCount = 0;
                this.lastFpsTime = now;
                this.onFpsUpdate(this.fps);
            }

            requestAnimationFrame(loop);
        };

        requestAnimationFrame(loop);
    }

    setPaused(paused) {
        this.paused = paused;
        if (paused) this.saveSram();
        if (this.audioContext) {
            if (paused) this.audioContext.suspend();
            else this.audioContext.resume();
        }
    }

    reset() {
        if (this.m && this.running) {
            this.saveSram();
            this.m._retro_reset();
        }
    }

    setVolume(vol) {
        this.volume = Math.max(0, Math.min(1, vol));
    }

    setMuted(muted) {
        this.muted = muted;
    }
}

window.GbaPlayer = GbaPlayer;
export { GbaPlayer };
