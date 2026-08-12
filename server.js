import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execSync } from "node:child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = process.env.PORT || 3000;

// Ensure web distribution bundle exists
if (!fs.existsSync(path.join(__dirname, "web", "dist", "emulator-bundle.js"))) {
    try {
        console.log("Building web distribution bundle...");
        execSync("node tools/bundle_web.js", { stdio: "inherit", cwd: __dirname });
    } catch (e) {
        console.warn("Failed to auto-bundle web dist:", e.message);
    }
}

const MIME_TYPES = {
    ".html": "text/html",
    ".js": "application/javascript",
    ".wasm": "application/wasm",
    ".gba": "application/octet-stream",
    ".css": "text/css",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".wav": "audio/wav",
    ".mp3": "audio/mpeg",
    ".ogg": "audio/ogg"
};

const server = http.createServer((req, res) => {
    // Cross-origin headers for WebAssembly & SharedArrayBuffer if needed
    res.setHeader("Access-Control-Allow-Origin", "*");
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");

    let reqPath = req.url.split("?")[0];
    if (reqPath === "/") reqPath = "/index.html";

    let filePath;
    if (reqPath === "/android" || reqPath === "/android/") {
        filePath = path.join(__dirname, "android", "index.html");
    } else if (reqPath.startsWith("/android/")) {
        filePath = path.join(__dirname, reqPath);
    } else if (reqPath === "/SpaceUnlimited.gba") {
        filePath = path.join(__dirname, "SpaceUnlimited.gba");
    } else if (reqPath.startsWith("/dist/")) {
        filePath = path.join(__dirname, "web", reqPath);
    } else {
        filePath = path.join(__dirname, "web", reqPath);
    }

    if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("404 Not Found");
        return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || "application/octet-stream";

    res.writeHead(200, { "Content-Type": contentType });
    fs.createReadStream(filePath).pipe(res);
});

server.listen(PORT, "0.0.0.0", () => {
    console.log(`Space Unlimited Web Server listening on http://0.0.0.0:${PORT}`);
    console.log(`- GBA Web Player: http://0.0.0.0:${PORT}/`);
    console.log(`- Android Mobile Edition: http://0.0.0.0:${PORT}/android/`);
});
