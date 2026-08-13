/*
 * Headless GBA smoke test.
 *
 * Boots SpaceUnlimited.gba in the mGBA libretro core, drives the menus with
 * synthetic input, and asserts the ROM keeps producing video frames. Used to
 * verify boss waves / mode select don't hang or crash the ROM.
 *
 * Usage: node tools/smoke_gba.mjs [--frames N] [--script name]
 */
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const corePath = require.resolve("romdev-platform-gba/wasm/mgba_libretro.js");
const create_mgba = (await import(corePath)).default;

// libretro RETRO_DEVICE_ID_JOYPAD ids
const ID = { B: 0, SELECT: 2, START: 3, UP: 4, DOWN: 5, LEFT: 6, RIGHT: 7, A: 8, L: 10, R: 11 };

const args = process.argv.slice(2);
const getArg = (name, dflt) => {
    const i = args.indexOf(name);
    return i >= 0 && args[i + 1] ? args[i + 1] : dflt;
};
const TOTAL_FRAMES = parseInt(getArg("--frames", "5400"), 10);

const romPath = path.resolve("SpaceUnlimited.gba");
const rom = fs.readFileSync(romPath);

const m = await create_mgba({});

let pixelFormat = 0;
const envCb = m.addFunction((cmd, data) => {
    if (cmd === 3) { m.setValue(data, 1, "i32"); return 1; }
    if (cmd === 10) { pixelFormat = m.getValue(data, "i32"); return 1; }
    return 0;
}, "iii");
m._retro_set_environment(envCb);
m._retro_init();

let frames = 0;
let lastFrameHash = 0;
const frameHashes = [];
const videoCb = m.addFunction((dataPtr, width, height, pitch) => {
    if (!dataPtr || !width || !height) return;
    const pitchWords = pitch >> 1;
    const src16 = new Uint16Array(m.HEAPU8.buffer, dataPtr, pitchWords * height);
    // Cheap FNV-ish hash of the visible frame so we can detect a frozen screen.
    let h = 2166136261;
    for (let y = 0; y < 160; y += 4) {
        const row = y * pitchWords;
        for (let x = 0; x < 240; x += 4) {
            h ^= src16[row + x];
            h = Math.imul(h, 16777619) >>> 0;
        }
    }
    lastFrameHash = h;
    frames++;
}, "viiii");
m._retro_set_video_refresh(videoCb);

const audioCb = m.addFunction((ptr, n) => n, "iii");
m._retro_set_audio_sample_batch(audioCb);

const held = new Set();
m._retro_set_input_poll(m.addFunction(() => {}, "v"));
m._retro_set_input_state(m.addFunction((port, device, index, id) => (held.has(id) ? 1 : 0), "iiiii"));

const romPtr = m._malloc(rom.byteLength);
m.HEAPU8.set(rom, romPtr);
const infoPtr = m._malloc(16);
m.setValue(infoPtr + 0, 0, "i32");
m.setValue(infoPtr + 4, romPtr, "i32");
m.setValue(infoPtr + 8, rom.byteLength, "i32");
m.setValue(infoPtr + 12, 0, "i32");

if (!m._retro_load_game(infoPtr)) {
    console.error("FAIL: mGBA refused to load the ROM");
    process.exit(1);
}
console.log(`Loaded ROM: ${(rom.byteLength / 1024).toFixed(1)} KB`);

const run = (n) => { for (let i = 0; i < n; i++) m._retro_run(); };
const tap = (id, downFrames = 4, gap = 8) => {
    held.add(id); run(downFrames);
    held.delete(id); run(gap);
};

// ── Drive the UI ────────────────────────────────────────────────────────
run(180);                       // boot into the main menu
const bootFrames = frames;
console.log(`Boot: ${bootFrames} frames rendered`);
if (bootFrames === 0) { console.error("FAIL: no video output after boot"); process.exit(1); }

const seen = new Set();
const sample = (label) => {
    seen.add(lastFrameHash);
    console.log(`  ${label.padEnd(28)} frameHash=0x${lastFrameHash.toString(16).padStart(8, "0")}`);
};
sample("main menu");

tap(ID.A);  run(30); sample("PLAY -> mode select");   // open mode select
tap(ID.DOWN); run(20); sample("mode select: ENDLESS");
tap(ID.DOWN); run(20); sample("mode select: OVERDRIVE");
tap(ID.UP);   run(20);
tap(ID.UP);   run(20); sample("mode select: WAVES");
tap(ID.A);  run(60); sample("gameplay started");

// Play: hold fire, wiggle, and periodically charge the beam.
// The starfield animates every frame while a run is live, so a long stretch of
// identical frames means the ROM hung. A settled menu (e.g. GAME OVER) is
// legitimately static, so only flag freezes while the run is still going: we
// re-press START/A to bounce off any menu and require the screen to stay
// frozen even after that.
let hangAt = -1;
let prevHash = lastFrameHash;
let stuckRun = 0;
held.add(ID.A);
for (let step = 0; step < TOTAL_FRAMES / 30; step++) {
    if (step % 7 === 3) held.add(ID.LEFT); else held.delete(ID.LEFT);
    if (step % 7 === 6) held.add(ID.RIGHT); else held.delete(ID.RIGHT);
    if (step % 11 === 5) held.add(ID.B); else held.delete(ID.B);
    run(30);
    if (lastFrameHash === prevHash) {
        stuckRun++;
        if (stuckRun >= 12) {
            // Nudge the UI: if this is a menu it will change, if the ROM is
            // wedged nothing will move.
            const before = lastFrameHash;
            held.delete(ID.A);
            tap(ID.START); tap(ID.B); tap(ID.A);
            run(60);
            held.add(ID.A);
            if (lastFrameHash === before && hangAt < 0) hangAt = step * 30;
            stuckRun = 0;
        }
    } else stuckRun = 0;
    prevHash = lastFrameHash;
}
held.clear();
run(60);
sample("after gameplay");

console.log(`\nTotal frames rendered: ${frames}`);
console.log(`Distinct sampled screens: ${seen.size}`);

let failed = false;
if (frames < TOTAL_FRAMES * 0.5) {
    console.error(`FAIL: only ${frames} frames rendered, expected >= ${Math.floor(TOTAL_FRAMES * 0.5)}`);
    failed = true;
}
if (hangAt >= 0) {
    console.error(`FAIL: video output appeared frozen around gameplay frame ${hangAt}`);
    failed = true;
}
if (seen.size < 4) {
    console.error(`FAIL: screens never changed (${seen.size} distinct) - UI likely stuck`);
    failed = true;
}

console.log(failed ? "\nSMOKE TEST FAILED" : "\nSMOKE TEST PASSED");
process.exit(failed ? 1 : 0);
