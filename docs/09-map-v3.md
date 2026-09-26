# 09 — Plane Map v3: "watch one coming"

> Audience: agents extending `firmware/ui_map.cpp` / `map_model.cpp`. Status: **approved (Mr Stacks: A 8/10 · B 8/10, round 3), round-3 items applied; implemented**
> (`design-review/v3-*`). Builds on `08-plane-map.md`. Everything in 08 still holds unless a row
> below replaces it. Mockups: `docs/mockups/map_screen.py` (`SCENARIOS_V3` plus every v2 state
> re-rendered), rendered via `python docs/mockups/screens.py final map`.

## Why
The map's promise is "see the plane *before* it pops the card". In v2 that promise only
worked if you tapped the right plane. The focus defaulted to the *nearest* plane, and at PHX
that is rarely the one about to pass overhead. So the countdown that teaches dim amber
almost never showed (v3 ideas, Mr Stacks). v3 makes the map show, without a tap:
- which plane is coming
- where it will pop
- when it will pop

It adds **no new controls**. The only new gesture is tapping again on the same spot.

Chosen from `design-review/v3-ideas-mr-stacks.md`: his bundle M1–M5, plus M6 and M7. Deferred:
- M8, the night map palette. The backlight dim already covers night use.
- M9, the first-run coach. M1 makes it redundant.

## Changes

| # | Change | Rule |
|---|---|---|
| M1 | **Inbound-first focus** | **Focus order: selected > soonest will-pop > just popped (30 s, M7) > chip-passed (3.0, docs/11) > nearest** (v3-R1-11). Chip-passed is entered only by tapping the weather chip in its `Passed … ago` state. It lasts the rest of that 10-min window while the hex is in the traffic, and draws like M7. A will-pop focus is **held** until it pops, stops being will-pop for 2 polls in a row, or leaves the traffic. It is not swapped poll-to-poll for a slightly sooner plane. `mapPickFocus()` is host-tested. |
| M1b | **Idle pause, capped** (v3-R1-4, v3-R3-2) | **Two stamps.** The 120 s timer restarts on a touch *or* a return from a card, and is paused while a will-pop plane is the focus. The cap is **`MAP_IDLE_MAX_S` = 300 s since the last REAL touch**; card returns don't reset it. The same rule (without the pause) applies to the radar screen. Under a PHX arrival bank, an untouched map still returns to weather. `mapIdleExpired()` is host-tested with a stream of will-pop planes. |
| M1a | Will-pop sampling | `mapWillPop()` samples the path **every 5 s from +5 to +60 s** (12 samples). It returns the first qualifying time **and that position** (the pop point). The ENTER test is unchanged. Cost: 480 `look()` calls per poll at 40 planes, well under 5 ms. |
| M2 | **60 s leader + pop point** (focus only, v3-R2 NTH 7) | A 1 px amber dot every 3 px from the focus glyph to its +60 s position, starting 12 px out. If the focus is will-pop, add a **hollow 7×7 amber square at the pop point**, reserved before tags. The **square is hidden within 16 px of the glyph**, where the countdown already says it. The leader is drawn on the map even when the glyph is off it, **but only if it leads to a pop square**. A selected off-map plane that won't pop gets only the edge pointer, so there are never two amber marks on one edge. **No leader on a plane that has already passed** (M7). Not drawn for stale or offline planes. |
| M3 | **Tap again to cycle** (v3-R1-12, v3-R2-3) | On a tap, the candidates are the planes within 28 px of the **tap**, ordered by distance from the tap and **frozen as hex codes**, with the tap point as the **anchor**. **A tap within 28 px of the anchor, while the focus is in the frozen list, advances** to the next frozen candidate still in the traffic, wrapping. The round-2 rule ("the nearest plane is the focus") could only toggle between #1 and #2. Any other tap starts a new list. Polls moving the planes don't reshuffle it. Taps < 250 ms apart count as one. `k of N here` shows for 3 s after each tap. `mapTap()` host test: N+1 taps at one spot give 2, 3, 1, 2; a tap 40 px away starts a new list. |
| M4 | **Off-map focus pointer** | When the focus is off the map (a selected plane kept to the poll radius, a will-pop plane still beyond the 5 mi edge, or a plane under a chrome chip), a **14 px amber triangle with a 1 px BG outline** sits on the map edge (inset 6 px), pointing along the bearing. It slides along the edge to clear the chips. **Under the back chip it goes ≥ 20 px lower**, so it never reads as a second back button. No text. |
| M5 | **Climb / descend tick** | A 5 px `M_MUTED` triangle after the altitude, at **\|vertical rate\| ≥ 300 fpm**. **One width function (`tagWidth`, which includes the tick) serves both placement and the backing.** The synthetic PHX arrivals now descend (−600 … −1000 fpm), so the busy mocks exercise it. |
| M6 | **Threshold in the strip** | Inside the disc: **`20° up · needs 25°`**, falling back to `20° up`. v3-R1 NTH 4 asked for one verb. `overhead at 25°` never fits beside an airline name and type, so the threshold would never be seen. `needs 25°` is plain English and fits. |
| M7 | **Back from a pop** | After a card closes, that plane is focus **only if no plane is will-pop** (precedence above), for 30 s: **`passed · 3.0 mi NE`**, with no leader. |
| M8 | **Backed focus tag** (v3-R1-7, v3-R2-4) | If the focus tag can't be placed clear, it gets a **tag-sized** `M_PANEL` backing (+2 px). **Every glyph or cluster badge the backing touches is not drawn at all**, so there are no clipped fragments; it counts as hidden. The side hiding the fewest planes wins. The backing may not touch the focus ring. If both sides do, the tag steps outward (14 → 22 → 30 → 38 → 46 px) as a **callout**, joined to the ring by a 1 px amber line. |
| — | Firmware bug, **fixed in firmware** (v3-R1-13, v3-R2-5) | `char right[16]` is gone. The right side comes from the pure `mapStripRight()` (map_model.cpp) into 24-byte segments, with a `static_assert` on the longest string. It is host-tested with the real Font 2 width table (every state, the Sun Country worst case, and a 40 px stub for every state). `ui_map.cpp` calls it. |

