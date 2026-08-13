/*
 * Headless GBA screenshotter: boots the ROM, drives input, and writes PNG
 * frames so gameplay/menus can be eyeballed without a browser.
 *
 * Usage: node tools/shot_gba.mjs [outDir]
 */
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const corePath = require.resolve("romdev-platform-gba/wasm/mgba_libretro.js");
const create_mgba = (await import(corePath)).default;

const ID = { B: 0, SELECT: 2, START: 3, UP: 4, DOWN: 5, LEFT: 6, RIGHT: 7, A: 8, L: 10, R: 11 };
const outDir = process.argv[2] || "/tmp/shots";
fs.mkdirSync(outDir, { recursive: true });

function writePng(file, rgba, w, h) {
    const raw = Buffer.alloc((w * 4 + 1) * h);
    for (let y = 0; y < h; y++) {
        raw[y * (w * 4 + 1)] = 0;
        rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
    }
    const chunk = (type, data) => {
        const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
        const td = Buffer.concat([Buffer.from(type, "ascii"), data]);
        const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
        return Buffer.concat([len, td, crc]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    fs.writeFileSync(file, Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0)),
    ]));
}
let crcTable = null;
function crc32(buf) {
    if (!crcTable) {
        crcTable = new Int32Array(256);
        for (let n = 0; n < 256; n++) {
            let c = n;
            for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
            crcTable[n] = c;
        }
    }
    let c = -1;
    for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
    return c ^ -1;
}

const rom = fs.readFileSync(path.resolve("SpaceUnlimited.gba"));
const m = await create_mgba({});
let pixelFormat = 0;
m._retro_set_environment(m.addFunction((cmd, data) => {
    if (cmd === 3) { m.setValue(data, 1, "i32"); return 1; }
    if (cmd === 10) { pixelFormat = m.getValue(data, "i32"); return 1; }
    return 0;
}, "iii"));
m._retro_init();

let latest = Buffer.alloc(240 * 160 * 4);
m._retro_set_video_refresh(m.addFunction((dataPtr, width, height, pitch) => {
    if (!dataPtr || !width || !height) return;
    const pitchWords = pitch >> 1;
    const src16 = new Uint16Array(m.HEAPU8.buffer, dataPtr, pitchWords * height);
    for (let y = 0; y < 160; y++) {
        for (let x = 0; x < 240; x++) {
            const c = src16[y * pitchWords + x];
            let r, g, b;
            if (pixelFormat === 2) { r = ((c >> 11) & 0x1f) << 3; g = ((c >> 5) & 0x3f) << 2; b = (c & 0x1f) << 3; }
            else { r = ((c >> 10) & 0x1f) << 3; g = ((c >> 5) & 0x1f) << 3; b = (c & 0x1f) << 3; }
            const o = (y * 240 + x) * 4;
            latest[o] = r; latest[o + 1] = g; latest[o + 2] = b; latest[o + 3] = 255;
        }
    }
}, "viiii"));
m._retro_set_audio_sample_batch(m.addFunction((p, n) => n, "iii"));
const held = new Set();
m._retro_set_input_poll(m.addFunction(() => {}, "v"));
m._retro_set_input_state(m.addFunction((port, dev, idx, id) => (held.has(id) ? 1 : 0), "iiiii"));

const romPtr = m._malloc(rom.byteLength);
m.HEAPU8.set(rom, romPtr);
const info = m._malloc(16);
m.setValue(info + 0, 0, "i32"); m.setValue(info + 4, romPtr, "i32");
m.setValue(info + 8, rom.byteLength, "i32"); m.setValue(info + 12, 0, "i32");
if (!m._retro_load_game(info)) { console.error("load failed"); process.exit(1); }

const run = (n) => { for (let i = 0; i < n; i++) m._retro_run(); };
const tap = (id, d = 4, g = 10) => { held.add(id); run(d); held.delete(id); run(g); };
let shotN = 0;
const shot = (label) => {
    const f = path.join(outDir, `${String(shotN++).padStart(2, "0")}-${label}.png`);
    writePng(f, latest, 240, 160);
    console.log("wrote", f);
};

run(200); shot("main-menu");
tap(ID.A); run(40); shot("mode-select");
tap(ID.A); run(90); shot("wave1-start");
held.add(ID.A);
run(600); shot("gameplay-early");
run(1800); shot("gameplay-mid");
run(3000); shot("gameplay-late");
held.clear();
run(300); shot("final");
console.log("done");
