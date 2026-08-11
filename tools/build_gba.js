import { buildGbaC } from "romdev-platform-gba";
import fs from "node:fs";
import path from "node:path";

const srcDir = "gba/src";
const incDir = "gba/include";

const sources = {};
for (const file of fs.readdirSync(srcDir)) {
    if (file.endsWith(".c")) {
        sources[file] = fs.readFileSync(path.join(srcDir, file), "utf8");
    }
}

const headers = {};
for (const file of fs.readdirSync(incDir)) {
    if (file.endsWith(".h")) {
        headers[file] = fs.readFileSync(path.join(incDir, file), "utf8");
    }
}

console.log(`Compiling GBA ROM: ${Object.keys(sources).length} source files, ${Object.keys(headers).length} header files...`);

buildGbaC({
    sources,
    headers,
    runtime: "libtonc"
}).then(res => {
    if (!res.ok) {
        console.error("Build failed at stage:", res.stage);
        console.error(res.log);
        process.exit(1);
    }
    const outPath = "SpaceUnlimited.gba";
    fs.writeFileSync(outPath, Buffer.from(res.binary));
    console.log(`Build SUCCESS! Output written to: ${outPath} (${(res.binary.length / 1024).toFixed(1)} KB)`);
}).catch(err => {
    console.error("Build exception:", err);
    process.exit(1);
});
