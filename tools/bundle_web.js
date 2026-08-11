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
