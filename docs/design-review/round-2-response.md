# Round 2 — Designer response to Mr Stacks (score 7/10)

Round 2 met the bar (≥ 7). The brief was three rounds, so every required item
was still addressed. Renders: `docs/mockups/png/r3-*.png` / `r3-*@3x.png`.

## Required changes
| # | Change | Check |
|---|---|---|
| 1 | The trail is now a **fixed-length direction arrow**: 12 px gap, 20 px shaft, 8 px head, tip at 40 px or rim − 3 px. Direction is computed from the **true unclamped** positions (now → +60 s). It is hidden only if the plane moves < 3 px on the dome or there's < 24 px to the rim. Result: it shows on the main airliner mock and 5 others. It is hidden on stale (by design) and on the two low planes, where it now points *along* the horizon as the geometry dictates | `r3-plane-airliner-multi`, `-eagle`, `-overhead`, `-rotated`, `-forced`, `-alaska`; 06 §4.2 |
| 2 | The straight-up ring is r 20–21 and the glyph is drawn on top. The zenith dot and `UP` label are skipped when the glyph is within 20 px | `r3-plane-overhead` |
| 3 | The stale glyph is solid `PLANE_DIM`. In stale **and** departing, `NN° up` turns MUTED (confidence styling, 06 §4.3) | `r3-plane-stale`, `r3-plane-departing` |
| 4 | New `drawNumber()` adds +1 px tracking to every fsb9 number (stats, weather values, hourly temps). `288`, `12,400` and `99°` now separate. The simulator implements the same per-char drawing | all r3 |
| 5 | The header is fitted as **one line**: fsb12 → fsb9 for the whole line, then the operator is truncated (never the type). Worst cases are mocked | `r3-plane-worst-eagle` (`American Eagle · ERJ-175` + `+2 more`), `r3-plane-worst-alaska` (`Alaska · 737 MAX 9` + `NOT OVERHEAD`) |
| 6 | The sprite plan is corrected. The dome is a **16-bit** sprite (144², ~41 KB, allocated first at boot before WiFi), so TFT_eSPI's anti-aliased primitives work. The four text sprites stay 4-bit, use only non-blending calls, each has its own 16-entry palette, and they draw with palette indices. Budget updated: ~55 KB sprites, ≥ 60 KB free is still required | 03 §Rendering, §Memory; 06 §4.2 |
| 7 | City line: a `fitCities()` short-city table, then trim the longer name with `.`. `Dallas-Fort Worth → Phoenix` renders as `Dallas → Phoenix`; `Salt Lake City` as `Salt Lake` | `-eagle`, `-alaska`; 06 §4.4 |
| 8 | You-relative: `behind you` is **fsb12 TEXT** on its own line (the block tightens to 58/93/123/150). Distance moves to the dome's top-left corner. A `HORIZON` label sits just outside the rim at the lower left on every plane screen. **2-second test:** see below | `r3-plane-ga-rotated` |
| 9 | Gesture precedence is written down: dismiss fires on **release** after a 1–3 s press; settings fires **while held** at 3 s, and the release after it does nothing | 06 §7; 01 Touch table |

### 2-second glance test (proxy, reported honestly)
No human was available in this session. As a proxy, a fresh agent (Haiku) with
**no project context** was told "a screen on a friend's desk that tells you about
planes over the house" and asked for first impressions only:
- **Airliner:** "Northeast. Pretty high, nearly halfway up to overhead. Big jet
  airliner. Moving off to the northeast, climbing." ✔ All four correct. It noted
  "about 4 fists" is "a bit odd".
- **GA, you-relative:** "North, but you'd have to turn around. About halfway up.
  Small private plane." ✔ Direction and height correct. Direction of motion was
  "hard to tell", and it wasn't sure what "behind you" means exactly.

Conclusion: *where* and *what* land. Motion on the rotated dome is weaker, and
"fists" needs a first-time explanation. The fists explanation is a line in the
README / first-boot tip. A real human test is on the README's on-device checklist.

## Nice to have
| # | Status |
|---|---|
| 1 | Done: glyph clamp is `R−19` |
| 2 | Done: below 10° the line reads `near horizon · 6.7 mi` (shortened to fit with distance) |
| 3 | Done: departing keeps the stats; only the pill and bar say "leaving" |
| 4 | Done: GA line 2 is just the registration |
| 5 | Done: 06 §8 "Simulator vs device" |
| 6 | Done: `07-design-review.md` now exists |
| 7 | Done: facing is 200 in both the setup and rotated mocks |
