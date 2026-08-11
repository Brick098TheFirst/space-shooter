# Space Unlimited: Recharged 🚀

A native **Game Boy Advance (GBA)** and **C# / .NET 8 Windows** remake of the original Scratch space shooter.
The Scratch project is preserved as `spaceshooter.sb3`; the game is a fresh, controller-ready implementation built from the ground up for both modern platforms and authentic retro Game Boy Advance hardware.

---

## 🕹️ Game Boy Advance (GBA) Version

The GBA edition is written in native C (ARMv4T / libtonc) and compiles directly to a `.gba` ROM playable on real hardware (flashcarts, EverDrive, EZ-Flash), handheld emulators (Miyoo Mini, Anbernic, Steam Deck), standalone emulators (mGBA, VisualBoyAdvance, Delta), and via the included WebAssembly browser player.

### GBA Features

- **Native Mode 4 Graphics Engine:** 240×160 60 FPS double-buffered page-flipped renderer with zero screen tearing and a custom 256-color palette.
- **16.384 kHz DirectSound Audio Engine:** Hardware-timed signed PCM playback on both speakers for the full soundtrack (`menu.wav` and `game.wav`) plus polyphonic sound effects (lasers, explosions, shield pickups, and impacts).
- **2× Arcade Pace:** Gameplay advances two simulation ticks per displayed frame, while audio remains on its independent hardware timer so faster action does not pitch-shift or starve the soundtrack.
- **Complete Command Deck Interface:**
  - **Main Menu:** Play, Hangar, Settings, Controls & Guide, Credits + Live Ship Preview card.
  - **Hangar:** 5 Ship Paint Accents (Solar Orange, Ion Cyan, Nova Violet, Plasma Mint, Pulsar Gold), 4 Engine Trails (Ember, Ion, Nova, Aurora), and 3 Weapon Rigs (Spread Cannons, Twin Cannons, Focused Beam).
  - **Settings:** Difficulty modes (Cadet, Pilot, Ace), Music Volume, SFX Volume, Screen Shake toggle, and High Score reset.
  - **Controls & Guide Screen:** Full button diagram and pickup guide.
  - **In-Game Pause:** Frosted glass overlay with Resume, Restart, and Main Menu options.
  - **Game Over Screen:** Final score, best record indicator, waves cleared, and quick restart.
- **SRAM Save Persistence:** Settings and high scores persist to cartridge backup memory (`0x0E000000`).
- **Arcade Gameplay:** Asteroid splitting, sinusoidal enemy drones with aimed plasma shots, powerup drops (Shield, Rapid Fire, Repair), and a timed combo multiplier system (up to ×8).
- **Dash Mechanic:** High-speed evasion burst with invulnerability window, engine trail particle fountain, and recharge meter.

### GBA Controls

| Action | GBA Button | Keyboard (Web Player) | Xbox / USB Gamepad |
|---|---|---|---|
| Move Ship | **D-Pad** | `WASD` or Arrow keys | Left Stick or D-Pad |
| Fire Weapon | **A** | `Space`, `Z`, or `J` | `A` or `Right Trigger` |
| Dash (Invincible) | **B** / **R** / **L** | `Shift`, `X`, or `K` | `X`, `B`, or `Right Bumper` |
| Pause / Menu | **START** | `Enter` or `P` | `Start` / Menu |
| Menu Select | **A** | `Space` or `Enter` | `A` |
| Menu Back / Cancel | **B** | `Escape` or `Backspace` | `B` |
| Options / Reset | **SELECT** | `Backspace` or `Tab` | `Back` / View |

---

## 🛠️ Building & Running the GBA Version

### 1. Build the GBA ROM (`SpaceUnlimited.gba`)

```bash
npm run build:gba
```

This compiles all C sources in `gba/src/` against `libtonc` and outputs `SpaceUnlimited.gba` (~2.1 MB).

### 2. Regenerate Assets from Source

To regenerate the 8-bit 16.384 kHz signed-PCM audio and indexed graphics C arrays from the checked-in source assets:

```bash
npm run generate-assets
```

The WAV files under `SpaceUnlimited.Windows/Assets/Audio/` are the source assets and are included in the repository. The GBA cannot play WAV containers directly: the generator downsamples them to signed 8-bit PCM in `gba/src/audio_data.c`, which is linked into the ROM. `audio.c` mixes that data into an EWRAM ring and feeds DirectSound FIFO A from a Timer 0 interrupt.

### GBA audio implementation notes

- The GBA DirectSound FIFOs accept signed 8-bit PCM and are fed in 32-bit words; Timer 0 / 1024 with a `0xffff` reload produces the exact 16,384 Hz sample clock.
- The mixer uses a short producer/consumer ring instead of changing a live FIFO-DMA source pointer at every VBlank. This avoids reading beyond a small buffer, which was the source of the static-and-silence failure on hardware.
- The timer ISR is intentionally tiny: it writes one four-sample word every four timer ticks and acknowledges only `IRQ_TIMER0`. Music and up to four effects are mixed ahead of time by the main loop.
- This follows the GBA DirectSound timer/FIFO requirements documented in [gbadoc's Direct Sound guide](https://gbadev.net/gbadoc/audio/directsound.html), [Tonc's sound register reference](https://gbadev.net/tonc/sndsqr.html), and [Tonc's interrupt guidance](https://gbadev.net/tonc/interrupts.html).

### 3. Run the Interactive Web Player / Preview

To launch the web-based mGBA player with live controller and keyboard support on port 3000:

```bash
npm start
```

Navigate to `http://localhost:3000` to play in browser or download the `.gba` ROM.

---

## 🪟 Windows (.NET 8) Version

### Run from Visual Studio

1. Open `SpaceUnlimited.sln`.
2. Select `SpaceUnlimited.Windows` as the startup project.
3. Press **F5**.

### Make a portable Windows publish

```powershell
.\build-windows.ps1
```

The output executable is written to `release\SpaceUnlimited-win-x64\SpaceUnlimited.exe`.

---

## 📁 Repository Layout

| Path | Description |
|---|---|
| `SpaceUnlimited.gba` | Compiled Game Boy Advance ROM |
| `gba/` | GBA C source code and headers (`src/`, `include/`) |
| `tools/` | Asset converter (`generate_gba_data.py`), GBA compiler driver (`build_gba.js`), and bundler (`bundle_web.js`) |
| `web/` | Web emulator player UI, styling, and WebAssembly mGBA assets |
| `server.js` | Web server for live interactive preview and ROM downloads |
| `SpaceUnlimited.Windows/` | Windows C# / .NET 8 WinForms game |
| `SpaceUnlimited.sln` | Visual Studio solution |
| `spaceshooter.sb3` | Preserved original Scratch 3 project file |
