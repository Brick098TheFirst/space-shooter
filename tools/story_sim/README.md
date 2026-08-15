# Story Mode verification harness

Headless builds of the real Android game core (`android/app/src/main/cpp/game.c`,
`story.c`, and `gba/src/save.c`) with rendering, audio and EOS stubbed out, so
Story Mode can be tested on a normal machine without an Android device.

## Playthrough simulator

Flies all 70 story levels with a scripted pilot that dodges the nearest threat
and holds fire. Proves every level terminates (no unwinnable/stalling level)
and reports how long each takes.

```bash
gcc -O2 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 \
    tools/story_sim/playthrough.c tools/story_sim/host_stubs.c gba/src/save.c \
    android/app/src/main/cpp/game.c android/app/src/main/cpp/story.c \
    android/app/src/main/cpp/story_data.c -o /tmp/story_sim -lm

/tmp/story_sim <tier> <godmode>
#   tier    0 = starter gear only, 1 = models Mr Chubbs purchases over the run
#   godmode 1 = keep the probe pilot alive (measures winnability + damage taken)
```

`stalls=0` at `tier=1` is the property that matters: it means no level can ever
soft-lock for a player who buys from Mr Chubbs as intended. The report's `TWIST`
column shows each level's objective and field modifier, which is the quickest
way to confirm the campaign is still varied rather than 70 rounds of the same
level.

`tier=0` models a player who never spends a single chubbcoin for all 70 levels,
so it deliberately stalls on the late kill-quota and boss levels (the baseline
did too). Treat it as a stress probe for relative regressions, not a pass/fail
gate.

Boss fights currently land at ~47-108 s each on a mid-progression loadout.

## UI screenshots

Renders the real Android menus (the opening speech, the PLAY tab, the level
map, Mr Chubbs' shop, the result and wreck cards and the in-game HUD)
headlessly and writes PPMs, so layout regressions can be eyeballed without a
device.

It also asserts behaviour while it renders, and exits non-zero on a failure:
SKIP must reach the map and set `intro_seen`; a real clear of level 1 must pay
*more* than that level's floor reward; and losing the last life must re-lock
the previous two levels and ground the ship.

```bash
gcc -O1 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 -DEOS_ENABLED=1 \
    tools/story_sim/ui_shots.c tools/story_sim/ui_stubs.c \
    android/app/src/main/cpp/platform_host.c gba/src/menu.c gba/src/renderer.c \
    gba/src/gfx_data.c gba/src/starfield.c gba/src/save.c \
    android/app/src/main/cpp/game.c android/app/src/main/cpp/story.c \
    android/app/src/main/cpp/story_data.c -o /tmp/ui_shots -lm

mkdir -p /tmp/ui
/tmp/ui_shots /tmp/ui     # writes 00a_intro_typing.ppm ... 14_map_grounded.ppm
```

## Save tests

Round-trips the V11 save block through SRAM and checks progression rules
(unlock frontier, half-pay replays, the dynamic payout — that idling pays only
the level's floor while a fast, accurate, untouched clear pays several times
more — the wreck-and-repair rules on death, the 14-page opening speech and its
markup, deterministic shop stock, the "LET ME BE FREE" flag) plus Mr Chubbs'
docking rules: that he only
docks every fifth level, that leaving a dock spends it permanently, that a
spent dock never reopens, and that the unsold shelf still carries over to the
next one.

```bash
gcc -O1 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 \
    tools/story_sim/save_tests.c tools/story_sim/save_test_stubs.c gba/src/save.c \
    android/app/src/main/cpp/story.c android/app/src/main/cpp/story_data.c \
    -o /tmp/save_tests -lm && /tmp/save_tests
```
