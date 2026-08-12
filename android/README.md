# Space Unlimited: Recharged — Android

The Android app is a thin native shell around the **same GBA game** used everywhere else in this repo (`gba/` C sources → `SpaceUnlimited.gba` + the shared mGBA web player).

There is **no separate Android remake**. Gameplay, shop, audio, and saves are the GBA ROM.

## Layout

| Path | Role |
|---|---|
| `android/www/index.html` | Mobile touch UI that loads the shared emulator |
| `android/app/.../MainActivity.kt` | Fullscreen WebView that serves packaged assets |
| `SpaceUnlimited.gba` (repo root) | The game ROM, copied into the APK at build time |
| `web/dist/` | Bundled `emulator-bundle.js` + `mgba_libretro.wasm` |

Gradle task `syncGbaPlayerAssets` copies those files into `app/src/main/assets/www` before each build.

## Build

From the repo root:

```bash
npm install
npm run build
cd android
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

## Controls

Same mapping as the GBA / web player: D-Pad move, **A** fire, **B** dash, **L/R** shop tabs, **START** pause.
