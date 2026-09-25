# v2 map, round 3 — Designer response to Mr Stacks (score 8/10, "ship it")

The map passed at 8/10, which completes three rounds (6 → 7 → 8). Both required items and most
nice-to-haves went straight into the firmware (`firmware/ui_map.cpp`, `firmware/map_model.cpp`).

## Required changes
| # | Change | Where |
|---|---|---|
| 1 | The stale sentence is fixed: "(after a dismiss it's drawn white)" | 08 §Visual rules |
| 2 | The prediction samples the **path** at +15/+30/+45/+60 s. It moved into `map_model.cpp` as `mapWillPopSecs()`, and the mock uses the same logic. **Host test added:** a 250 kt jet 5,000 ft up, crossing 1.5 nm to the side. The test proves that a +60 s-only check misses it, that the path samples flag it, and that the same jet flying away isn't flagged | `test/host/test_map.cpp` |

## Nice to have
| # | Status |
|---|---|
| 1 | **Done (firmware).** A backed focus tag goes on the side that covers the fewest planes |
| 2 | **Done (firmware).** A will-pop focus plane's strip reads `overhead in ~30 s` |
| 3 | **Done.** The 25° threshold is explained in the README |
| 4 | **Done.** Taps don't select planes while offline |
| 5 | **Done.** Map checks added to the README's on-device list |
