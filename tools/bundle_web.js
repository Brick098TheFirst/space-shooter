import esbuild from "esbuild";
import fs from "node:fs";
import path from "node:path";

// Copy wasm file to web/dist
const wasmSrc = "node_modules/romdev-platform-gba/wasm/mgba_libretro.wasm";
const wasmDst = "web/dist/mgba_libretro.wasm";
fs.mkdirSync("web/dist", { recursive: true });
fs.copyFileSync(wasmSrc, wasmDst);

// Copy GBA ROM to web/dist
if (fs.existsSync("SpaceUnlimited.gba")) {
    fs.copyFileSync("SpaceUnlimited.gba", "web/dist/SpaceUnlimited.gba");
}

// Copy the original audio assets: the WASM core's DirectSound path cannot
// deliver a continuous stream, so the client mirrors the GBA audio engine by
// playing these WAVs via WebAudio, synced to the in-ROM engine state.
const audioDir = "web/dist/audio";
fs.mkdirSync(audioDir, { recursive: true });
for (const name of ["menu", "game", "laser", "explosion", "pickup"]) {
    const src = `SpaceUnlimited.Windows/Assets/Audio/${name}.wav`;
    if (fs.existsSync(src)) {
        fs.copyFileSync(src, `${audioDir}/${name}.wav`);
    } else {
        console.warn(`WARNING: missing audio asset ${src}`);
    }
}

console.log("Bundling web emulator client...");
esbuild.buildSync({
    entryPoints: ["web/src/emulator-client.js"],
    bundle: true,
    platform: "browser",
    external: ["module", "fs", "path", "url", "crypto"],
    format: "esm",
    outfile: "web/dist/emulator-bundle.js",
    sourcemap: true,
    minify: false
});

console.log("Web assets bundled successfully!");
