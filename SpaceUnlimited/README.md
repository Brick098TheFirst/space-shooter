# SPACE UNLIMITED — desktop port

A **1:1 copy** of the Scratch project `spaceshooter.sb3` (by Brick098TheFirst),
repackaged as a standalone game that runs on **Windows, macOS and Linux**.

The game itself, every sprite, every sound and every image are the original
ones from the Scratch project. The only thing that changed is the "player":
instead of the Scratch editor, this folder is a tiny self-contained HTML5 game.

## How to run

No installation needed. Open **`index.html`** in any modern browser
(Chrome, Edge, Firefox, Safari). It works from `file://` (double-click) with
no internet connection — all assets are local.

- **Windows** — double-click `index.html` (or right-click → Open with → your browser)
- **macOS** — double-click `index.html` (or right-click → Open With → Safari/Chrome)
- **Linux** — `xdg-open index.html`, or from a terminal: `python3 -m http.server 8000` in this folder and open `http://localhost:8000`

> Tip: the window scales automatically. It also works with a touch screen.

## Controls

| Action               | Input                                    |
|----------------------|------------------------------------------|
| Start game           | `Enter` (on the menu / game-over screen) |
| Move ship            | mouse (ship follows the cursor)          |
| Shoot                | hold the left mouse button               |
| Pause / resume       | `P` or `Esc`                             |
| Mute / unmute        | `M`                                      |

The game auto-pauses when the window loses focus.

## What was copied 1:1 from the Scratch project

- The full game flow: menu → "get ready" → gameplay → game over → back to menu
- All 5 meteor types (big / med / med2 / small / tiny) with the exact spawn
  chances, glide times and behaviour from the original scripts
- All original music and sound effects (`menu`, `getready`, `frozenjam2` /
  `398220_frozenjam`, `pew`, `expl6`, `Explosion2`, `Pop`)
- The life-ship HUD, score + hi-score displays, the shield pickup
  (+1 life, capped at 3) and the 9-frame explosion animation
- Scratch timing: the game logic runs at 30 ticks per second, like Scratch
- Faithful quirks, including:
  - `med rock2` is destroyed by a laser but gives **no points** (as in the original)
  - small and tiny rocks are **silent** when destroyed by a laser
    (the original scripts play a sound those sprites don't own)
  - a shield picked up at 3 lives gives nothing (life is capped at 3)
  - the laser's "delete if touching red" check, which never fires in the
    original (nothing in the game is exactly `#ef0000` except the laser itself)

## The tiny few adjustments

1. **Pause** (`P` / `Esc`) and **auto-pause** when the window loses focus
2. **Mute** (`M`)
3. **Hi-score persists on this device** (localStorage) instead of the Scratch
   cloud variable — same behaviour, but saved locally
4. Small on-screen control hints on the menu screen
5. The ship is drawn with smooth 60 fps interpolation (the game logic is still
   exactly 30 ticks/s)

## Project layout

```
SpaceUnlimited/
├── index.html          the game (UI, rendering, audio, input)
├── game.js             the game engine (pure logic, 30 ticks/s)
├── test-game.js        headless engine tests (run with: node test-game.js)
└── assets/             all original sounds and images from the .sb3
```

## The original Scratch file

`../spaceshooter.sb3` is the untouched original project (the `.sb3` is a zip —
rename it to `.zip` to browse its contents: `project.json` plus the same
assets you see in `assets/`).
