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

        // One Set per input source, unioned once per emulation frame.
        // The old code polled the gamepad INTO the same Set the keyboard
        // wrote to: with any gamepad connected, every frame's poll deleted
        // the keyboard-held directions (isKeyboardDown() was a stub that
        // always returned false), so movement stuttered or ignored input
        // entirely.
        this.keys = new Set();        // effective union fed to the core
        this.keyboardKeys = new Set();
        this.virtualKeys = new Set(); // on-screen touch/mouse buttons
        this.gamepadKeys = new Set();
        this.audioContext = null;
        this.audioNode = null;
        // This mGBA build's av_info reports a 65,536 Hz stream (verified by
        // measurement).  Keep a bounded typed-array queue and resample it to
        // the browser device rate instead of shift()-ing a JavaScript array
        // for every sample.  (The shipped code assumed 32,768 Hz, which
        // played everything an octave down.)
        this.audioInputRate = 65536;
        this.audioBufferFrames = 8192;
        this.audioRingBuffer = new Int16Array(this.audioBufferFrames * 2);
        this.audioReadFrame = 0;
        this.audioWriteFrame = 0;
        this.audioFramesAvailable = 0;
        this.audioSourcePosition = 0;

        // WebAudio mirror state.  The WASM core cannot deliver continuous
        // DirectSound audio, so the client mirrors the GBA audio engine: the
        // ROM publishes which track/effect is playing in the WebAudioSync
        // block in EWRAM (found by magic scan), and the client plays the
        // pristine source WAVs in lockstep with the game state.
        this.mirror = {
            active: false,     // sync block found AND WAV buffers decoded
            syncFound: false,
            scanAttemptedAt: 0,
            ewramBase: 0,
            ewramSize: 0,
            syncAddr: -1,
            buffers: null,     // {menu, game, laser, explosion, pickup}
            musicSource: null,
            musicGain: null,
            sfxGain: null,
            masterGain: null,
            currentTrack: 0,   // BgmTrack: 0 none, 1 menu, 2 game
            trackBuf: null,
            startedAt: 0,      // ctx.currentTime mapping for position math
            lastSeq: [0, 0, 0, 0],
            lastMusicVol: -1,
            lastSfxVol: -1,
            rateBase: 1
        };
        this.WA_MAGIC = 0x53554153;   // 'SUAS'
        this.WA_VERSION = 1;

        this.fps = 0;
        this.frameCount = 0;
        this.lastFpsTime = performance.now();

        // --- Emulator pacing governor -----------------------------------
        // This WASM core advances only a slice of an emulated frame per
        // retro_run() call (~1/17th for this ROM — a stepping-oriented
        // build), so a single call per rAF plays the game in slow motion.
        // The governor pumps several calls per rAF and servoes the count on
        // the game loop's own iteration rate (the sync block's frame
        // counter), targeting the GBA's 59.73 frames/second.
        this.pumpCalls = 16;          // current calls-per-rAF target
        this.lastSyncFrame = -1;
        this.lastSyncMeasure = 0;
        this.emuRate = 59.73;         // measured emulated iterations/second
        this.lastVideoPtr = 0;        // freshest core framebuffer pointer
        this.lastVideoPitch = 0;

        this.onFpsUpdate = options.onFpsUpdate || (() => {});
        this.onStatusChange = options.onStatusChange || (() => {});

        this.initInput();
    }

    initInput() {
        window.addEventListener("keydown", (e) => {
            const id = this.mapKeyCode(e.code);
            if (id !== null) {
                this.keyboardKeys.add(id);
                e.preventDefault();
            }
        });

        window.addEventListener("keyup", (e) => {
            const id = this.mapKeyCode(e.code);
            if (id !== null) {
                this.keyboardKeys.delete(id);
                e.preventDefault();
            }
        });

        // Losing window/tab focus (alt-tab, clicking the toolbar, opening
        // devtools) can swallow keyup events and leave the ship drifting
        // forever; dropping every held key on blur is strictly safer.
        const releaseAll = () => {
            this.keyboardKeys.clear();
            this.virtualKeys.clear();
            this.gamepadKeys.clear();
            this.keys.clear();
        };
        window.addEventListener("blur", releaseAll);
        document.addEventListener("visibilitychange", () => {
            if (document.hidden) releaseAll();
        });
    }

    /** Rebuild the effective key set presented to the emulator core from the
     *  per-source sets.  Called once per emulation frame from the core's
     *  input-poll callback. */
    refreshKeys() {
        if (this.keyboardKeys.size === 0 && this.virtualKeys.size === 0 && this.gamepadKeys.size === 0) {
            this.keys.clear();
            return;
        }
        const merged = new Set();
        for (const src of [this.keyboardKeys, this.virtualKeys, this.gamepadKeys]) {
            for (const id of src) merged.add(id);
        }
        this.keys = merged;
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
                return 8; // A (Fire / Select)
            case "ShiftLeft":
            case "ShiftRight":
            case "KeyX":
            case "KeyK":
                return 0; // B (Dash / Back)
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
            this.virtualKeys.add(buttonId);
        } else {
            this.virtualKeys.delete(buttonId);
        }
    }

    /* Snapshot the first connected gamepad into the gamepad-only key set.
     * This used to mutate the same Set the keyboard used (deleting keyboard
     * directions whenever no gamepad button was held, because the
     * isKeyboardDown() guard always returned false) — with any controller
     * connected, keyboard movement broke every frame. */
    pollGamepad() {
        const now = performance.now();
        if (now - (this.lastGamepadPoll || 0) < 8) return;
        this.lastGamepadPoll = now;
        const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
        const padKeys = new Set();
        for (const gp of gamepads) {
            if (!gp || !gp.connected) continue;

            // D-Pad / Left Stick
            if (gp.buttons[12]?.pressed || gp.axes[1] < -0.4) padKeys.add(4);
            if (gp.buttons[13]?.pressed || gp.axes[1] > 0.4) padKeys.add(5);
            if (gp.buttons[14]?.pressed || gp.axes[0] < -0.4) padKeys.add(6);
            if (gp.buttons[15]?.pressed || gp.axes[0] > 0.4) padKeys.add(7);

            // Action Buttons
            if (gp.buttons[0]?.pressed || gp.buttons[7]?.pressed) padKeys.add(8);   // A / RT -> Fire
            if (gp.buttons[2]?.pressed || gp.buttons[1]?.pressed || gp.buttons[5]?.pressed) padKeys.add(0); // X/B/RB -> Dash
            if (gp.buttons[4]?.pressed || gp.buttons[6]?.pressed) padKeys.add(10);  // LB / LT
            if (gp.buttons[5]?.pressed || gp.buttons[7]?.pressed) padKeys.add(11);  // RB / RT
            if (gp.buttons[9]?.pressed || gp.buttons[16]?.pressed) padKeys.add(3);  // Start / Menu
            if (gp.buttons[8]?.pressed) padKeys.add(2);                             // Back / Select

            break;
        }
        this.gamepadKeys = padKeys;
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

        // Mirror graph: music and SFX chains into a master gain that follows
        // the page's volume/mute controls.
        const mr = this.mirror;
        mr.masterGain = this.audioContext.createGain();
        mr.masterGain.gain.value = this.muted ? 0 : this.volume;
        mr.masterGain.connect(this.audioContext.destination);
        mr.musicGain = this.audioContext.createGain();
        mr.musicGain.connect(mr.masterGain);
        mr.sfxGain = this.audioContext.createGain();
        mr.sfxGain.connect(mr.masterGain);
        this.loadMirrorBuffers();

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
                    // A short startup/underrun is silence, never stale memory
                    // or a repeated sample that sounds like static.
                    outL[i] = 0;
                    outR[i] = 0;
                }
            }
        };

        this.audioNode.connect(this.audioContext.destination);
    }

    /* Fetch and decode the original audio assets for the WebAudio mirror. */
    async loadMirrorBuffers() {
        const names = ["menu", "game", "laser", "explosion", "pickup"];
        const out = {};
        try {
            await Promise.all(names.map(async (name) => {
                const resp = await fetch("/dist/audio/" + name + ".wav");
                if (!resp.ok) throw new Error("audio fetch failed: " + name);
                const bytes = await resp.arrayBuffer();
                out[name] = await this.audioContext.decodeAudioData(bytes);
            }));
            this.mirror.buffers = out;
            this.updateMirrorActive();
        } catch (err) {
            console.warn("WebAudio mirror assets unavailable, using emulator audio:", err);
        }
    }

    updateMirrorActive() {
        const mr = this.mirror;
        mr.active = mr.syncFound && mr.buffers !== null;
    }

    /* Find the WebAudioSync block the ROM publishes in EWRAM (magic scan).
     * Retried periodically until found; older ROMs simply keep emulator
     * audio. */
    locateAudioSync(now) {
        const mr = this.mirror;
        if (mr.syncFound || !this.m) return;
        if (now - mr.scanAttemptedAt < 500) return;
        mr.scanAttemptedAt = now;

        if (!mr.ewramBase) {
            mr.ewramBase = this.m._retro_get_memory_data(2); // SYSTEM_RAM
            mr.ewramSize = this.m._retro_get_memory_size(2);
            if (!mr.ewramBase || !mr.ewramSize) return;
        }
        // Scan EWRAM plus 64 KB beyond it (covers cores that place the EWRAM
        // window's tail next to IWRAM in their arena).
        const scanBytes = mr.ewramSize + 65536;
        const heap = this.m.HEAPU8.buffer;
        const view = new Uint32Array(heap, mr.ewramBase, scanBytes - (scanBytes % 4));
        for (let i = 0; i < view.length - 3; i++) {
            if (view[i] === this.WA_MAGIC && view[i + 1] === this.WA_VERSION) {
                mr.syncAddr = mr.ewramBase + i * 4;
                mr.syncFound = true;
                this.updateMirrorActive();
                return;
            }
        }
    }

    stopMirrorMusic() {
        const mr = this.mirror;
        if (mr.musicSource) {
            try { mr.musicSource.stop(); } catch (e) { /* already stopped */ }
            mr.musicSource = null;
        }
        mr.currentTrack = 0;
        mr.trackBuf = null;
    }

    restartMirrorMusic(trackId, positionRatio) {
        const mr = this.mirror;
        this.stopMirrorMusic();
        const buf = trackId === 1 ? mr.buffers.menu : trackId === 2 ? mr.buffers.game : null;
        if (!buf) return;
        const ctx = this.audioContext;
        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.loop = true;
        src.connect(mr.musicGain);
        src.playbackRate.value = mr.rateBase || 1;
        const offset = (positionRatio * buf.duration) % buf.duration;
        mr.startedAt = ctx.currentTime - offset;
        src.start(0, offset);
        mr.musicSource = src;
        mr.currentTrack = trackId;
        mr.trackBuf = buf;
    }

    /* Poll the ROM's WebAudioSync block and keep the WebAudio playback
     * aligned with the emulated engine state.  Called once per rAF. */
    syncMirrorAudio(now) {
        const mr = this.mirror;
        if (!this.m || !this.audioContext) return;
        if (!mr.syncFound) {
            this.locateAudioSync(now);
            return;
        }
        if (!mr.buffers) return; // core audio remains until assets decode

        const dv = new DataView(this.m.HEAPU8.buffer, mr.syncAddr, 44);
        if (dv.getUint32(0, true) !== this.WA_MAGIC) {
            mr.syncFound = false; // struct vanished (core reset); rescan
            mr.active = false;
            this.stopMirrorMusic();
            return;
        }

        const bgmId = dv.getInt8(12);
        const bgmPlaying = dv.getInt8(13) !== 0;
        const musicVol = dv.getUint8(14);
        const sfxVol = dv.getUint8(15);
        const bgmPos = dv.getUint32(16, true);
        const bgmLen = dv.getUint32(20, true);

        // Music follows the ROM's track selection and plays the pristine
        // source WAV, looping seamlessly on its own.  (The GBA engine's
        // position counter is not a usable wall clock in this core, and
        // nothing in gameplay depends on phase alignment, so the WAV loops
        // independently while track START/STOP/SWITCH stay exact.)
        const wantTrack = bgmPlaying && (bgmId === 1 || bgmId === 2) && bgmLen > 0 ? bgmId : 0;
        if (wantTrack !== mr.currentTrack) {
            this.restartMirrorMusic(wantTrack, wantTrack ? bgmPos / bgmLen : 0);
        }

        // Volumes follow the in-game settings sliders.
        if (musicVol !== mr.lastMusicVol) {
            mr.lastMusicVol = musicVol;
            mr.musicGain.gain.setTargetAtTime((musicVol / 100) * 0.9, this.audioContext.currentTime, 0.03);
        }
        if (sfxVol !== mr.lastSfxVol) {
            mr.lastSfxVol = sfxVol;
            mr.sfxGain.gain.setTargetAtTime((sfxVol / 100) * 0.9, this.audioContext.currentTime, 0.03);
        }

        // SFX triggers: each channel's sequence number bumps per play.
        const fx = [mr.buffers.laser, mr.buffers.explosion, mr.buffers.pickup];
        for (let i = 0; i < 4; i++) {
            const seq = dv.getUint32(24 + i * 4, true);
            if (seq !== mr.lastSeq[i]) {
                mr.lastSeq[i] = seq;
                const id = dv.getInt8(40 + i);
                if (id >= 0 && id < fx.length && fx[id]) {
                    const src = this.audioContext.createBufferSource();
                    src.buffer = fx[id];
                    src.connect(mr.sfxGain);
                    src.start();
                }
            }
        }
    }

    async loadRom(romBuffer) {
        this.onStatusChange("Initializing mGBA emulator engine...");

        this.m = await create_mgba({
            locateFile: (file) => "/dist/" + file
        });

        const m = this.m;

        // Setup Environment Callback
        this.pixelFormat = 0; // 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565
        const envCb = m.addFunction((cmd, data) => {
            if (cmd === 3) { // RETRO_ENVIRONMENT_GET_CAN_DUPE
                m.setValue(data, 1, "i32");
                return 1;
            }
            if (cmd === 10) { // RETRO_ENVIRONMENT_SET_PIXEL_FORMAT
                this.pixelFormat = m.getValue(data, "i32");
                return 1;
            }
            return 0;
        }, "iii");
        m._retro_set_environment(envCb);

        m._retro_init();

        // Setup Video Callback.  With the pacing governor the core presents
        // many times per displayed frame, so the callback only remembers the
        // newest framebuffer pointer; the actual pixel conversion runs once
        // per rAF in presentFrame().
        const videoCb = m.addFunction((dataPtr, width, height, pitch) => {
            if (!dataPtr || width === 0 || height === 0) return;
            this.lastVideoPtr = dataPtr;
            this.lastVideoPitch = pitch;
        }, "viiii");
        m._retro_set_video_refresh(videoCb);

        // Setup Audio Callback.  This core emits batches at 65,536 Hz with
        // long zero-filler gaps between the real content (it advances the
        // emulated machine only in per-call slices), so reconstruct the
        // stream by deleting any run of exact silence longer than 48 stereo
        // frames — dithered PCM never produces one legitimately.  When the
        // WebAudio mirror is active the stream is dropped entirely.
        const audioCb = m.addFunction((dataPtr, frames) => {
            if (this.mirror.active) return frames;
            const src16 = new Int16Array(m.HEAPU8.buffer, dataPtr, frames * 2);

            // Find filler-free segments.
            let segStart = 0;
            let zeroStart = -1;
            const pushSegment = (from, to) => {
                for (let frame = from; frame < to; frame++) {
                    // Keep latency bounded by dropping the oldest complete
                    // stereo frame only when the queue is full.  Never use
                    // Array.shift() here: it reallocates and copies the
                    // queue at audio rate.
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
            };
            for (let f = 0; f < frames; f++) {
                const silent = src16[f * 2] === 0 && src16[f * 2 + 1] === 0;
                if (silent) {
                    if (zeroStart < 0) zeroStart = f;
                } else {
                    if (zeroStart >= 0) {
                        const runLen = f - zeroStart;
                        if (runLen > 48) {
                            // flush content before the filler gap, skip the gap
                            if (zeroStart > segStart) pushSegment(segStart, zeroStart);
                            segStart = f;
                        }
                        zeroStart = -1;
                    }
                }
            }
            // Trailing filler that reaches the end of the batch also counts.
            if (zeroStart >= 0 && frames - zeroStart > 48) {
                if (zeroStart > segStart) pushSegment(segStart, zeroStart);
                segStart = frames;
            }
            if (segStart < frames) pushSegment(segStart, frames);
            return frames;
        }, "iii");
        m._retro_set_audio_sample_batch(audioCb);

        // Setup Input Callback
        const inputPollCb = m.addFunction(() => {
            this.pollGamepad();
            this.refreshKeys();
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

        this.running = true;
        this.paused = false;
        this.onStatusChange("Game Boy Advance ready!");
        this.startLoop();
    }

    /* Convert the newest core framebuffer to the canvas — once per displayed
     * frame, no matter how many times the core presented during the pump. */
    presentFrame() {
        if (!this.lastVideoPtr) return;
        const m = this.m;
        const pitchWords = this.lastVideoPitch >> 1;
        const src16 = new Uint16Array(m.HEAPU8.buffer, this.lastVideoPtr, pitchWords * 160);
        const dst = this.imgBuf;
        const rgb565 = this.pixelFormat === 2;

        for (let y = 0; y < 160; y++) {
            const srcRow = y * pitchWords;
            const dstRow = y * 240;
            for (let x = 0; x < 240; x++) {
                const color = src16[srcRow + x];
                let r, g, b;
                if (rgb565) { // RGB565
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
    }

    startLoop() {
        let lastTime = performance.now();
        const frameInterval = 1000 / 60; // 60 FPS

        const loop = (now) => {
            if (!this.running) return;

            const delta = now - lastTime;
            if (!this.paused && delta >= frameInterval * 0.9) {
                lastTime = now - (delta % frameInterval);

                // Pump the core: this build advances ~1/17th of an emulated
                // frame per call, so pump several calls per rAF within a
                // bounded slice of the frame budget.  The governor then
                // servoes the count on the game's own loop rate.
                const budgetMs = 26;
                const t0 = performance.now();
                let calls = 0;
                do {
                    this.m._retro_run();
                    calls++;
                } while (calls < this.pumpCalls && performance.now() - t0 < budgetMs);

                this.presentFrame();

                // Governor: every 500 ms measure the emulated loop rate from
                // the audio sync block's frame counter and adjust the pump.
                if (now - this.lastSyncMeasure >= 500) {
                    const syncFrame = this.readSyncFrame();
                    if (syncFrame >= 0 && this.lastSyncFrame >= 0) {
                        const elapsed = (now - this.lastSyncMeasure) / 1000;
                        const rate = (syncFrame - this.lastSyncFrame) / elapsed;
                        this.emuRate = rate;
                        // Keep mirrored music glued to the emulated timeline
                        // even when the host machine can't reach full speed.
                        this.mirror.rateBase = Math.max(0.35, Math.min(1.05, rate / 59.73));
                        if (rate > 1) {
                            const adjust = 59.73 / rate;
                            const next = Math.round(this.pumpCalls * (0.5 + 0.5 * adjust));
                            this.pumpCalls = Math.max(1, Math.min(32, next));
                        }
                    }
                    this.lastSyncFrame = syncFrame;
                    this.lastSyncMeasure = now;
                }
            }

            // Drive the WebAudio audio mirror against the ROM's sync block.
            this.syncMirrorAudio(now);

            // FPS Meter: report the EMULATED game rate (what the player
            // experiences), not the canvas refresh count.
            if (now - this.lastFpsTime >= 1000) {
                const shown = (this.mirror.syncFound && this.emuRate > 1)
                    ? Math.round(this.emuRate)
                    : this.frameCount;
                this.fps = shown;
                this.frameCount = 0;
                this.lastFpsTime = now;
                this.onFpsUpdate(shown);
            }

            requestAnimationFrame(loop);
        };

        requestAnimationFrame(loop);
    }

    /* Current value of the ROM's g_web_audio_sync.frame, or -1 if unknown. */
    readSyncFrame() {
        const mr = this.mirror;
        if (!this.m || !mr.syncFound || mr.syncAddr < 0) {
            // kick the scan so the governor can start working
            if (!mr.syncFound) this.locateAudioSync(performance.now());
            return -1;
        }
        const dv = new DataView(this.m.HEAPU8.buffer, mr.syncAddr, 44);
        if (dv.getUint32(0, true) !== this.WA_MAGIC) return -1;
        return dv.getUint32(8, true);
    }

    setPaused(paused) {
        this.paused = paused;
        if (this.audioContext) {
            if (paused) this.audioContext.suspend();
            else this.audioContext.resume();
        }
    }

    reset() {
        if (this.m && this.running) {
            this.m._retro_reset();
            // The ROM re-initializes its audio engine; re-sync the mirror.
            this.mirror.lastSeq = [0, 0, 0, 0];
            this.mirror.lastMusicVol = -1;
            this.mirror.lastSfxVol = -1;
            this.stopMirrorMusic();
            this.emuRate = 59.73;
            this.mirror.rateBase = 1;
        }
    }

    setVolume(vol) {
        this.volume = Math.max(0, Math.min(1, vol));
        if (this.mirror.masterGain && this.audioContext) {
            this.mirror.masterGain.gain.setTargetAtTime(
                this.muted ? 0 : this.volume, this.audioContext.currentTime, 0.02);
        }
    }

    setMuted(muted) {
        this.muted = muted;
        if (this.mirror.masterGain && this.audioContext) {
            this.mirror.masterGain.gain.setTargetAtTime(
                muted ? 0 : this.volume, this.audioContext.currentTime, 0.02);
        }
    }
}

window.GbaPlayer = GbaPlayer;
export { GbaPlayer };
