# Space Unlimited: Recharged 🚀

A native **C# / .NET 8 Windows** remake of the original Scratch space shooter.
The Scratch project is preserved as `spaceshooter.sb3`; the new game is a fresh,
controller-ready implementation rather than an automatic 1:1 conversion.

## What changed

- Completely new command-deck menu, hangar, settings, pause and game-over screens
- Native 1280×720, 16:9 renderer with correct letterboxing at every window size
- Measured ship, laser, shield and asteroid sizes—no oversized converted sprites
- Keyboard, mouse and Xbox-compatible XInput controller support
- Three ship hulls, five paint colors, four engine trails and three weapon rigs
- Escalating waves, asteroid splitting, enemy drones and aimed enemy fire
- Dash with cooldown and invulnerability window
- Shield, rapid-fire and repair pickups
- Timed score combo and persistent local high score
- Music/effects volume, difficulty, screen-shake and fullscreen settings
- Original starfield, classic ship, laser, asteroid, shield, explosion, sound and
  music assets reused selectively from the `.sb3`

## Controls

| Action | Keyboard / mouse | Xbox-compatible controller |
|---|---|---|
| Move | `WASD` or arrow keys | Left stick or D-pad |
| Fire | `Space`, `Z`, or left mouse | `A` or right trigger |
| Dash | `Shift` or `X` | `X` or right bumper |
| Pause | `Esc` or `P` | Menu / Start |
| Menu select | `Enter` / click | `A` |
| Menu back | `Esc` / Backspace | `B` |

## Run from Visual Studio

Requirements: Windows 10/11 and Visual Studio 2022 with the **.NET desktop
development** workload (or the .NET 8 SDK).

1. Open `SpaceUnlimited.sln`.
2. Select `SpaceUnlimited.Windows` as the startup project.
3. Press **F5**.

No NuGet packages or game engine downloads are required.

## Make a portable Windows build

From PowerShell at the repository root:

```powershell
.\build-windows.ps1
```

The self-contained x64 build is written to:

```text
release\SpaceUnlimited-win-x64\SpaceUnlimited.exe
```

Keep its generated `Assets` directory next to the executable. The self-contained
publish does not require players to install .NET.

For a smaller framework-dependent build:

```powershell
dotnet publish .\SpaceUnlimited.Windows -c Release -r win-x64 --self-contained false
```

## Display proportions

Gameplay uses a fixed 1280×720 logical canvas. It is scaled uniformly and
letterboxed instead of stretched. Source art keeps its aspect ratio, while fixed
logical dimensions keep collision sizes and visual sizes aligned. See
`SpaceUnlimited.Windows/Assets/README.md` for the exact asset-size policy.
Validate the selected source files and logical-size policy with:

```bash
python3 tools/validate-assets.py
```

## Save data

Settings and high score are saved to:

```text
%LOCALAPPDATA%\SpaceUnlimitedRecharged\settings.json
```

## Repository layout

| Path | Purpose |
|---|---|
| `SpaceUnlimited.Windows/` | Native C# WinForms game and selected assets |
| `SpaceUnlimited.sln` | Visual Studio solution |
| `build-windows.ps1` | Portable Windows x64 publish script |
| `spaceshooter.sb3` | Preserved original Scratch 3 project |