## Strip, right side (replaces the 08 row)
The first matching state wins. **Fit rule** (v3-R1 NTH 3): use the first fallback that leaves room
for the **whole operator name** plus the type. If none does, use the first that leaves a 40 px
operator stub. **The type is never truncated or overlapped.** The flight number is inserted only when the state's **preferred** option was kept (v3-R2 NTH 8).

| State | Preferred | Fallbacks |
|---|---|---|
| Cycling (3 s after a tap) | `2 of 5 here` (TEXT) | — |
| Will pop | `overhead in ~45 s` (**amber**) `· 5.4 mi SW` | `overhead in ~45 s` → `in ~45 s` |
| Back from a pop (30 s) | `passed · 3.0 mi NE` | `passed` |
| Inside the disc | `20° up · needs 25°` | `20° up` |
| Otherwise | `7.2k ft · 5.0 mi NW` | — |

## States (mocked, `png/final-map-*`)
New in v3:
- `map-inbound`, `map-inbound-30s`: a **synthetic** SWA3319 5.4 nm SW, descending, is focused ahead of the nearest plane: leader, pop square, `overhead in ~45 s`, then `~15 s`.
- `map-cycle-1`, `map-cycle-2`: 20 mi busy (a real 35 nm sample plus 12 **synthetic** descending PHX arrivals): `1 of 5 here`, then `2 of 5 here`. The focus tag is a **callout** on a tag-sized backing. Planes it touches are simply not drawn.
- `map-offscreen`: a selected plane 9 nm W, with the edge pointer.
- `map-offscreen-inbound` (new): at 5 mi, the soonest will-pop plane is still below the map. You see the pointer plus the on-map part of its leader and its pop square.
- `map-worst-strip`: Sun Country (the longest operator in the table) plus a countdown. The operator stays whole: `in ~45 s`.
- `map-after-pop`: `passed · 3.0 mi NE`, no leader.
- `map-after-pop-inbound` (new): the same moment, plus a **synthetic** AAL2651 inbound. The inbound plane wins the focus (M7 precedence).

The v2 states re-rendered under v3 rules: `map-z1`, `map-z2-busy` (focus callout), `map-z0-selected` (`20° up · needs 25°`), `map-empty`, `map-loading`, `map-offline-recent` (now **`Traffic offline`**, see 10 → Vocabulary), `map-offline-cleared`.

## Tests (host, all passing: `sh test/host/run.sh`)
- `mapWillPop`: the first qualifying time is on the 5 s grid, and the point itself qualifies
- focus: will-pop beats nearest; a selection beats will-pop; a held plane isn't swapped for a sooner one; it drops after 2 misses; will-pop beats the just-popped plane; the just-popped plane beats nearest
- `mapIdleExpired`: 120 s idle; a will-pop stream keeps it paused up to 300 s, never beyond; wrap-safe
- `mapTap`: a frozen list around the anchor; N+1 taps at one spot give 2, 3, 1, 2; the order survives a poll; a 40 px tap starts a new list; debounce; dropped hexes are skipped; empty sky clears
- `mapStripRight`: every state and fallback, with the real Font 2 widths; Sun Country gives `in ~45 s`; `needs 25`; a 40 px stub for every state

## Firmware status
`ui_map.cpp` is ported to v3 and compiles:
- focus via `mapPickFocus` (hold, after-pop)
- leader and pop square
- tag-sized backings and callouts
- the edge pointer
- ticks
- the strip via `mapStripRight`
- the anchored tap cycle
- `mapCardClosed()` for M7, and `mapFocusWillPop()` for the capped idle in `main.cpp`

## Budget
No new RAM beyond about 40 bytes: the pop point and the 60 s point per plane are computed during
layout; the focus-hold and cycle state are a few words. No flash beyond code.
