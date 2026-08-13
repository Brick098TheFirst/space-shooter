# Space Unlimited: Recharged 🚀

A native **Game Boy Advance (GBA)** remake of the original Scratch space shooter.
The Scratch project is preserved as `spaceshooter.sb3`; the game is a fresh, controller-ready implementation built from the ground up for authentic retro Game Boy Advance hardware.

---

## 🕹️ Game Boy Advance (GBA) Version

The GBA edition is written in native C (ARMv4T / libtonc) and compiles directly to a `.gba` ROM playable on real hardware (flashcarts, EverDrive, EZ-Flash), handheld emulators (Miyoo Mini, Anbernic, Steam Deck), standalone emulators (mGBA, VisualBoyAdvance, Delta), and via the included WebAssembly browser player.

### GBA Features

- **Native Mode 4 Graphics Engine:** 240×160 60 FPS double-buffered page-flipped renderer with zero screen tearing and a custom 256-color palette.
- **18.157 kHz DirectSound Audio Engine:** Hardware-timed signed PCM playback on both speakers for the full soundtrack (`menu.wav` and `game.wav`) plus polyphonic sound effects (lasers, explosions, shield pickups, and impacts).
- **2× Arcade Pace:** Gameplay advances two simulation ticks per displayed frame, while audio remains on its independent hardware timer so faster action does not pitch-shift or starve the soundtrack.
- **Expanded Shop & Deep Progression Grind:**
  Earn coins by destroying asteroids, enemy fighters, completing waves, and racking up combo chains. Coins persist in SRAM and are spent in the **Upgrade Hangar** on permanent unlocks & upgrades:
  - **9 Ship Paints** (Solar Orange 800c, Ion Cyan [Starter 0c], Nova Violet 2,500c, Plasma Mint 5,500c, Pulsar Gold 14,000c, Crimson Void 30,000c, Obsidian Shadow 65,000c, Quantum Neon 120,000c, and the ultimate animated **Rainbow Prism** 1,000,000c)
  - **8 Engine Trails** (Ember Fire 1,000c, Ion Cyan [Starter 0c], Nova Purple 3,200c, Aurora Mint 7,000c, Solar Gold 16,000c, Crimson Flame 35,000c, Void Shadow 70,000c, animated **Rainbow Trail** 130,000c)
  - **6 Weapon Rigs** (Twin Cannons [Starter 0c], Spread Cannon 2,500c, Focused Beam 7,500c, Triple Blaster 20,000c, Plasma Wave 50,000c, Quantum Core 100,000c)
  - **8 Laser Crystals** (Ion Cyan [0c], Solar Gold 1,800c, Nebula Violet 4,500c, Toxic Mint 9,500c, Crimson Fury 22,000c, Emerald Surge 48,000c, Void Shadow 85,000c, animated **Rainbow Laser** 150,000c)
  - **8 Plain Stat Upgrades (5 Levels Each):** Speed, Fire Rate, Damage, Shield, Lives, Beam, Coins, Rapid. Names are the stat — no hull/reactor flavor text.
- **Spacious & Polished Shop UI:**
  - Tabbed category navigation (`PAINTS`, `TRAILS`, `WEAPON`, `LASERS`, `TECH`) with `L` and `R` triggers or D-pad.
  - Scrolling 5-item catalog panel with status badges (`[EQ]`, `OWN`, `Lv1/3`, `MAX`, price).
  - Live animated Ship Preview Chamber with active engine flare and weapon laser test-fire.
  - Dedicated item detail card with full stats, descriptions, and manual `[A] BUY` / `[A] EQUIP` actions.
- **SRAM Save Persistence:**
  Coins, high score, unlocked items, equipped loadout, and tech upgrade levels persist to cartridge backup memory (`0x0E000000`) and automatically sync to browser `localStorage` in the web player. On Android the same blob is written to the app-private `files/saves/save.sav` folder (`Context.getFilesDir()`), which is always readable/writable without any permission prompt or startup setup.
- **Hunter Enemy Fighters:** Crimson versions of the player ship track your horizontal position with red engine trails, then fire random 2–4 shot bursts straight downward using the same equipped laser appearance and laser sound as the player.
- **Balanced Arcade Gameplay:** Asteroid splitting, hunter enemy fighters, rare powerup drops (Shield, Rapid Fire, Repair), and a timed combo multiplier system (up to ×20).
- **Boss Fights (Waves mode):** Wave **5 / 15 / 25...** is a **mini-boss** (same kit as the battleship, about **4× less HP**, smaller hull, no dive lunge). Every **10th** wave is the full crimson battleship. Both clear the field and show a top-of-screen HP bar. The full boss takes ~**15 shots** from a maxed Focused Beam (more with a starter laser — you need good gear); the charged mega-beam cannot one-shot it. Killing a full boss pays a big score/coin bonus and drops three powerups; the mini-boss pays less and drops two. Runs on **both** GBA and Android.
- **Three Game Modes:** Play opens a mode select on both platforms — **Waves** (with bosses), **Endless**, and **Overdrive** (90-second score rush).
- **Big Laser Mechanic (replaces Dash):** Hold **B/R/L** for **2 seconds** to charge a full-screen piercing beam that fires for 3 seconds and reaches the top of the screen. The beam cuts through every rock and hunter in its column, dealing exactly `current laser damage ÷ 10` per frame (fractional damage accumulates, so even a 1-damage starter laser chews through rocks).
- **Settings Screen:** Difficulty (**Easy / Medium / Hard**), Music & SFX volume, and Screen Shake — plus an **Android-only Haptics** (vibration feedback) toggle. Persisted in the save file. Gyro / tilt steering has been removed.
- **Combo Coins:** Combo still multiplies coins, but the curve is much softer so a long chain is not a money printer. Locking **15x** is the payday (4.5x coins plus a lump bonus).
- **Rock Physics Overhaul:** Big rocks drift slow, medium rocks keep their classic speed, and small/tiny rocks scream past even faster. Later waves spawn a lot more rocks (and keep reinforcing the field) so it gets really difficult. Small rocks only appear once — when they fall off-screen they're gone for good (no more endless respawn wrap), so waves end cleanly.

