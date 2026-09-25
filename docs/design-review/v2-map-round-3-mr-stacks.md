# Design Review — v2 Plane Map — Round 3 (final) — Mr Stacks

Reviewed: `v2-map-round-2-response.md`; the updated `08-plane-map.md` (§What it is, Visual rules,
Data, States); `map_screen.py` (`qualifies()`, `mk()`, strip fit, focus tag and badges); the
`tftsim.py` Font 2 fix; and all seven `v2r3-map-*@3x.png` renders. I re-ran the `z0-selected`
scenario through `geo.py` to check the new prediction.

## Score: 8/10

## Verdict
Ship it. The map now tells the truth about the trigger:
- The disc is honestly named "nearby".
- Dim amber means one thing only, "will pop the card", and it's computed with the real v1
  ENTER test.
- The focused plane inside the disc shows `20° up`, so "why no card?" answers itself.

Across seven states, the screen is calm, legible at arm's length, and consistent with v1's
colour and wording rules. Two small items remain:
- A leftover sentence in 08 contradicts the new dim-amber rule.
- The prediction checks only the +60 s endpoint, so fast jets that fly *through* the trigger
  region within the minute get no warning.

Neither blocks shipping, and both take minutes to fix. It isn't a 9 because "will pop" is
still an unexplained colour for a first-time user, and nobody has used the map on the
device yet.

## Round 2 items

| # | Item | Status | Evidence / note |
|---|---|---|---|
| 1a | Prediction uses the full ENTER test | **Fixed** | `mk()` projects along track, ground speed and **vertical rate**, then runs `qualifies()` (≤ 3 nm, ≥ 25°, ≥ 300 ft above ground). Verified: SWA2208 at 12,000 ft is 3.6 nm out and 26° up now → `inbound = True`. SWA2073 at 20° and SWA2794 at 13° → `False`. |
| 1b | Elevation in the strip inside the disc | **Fixed** | `z0-selected`: `20° up · 2.1 mi NE`, worded like the card. The Font 2 degree sign renders correctly now that the tftsim parser bug is fixed (good catch; it was preview-only). |
| 1c | Disc semantics decided | **Fixed** | Renamed "nearby disc", documented as *not* the trigger, with the altitude-vs-distance table at the top of 08. |
| 1 re-mock | Hero mock shows real behaviour | **Fixed** | The selected in-disc plane is correctly not amber-dim, and the qualifying-soon plane outside it is dim amber. It's the clearest single frame in the set. |
| 2 | Strip priority | **Fixed** | `Southwest · 737 MAX 8`: the type is kept and the flight number omitted because it doesn't fit, per the rule. |
| NTH 1 | Truncation coverage ring | **Done (spec)** | No mock, since the fixture has 30 planes. Acceptable. |
| NTH 2 | Focus plane kept during loading | **Done** | `WIDENING...` next to the ring, and FALCON7 stays in the strip. |
| NTH 3 | Focus tag guaranteed, badge clearance | **Done** | See new nice-to-have #1 on occlusion. |
| NTH 4 | Dismissed planes = white | **Done in rules/code** | But the spec still contradicts it: see required #1. |
| NTH 5 | Empty state points somewhere | **Done** | `Nothing within 5 mi · nearest 7.2 mi NW`. |

## Required changes

1. **Remove the stale dim-amber sentence in 08 §Visual rules.** The row "Why amber rarely
   means 'in the zone'" still ends with "or after a dismiss (then dim amber)". Two rows
   above, the spec says dismissed planes are white and dim amber is "the only meaning". A
   firmware agent following that row will reintroduce the double meaning. Change it to
   "(after a dismiss it's drawn white)".

2. **Sample the prediction along the path, not just at +60 s.** `mk()` tests only the
   endpoint. A 250 kt jet covers about 4.2 nm in 60 s, so one crossing a 12k ft trigger
   region of about 3 nm radius on a chord can enter *and leave* between now and +60 s. It
   gets no dim amber, then the card pops "out of nowhere". Fix: evaluate `qualifies()` at
   +15, +30, +45 and +60 s (four cheap evaluations per plane per poll, about 160
   `geo.look` calls at 40 planes) and mark it inbound if any sample qualifies. Add a host
   test: a tangential 250 kt crossing at 1.5 nm abeam.

## Nice to have

1. **Focus-tag backing hides planes.** In `z2-busy` the `FALCON7` backing panel covers at
   least one arrival in the stream, and a hidden plane can't be tapped. When both sides are
   blocked, pick the side whose backing covers the fewest glyphs, or redraw the covered
   glyphs as 1 px `M_MUTED` outlines on top of the backing.
2. **Make "will pop" explain itself once.** Dim amber carries the feature's promise, but
   nothing says what it means. When a dim-amber plane is the focus, the strip's right side
   could read `overhead in ~40 s` (you already compute the time). After one tap the user
   knows what the colour means for good.
3. **Strip hint for in-disc planes.** `20° up` is honest, but users don't know the
   threshold. Consider `20° up · pops at 25°` when it fits, or put the rule in the README's
   map section.
4. **Offline focus.** In `offline-recent` the dimmed planes are still tappable. Either block
   selection while offline, or let the strip say `last seen 40 s ago` for the tapped plane,
   consistent with v1's stale card.
5. **Device checks to add to the README list:** band seams at the 3 s cadence, tap accuracy
   against the 28 px radius after touch calibration, and the time to first wide fetch when
   zooming out to 20 mi (spec says ≤ 5 s over TLS at a 35 nm radius).

## What works
- **The map is honest about what triggers a card.** The "nearby" disc, the real-test dim
  amber, and `20° up` in the strip close the gap I flagged in round 2 without adding a legend.
- **Priorities match v1.** Location and focus in amber, `operator · type` with the type
  never truncated, plain `7.2k` altitudes, miles everywhere.
- **Every state is designed and mocked:** live, busy, selected, empty (with a pointer to
  the nearest plane), loading (with the focus kept), offline-recent and offline-cleared.
- **Real data and synthetic stress**, both labelled, and every number computed rather
  than typed.
- **It fits the hardware:** a 7.7 KB band sprite, basemaps in flash, a tftsim guard against
  anti-aliased calls in 4-bit sprites, layout computed once per frame, and ground traffic
  filtered before truncation with the selected plane pinned.
- **The process worked.** Three rounds, each response point-by-point, found bugs disclosed
  (even preview-only ones). That's why this converged from 6 to 8.
