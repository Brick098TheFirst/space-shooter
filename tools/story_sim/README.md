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

`stalls=0` is the property that matters: it means no level can ever soft-lock.
Boss fights currently land at ~66-108 s each on a mid-progression loadout.

## Save tests

Round-trips the V9 save block through SRAM and checks progression rules
(unlock frontier, half-pay replays, checkpoint-on-death, deterministic shop
stock, the "LET ME BE FREE" flag).

```bash
gcc -O1 -I android/app/src/main/cpp -I gba/include -DPLATFORM_HOST=1 \
    tools/story_sim/save_tests.c tools/story_sim/save_test_stubs.c gba/src/save.c \
    android/app/src/main/cpp/story.c android/app/src/main/cpp/story_data.c \
    -o /tmp/save_tests -lm && /tmp/save_tests
```