### GBA Controls

| Action | GBA Button | Keyboard (Web Player) | Xbox / USB Gamepad |
|---|---|---|---|
| Move Ship / Navigate | **D-Pad** | `WASD` or Arrow keys | Left Stick or D-Pad |
| Fire / Buy / Equip | **A** | `Space`, `Z`, or `J` | `A` or `Right Trigger` |
| Beam (hold 2s) / Back | **B** | `Shift`, `X`, or `K` | `X`, `B`, or `Right Bumper` |
| Shop Tab Switch | **L** / **R** | `Q` / `E` or `Left/Right` | `LB` / `RB` or `Left/Right` |
| Pause / Menu | **START** | `Enter` or `P` | `Start` / Menu |
| Options / Reset | **SELECT** | `Backspace` or `Tab` | `Back` / View |

---

## 📱 Android (same C game, NDK host)

The Android app compiles the **same sources in `gba/`** with the NDK (`PLATFORM_HOST`). No WebView, no emulator. A Kotlin `GameView` presents the 16:9 widescreen 284×160 framebuffer scaled to the phone, runs at 90 Hz, plays the soundtrack at 44.1 kHz, uses native tap targets in menus (including shop tabs), and shows a circular virtual stick only while playing.

Play opens a **mode select** on **both** Android and GBA:

- **Waves** — clear each wave of asteroids and hunters. Wave 5/15/25... is a **mini-boss**; every 10th wave is a full **boss fight**.
- **Endless** — no waves; random hunter ships and rocks keep coming and the threat keeps rising.
- **Overdrive** — 90-second score rush with denser random spawns.

The Settings screen (Main Menu → Settings) has **Haptics** (vibration on hits, kills, beam charge-up, beam fire, and button presses) and a **CODES** row — tap it to open the cheat-code dialog (native Android text input). Known code: **`GIMMEMONEY`** tops your coin balance up to **$999,000,000,000,000** (999 trillion — the coin counter is 64-bit on Android and the save file upgrades to the V6 layout automatically). Haptics stay **Android-only** — the GBA has no vibration motor, so `platform_queue_haptic()` compiles to a no-op there. Gyro / tilt steering is gone. The big-laser button replaces DASH on the touch pad.

### Android-only gameplay differences

- **Life-only powerups:** The Shield and Rapid Fire drops are removed. The only powerup that drops is the **life (repair)** pickup, at exactly the same rarity the repair slice had before (the underlying drop chances are unchanged). The now-dead "Rapid" duration tech upgrade is hidden from the Upgrades screen (its data stays in the save).
- **Adaptive full-screen widescreen:** The framebuffer is always 160 px tall, but its width adapts to the phone (284–480 px) so the game **fills the entire screen — no side bars**, on any aspect ratio from 16:9 up to 21:9+. The app is locked to **landscape** (`sensorLandscape`), draws under the camera cutout, and re-fits live on fold/unfold or multi-window resizes. The GBA build keeps its fixed 240×160 Mode 4 screen untouched.

`cd android && ./gradlew assembleDebug` — see `android/README.md`.

---

## 🛠️ Building & Running the GBA Version

### 1. Build the GBA ROM (`SpaceUnlimited.gba`)

```bash
npm run build
```

This compiles all C sources in `gba/src/` against `libtonc` and outputs `SpaceUnlimited.gba`.

### 2. Regenerate Assets from Source

To regenerate the 8-bit signed-PCM audio and indexed graphics C arrays from the checked-in source assets:

```bash
npm run generate-assets
```

### 3. Run the Interactive Web Player / Preview

To launch the web-based mGBA player with live controller and keyboard support on port 3000:

```bash
npm start
```

Navigate to `http://localhost:3000` to play in browser or download the `.gba` ROM.

### 4. Headless ROM Smoke Test

Boots the built ROM in the mGBA core, drives the menus and a gameplay run with synthetic input, and fails if the ROM stops producing frames or the UI wedges:

```bash
node tools/smoke_gba.mjs            # exits non-zero on hang/crash
node tools/shot_gba.mjs /tmp/shots  # write PNG screenshots instead
```

---

## 📁 Repository Layout

| Path | Description |
|---|---|
| `SpaceUnlimited.gba` | Compiled Game Boy Advance ROM |
| `android/` | NDK host that compiles `gba/` C for Android (`PLATFORM_HOST`) |
| `Assets/` | Source images and WAV audio (extracted from `spaceshooter.sb3`) |
| `gba/` | GBA C source code and headers (`src/`, `include/`) |
| `tools/` | Asset converter (`generate_gba_data.py`), GBA compiler driver (`build_gba.js`), and bundler (`bundle_web.js`) |
| `web/` | Web emulator player UI, styling, and WebAssembly mGBA assets |
| `server.js` | Web server for live interactive preview and ROM downloads |
| `spaceshooter.sb3` | Preserved original Scratch 3 project file |
