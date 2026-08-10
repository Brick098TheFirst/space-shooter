# space-shooter — SPACE UNLIMITED 🚀

A Scratch space-shooter game by **Brick098TheFirst**, now also available as a
standalone desktop game for **Windows, macOS and Linux**.

## What's in this repo

| Path | What it is |
|---|---|
| `spaceshooter.sb3` | The original Scratch project, restored in full (13.4 MB). It's a zip — rename to `spaceshooter.zip` to browse its contents (`project.json` + all sounds and images). |
| `SpaceUnlimited/` | A **1:1 copy** of the game as a self-contained HTML5 game with all original music, sounds and images. Runs on Windows, macOS and Linux — just open `SpaceUnlimited/index.html` in any browser. |
| `SpaceUnlimited/README.md` | How to run it and the (tiny) list of adjustments made for the desktop version. |

## Quick start

```
git clone https://github.com/Brick098TheFirst/space-shooter.git
cd space-shooter/SpaceUnlimited
# open index.html in any browser (no server, no internet needed)
```

Controls: mouse to move, hold click to shoot, `Enter` to start,
`P`/`Esc` to pause, `M` to mute.

## Note on the .sb3

The `spaceshooter.sb3` on this branch is the real, complete project
(13,374,133 bytes) recovered from commit `cfea49b`. An earlier rename commit
had replaced it with a 2-byte placeholder, which made the file unopenable in
Scratch — that is fixed here.

## No CI/workflows

This repository intentionally contains no `.github/workflows` files.
