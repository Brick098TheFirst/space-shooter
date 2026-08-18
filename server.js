import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execSync } from "node:child_process";

const root = path.dirname(fileURLToPath(import.meta.url));
const webRoot = path.join(root, "web");
const romPath = path.join(root, "SpaceUnlimited.gba");
const PORT = Number(process.env.PORT || 3000);

// Build the browser core on first launch. Production/CI normally does this in
// `npm run build`; keeping the guard makes a clean source checkout runnable.
if (!fs.existsSync(path.join(webRoot, "dist", "emulator-bundle.js"))) {
  try {
    console.log("Building web distribution bundle…");
    execSync("node tools/bundle_web.js", { stdio: "inherit", cwd: root });
  } catch (error) {
    console.warn("Could not auto-build the web player:", error.message);
  }
}

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".webmanifest": "application/manifest+json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".wasm": "application/wasm",
  ".gba": "application/octet-stream",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".wav": "audio/wav",
  ".mp3": "audio/mpeg",
  ".ogg": "audio/ogg"
};

function applySecurityHeaders(res) {
  // The mGBA WebAssembly core works best in a cross-origin-isolated page.
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  res.setHeader("X-Content-Type-Options", "nosniff");
  res.setHeader("X-Frame-Options", "SAMEORIGIN");
  res.setHeader("Referrer-Policy", "strict-origin-when-cross-origin");
  res.setHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
}

function resolveRequest(urlString) {
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(urlString, "http://local").pathname);
  } catch {
    return null;
  }
  if (pathname === "/") pathname = "/index.html";
  if (pathname === "/SpaceUnlimited.gba") return romPath;

  // Resolve against webRoot and verify the result stays inside it. This keeps
  // malformed or encoded traversal requests away from source/save files.
  const candidate = path.resolve(webRoot, `.${pathname}`);
  if (candidate !== webRoot && !candidate.startsWith(`${webRoot}${path.sep}`)) return null;
  return candidate;
}

const server = http.createServer((req, res) => {
  applySecurityHeaders(res);
  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405, { "Content-Type": "text/plain; charset=utf-8", "Allow": "GET, HEAD" });
    res.end("Method Not Allowed");
    return;
  }

  const filePath = resolveRequest(req.url || "/");
  if (!filePath) {
    res.writeHead(400, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("Bad Request");
    return;
  }

  let stat;
  try {
    stat = fs.statSync(filePath);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("404 — signal not found");
    return;
  }
  if (!stat.isFile()) {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("404 — signal not found");
    return;
  }

  const ext = path.extname(filePath).toLowerCase();
  const etag = `W/\"${stat.size.toString(16)}-${Math.floor(stat.mtimeMs).toString(16)}\"`;
  if (req.headers["if-none-match"] === etag) {
    res.writeHead(304, { ETag: etag });
    res.end();
    return;
  }

  const isReleaseBinary = ext === ".gba" || ext === ".wasm";
  const isBuiltAsset = filePath.includes(`${path.sep}dist${path.sep}`);
  const cacheControl = isReleaseBinary
    ? "public, max-age=3600, must-revalidate"
    : isBuiltAsset
      ? "public, max-age=31536000, immutable"
      : "no-cache";

  res.writeHead(200, {
    "Content-Type": MIME[ext] || "application/octet-stream",
    "Content-Length": stat.size,
    "Cache-Control": cacheControl,
    ETag: etag
  });
  if (req.method === "HEAD") {
    res.end();
    return;
  }
  fs.createReadStream(filePath).pipe(res);
});

server.on("clientError", (_error, socket) => {
  if (socket.writable) socket.end("HTTP/1.1 400 Bad Request\r\n\r\n");
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`Space Unlimited release server: http://0.0.0.0:${PORT}`);
  console.log("Browser flight deck, GBA ROM download, and PWA shell are ready.");
});
