# v3 round 3: response to Mr Stacks (A 8/10 · B 8/10, final)

Both parts are at 8/10, above the owner's 7 bar. Mr Stacks' two conditions for signing off are applied,
and the build is green. The renders are `docs/mockups/png/final-*`.

## Required changes

| # | Item | Status | What changed |
|---|---|---|---|
| 1 | [B] "No rain" over drawn rain; small cells over you unnamed | **Fixed (fw + mock)** | `radarClean` tracks each blob's closest pixel to you. A blob **within `RADAR_NAME_ALWAYS_MI` = 5 mi is always named**, so a young cell over you says `Raining here` and cues the weather screen. When only unnamed echoes are drawn, the strip says **`Small echoes only`**. `No rain within 50 mi` appears only when nothing is drawn. The frame bar, loop and backfill are keyed to "anything drawn" (`h.echo` / `RadarStatus::echo`). New mock: `final-radar-small-echoes` (the held-out 20 Apr frame). Host tests: a 10 px moderate cell at 1 mi is named (< 2 mi); a lone far 9 px echo is drawn and unnamed; the monsoon storm is still the headline. |
| 2 | [A] Card returns defeat the idle cap | **Fixed (fw)** | Two stamps in `main.cpp`: `g_idleTimerMs` (touch or card return) and `g_lastTouchMs` (real touches only). `mapIdleExpired(now, timerStart, lastTouch, willPop)`. Host test: a card every 90 s with no touch still returns to weather at 300 s. The radar screen uses the same rule without the pause. |

## Nice to have

| # | Status |
|---|---|
| 1 | **Done.** `wetHint()` also switches to the 10-min cadence when the newest frame has named rain within 60 mi. `RADAR_CUE_MAX_AGE_MIN` = 30 is in `config.h` and doc 10. |
| 2 | **Done.** Doc 10 now says the monsoon speck is *dropped* (mask proximity). |
| 3 | **Noted** as a device watch item. No notch was added pre-emptively. |
| 4 | **Done.** The stale "exit radius" comment is removed. |

## Build
`pio run`: SUCCESS (flash 70.5 %, static RAM 31.4 %). `sh test/host/run.sh`: all passed. The firmware
now includes the radar screen (`ui_radar.cpp`), the weather cue, and the navigation: weather hero →
radar, idle, M7 after-pop, and the UI frame-list ack.
