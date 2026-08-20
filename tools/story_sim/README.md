# Story Mode verification harness

Headless builds of the real Android game core (`android/app/src/main/cpp/game.c`,
`story.c`, and `gba/src/save.c`) with rendering, audio and EOS stubbed out, so
Story Mode can be tested on a normal machine without an Android device.

## Playthrough simulator

Flies all 80 story levels with a scripted pilot that dodges the nearest threat,
learns the puzzle reticles, handles the drone-only board, and holds fire only
where the puzzle permits it. Proves every level terminates (no
unwinnable/stalling level) and reports how long each takes.

The campaign intentionally has **35 puzzle levels** running **33 different
rules** - collection runs, gate runs, tug-of-war, scans, escorts, arithmetic,
memory, fuses, chain detonations, polarity swaps, gravity, stealth and only
two scanner-target levels in the whole game. A rule appears once wherever
possible and never more than twice, so the `TWIST` column is the quickest
audit of that variety.

The pilot plays each rule rather than mashing the trigger: it flies to gates,
scoops cells, shoves the cargo pod toward its dock, holds a scan, watches the
tonnage on EXACT LOAD, alternates sides on TIDE LOCK, flanks plated rocks on
OPEN SIDE, swaps polarity to match incoming fire, and never fires on any of
the fifteen no-trigger rules. If a rule cannot be flown by this pilot, the
campaign has a level that cannot be finished - which is what `stalls=0` is
there to prove.

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
way to confirm the campaign is still varied rather than 80 rounds of the same
level. A tier-1 godmode run should report `stalls=0` across all 80 levels.

`tier=0` models a player who never spends a single chubbcoin for all 80 levels,
so it deliberately stalls on the late kill-quota and boss levels (the baseline
did too). Treat it as a stress probe for relative regressions, not a pass/fail
gate.

Boss fights currently land at ~30-115 s each on a mid-progression loadout;
the tutorial Alien on level 10 is deliberately the shortest of them.

## UI screenshots

Renders the real Android menus (the opening speech, the PLAY tab, the level
map, Mr Chubbs' shop, the result and wreck cards and the in-game HUD)
headlessly and writes PPMs, so layout regressions can be eyeballed without a
device.

It also asserts behaviour while it renders, and exits non-zero on a failure:
SKIP must reach the map and set `intro_seen`; a real clear of level 1 must pay
*more* than that level's floor reward; losing the last life must re-lock
the previous two levels and ground the ship; and the final whiteout must
persist, show, and clear the two-step reboot coda.

```bash
gcc -O1 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 -DEOS_ENABLED=1 \
    tools/story_sim/ui_shots.c tools/story_sim/ui_stubs.c \
    android/app/src/main/cpp/platform_host.c gba/src/menu.c gba/src/renderer.c \
    gba/src/gfx_data.c gba/src/starfield.c gba/src/save.c gba/src/boss_gfx.c \
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
markup, deterministic shop stock, the "LET ME BE FREE" flag). It also asserts
that at least half the campaign is puzzles and no named puzzle rule appears
more than twice. Mr Chubbs' docking rules cover: that he only
docks every fifth level, that leaving a dock spends it permanently, that a
spent dock never reopens, and that the unsold shelf still carries over to the
next one.

```bash
gcc -O1 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 \
    tools/story_sim/save_tests.c tools/story_sim/save_test_stubs.c gba/src/save.c \
    android/app/src/main/cpp/story.c android/app/src/main/cpp/story_data.c \
    -o /tmp/save_tests -lm && /tmp/save_tests
```
