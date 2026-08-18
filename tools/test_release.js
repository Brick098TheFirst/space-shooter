import { spawnSync } from "node:child_process";
import { mkdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";

const cc = process.env.CC || "gcc";
const out = join(tmpdir(), "space-unlimited-release-tests");
const uiOut = join(out, "ui");
mkdirSync(uiOut, { recursive: true });

function run(label, command, args) {
  console.log(`\n━━ ${label}`);
  const result = spawnSync(command, args, { stdio: "inherit", cwd: process.cwd() });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status || 1);
}

const include = ["-std=c11", "-O2", "-I", "android/app/src/main/cpp", "-I", "gba/include", "-DPLATFORM_HOST=1"];

run("Validate source assets", "python3", ["tools/validate-assets.py"]);
run("Check release server syntax", process.execPath, ["--check", "server.js"]);
run("Check browser client syntax", process.execPath, ["--check", "web/src/emulator-client.js"]);
run("Check service worker syntax", process.execPath, ["--check", "web/sw.js"]);
JSON.parse(readFileSync("web/manifest.webmanifest", "utf8"));

const saveBin = join(out, "save-tests");
run("Compile campaign/save contract", cc, [
  ...include,
  "tools/story_sim/save_tests.c", "tools/story_sim/save_test_stubs.c",
  "gba/src/save.c", "android/app/src/main/cpp/story.c", "android/app/src/main/cpp/story_data.c",
  "-o", saveBin, "-lm"
]);
run("Run campaign/save contract", saveBin, []);

const simBin = join(out, "story-sim");
run("Compile 80-level playthrough simulator", cc, [
  ...include,
  "tools/story_sim/playthrough.c", "tools/story_sim/host_stubs.c", "gba/src/save.c",
  "android/app/src/main/cpp/game.c", "android/app/src/main/cpp/story.c",
  "android/app/src/main/cpp/story_data.c", "-o", simBin, "-lm"
]);
run("Fly all 80 levels with the progression pilot", simBin, ["1", "1"]);

const uiBin = join(out, "ui-shots");
run("Compile real-menu visual smoke test", cc, [
  ...include, "-DEOS_ENABLED=1",
  "tools/story_sim/ui_shots.c", "tools/story_sim/ui_stubs.c",
  "android/app/src/main/cpp/platform_host.c", "gba/src/menu.c", "gba/src/renderer.c",
  "gba/src/gfx_data.c", "gba/src/starfield.c", "gba/src/save.c", "gba/src/boss_gfx.c",
  "android/app/src/main/cpp/game.c", "android/app/src/main/cpp/story.c",
  "android/app/src/main/cpp/story_data.c", "-o", uiBin, "-lm"
]);
run("Render menu and all nine sky themes", uiBin, [uiOut]);

console.log("\n✓ Release verification passed: assets, saves, 80 levels, menus, and web shell are healthy.");
