# v3 ideas: design conversation with Mr Stacks (not scored)

Read: AGENTS, 01, 06, 07, 08, v2-map round 3 and its response, `ui_map.cpp`, and the five `final-*` renders.

Two facts shape everything below:
- **All 16 map palette slots are in use** (`basemap.h`). A new colour means a palette swap or a stolen slot.
- Two round-3 fixes (the backed tag and `overhead in ~30 s`) shipped **in firmware with no mock**. NTH 3 went to the README, which nobody at a desk reads.

## Map view: enhancement candidates

**M1. Inbound-first focus.**
- *Problem:* dim amber only explains itself when that plane is the focus. The focus defaults to the nearest plane, which is rarely the inbound one, so the fix almost never fires.
- *Proposal:* focus order becomes **selected > soonest will-pop > nearest**. It holds until the plane pops or misses 2 polls. While it's live, the idle timeout is suspended.
- *Value/Cost:* H / L. Logic only, host-testable.
- *Rule:* changes 08's "selected, else nearest". "Amber = the strip's plane" still holds.

**M2. 60 s leader + pop point.**
- *Problem:* nothing shows *where* it pops, so the disc invites the wrong guess (v2-R2-1).
- *Proposal:* **for the focus plane only**, a 1 px dotted amber leader to its +60 s position, plus a hollow 5 px amber square where `mapWillPopSecs()` first qualifies. It reuses the existing projection.
- *Value/Cost:* H / L.
- *Rule:* if the leader is drawn on any other plane, amber stops meaning "focus".

**M3. Tap again to cycle.**
- *Problem:* the `z2-busy` arrival stream sits 12–16 px apart. With a 28 px radius, you can't pick the third plane.
- *Proposal:* a repeat tap within 28 px and 3 s selects the next candidate, ordered by distance from the tap. The strip flashes `2 of 4 here`.
- *Value/Cost:* H / L.
- *Rule:* none. It's still "select, then tap the strip".

**M4. Off-screen focus pointer.**
- *Problem:* a selection persists out to the poll radius (2× zoom), so the strip can describe an invisible plane. That's a hole in 08.
- *Proposal:* an amber edge notch pointing at it, with no text.
- *Value/Cost:* M / L.
- *Rule:* no second compass word.

**M5. Climb/descend tick in tags.**
- *Problem:* "arriving or departing?" is the first PHX question, and `7.2k` doesn't answer it.
- *Proposal:* a 5 px triangle after the altitude, only beyond ±300 fpm (the card's threshold).
- *Value/Cost:* M / L. It adds 7 px of tag width, so re-check placement in `z2-busy`.

**M6. Threshold in the strip.**
- *Proposal:* inside the disc, `20° up · pops at 25°`, dropping the distance. The disc position already shows distance.
- *Value/Cost:* M / L.

**M7. Return with context after a pop.**
- *Problem:* after the card closes, you land back on an arbitrary focus.
- *Proposal:* the popped plane stays focused for 30 s, and the strip reads `passed overhead · 3.1 mi SE`.
- *Value/Cost:* M / L.
- *Rule:* the pop always wins. Never defer it because the screen is being touched.

**M8. Night map palette.**
- *Problem:* white glyphs and towns glare at 35 % backlight.
- *Proposal:* swap `MAP_PALETTE` at night: TEXT down to MUTED, darker roads, amber unchanged. Zero RAM.
- *Value/Cost:* L–M / L.
- *Rule:* keep the 4.5:1 floors.

**M9. First-run coach.**
- *Proposal:* for the first 3 visits, or until the first strip tap, the idle strip reads `Tap a plane · tap here for its card`.
- *Value/Cost:* L–M / L. With M1, it's mostly redundant.

**Cut:**
- **Altitude colour ramp.** No free slots, it's colour-only encoding, and it fights amber.
- **Pan, pinch and double-tap zoom.** Resistive drag is mushy, "you in the centre" is the model, and a double-tap window delays every select.
- **A list screen.** Five 36 px rows duplicate the map. M3 fixes the real problem.
- **Route on the map.** Routes are often wrong. The card handles them honestly.
- **Auto-zoom.** The saved zoom is user intent.
- **Radar under the planes.** Clutter, no palette room, and two refresh clocks.

## Map view: my recommended v3 bundle
1. **M1 inbound-first focus.** It makes "watch one coming" actually happen.
2. **M2 leader + pop point.** It shows *where*, and it depends on M1.
3. **M3 tap-again cycle.** It fixes the one place the map fails under PHX.
4. **M4 off-screen pointer.** It closes a spec hole.
5. **M5 climb/descend tick.** Cheap and genuinely informative.

M6 and M7 ride along if they mock cleanly. M8 and M9 wait. The bundle adds **no chrome and no gestures** apart from the repeat tap.

## The one other feature

| Rank | Feature | Verdict |
|---|---|---|
| 1 | **Rain radar** | The owner's pick, and right. The monsoon is when this desk needs it, and it fits the SD card and the band pipeline |
| 2 | Spotter log / "today's traffic" | Cheapest, with no network, and high delight (`212 today · 31 overhead · rarest: C-17`). Do it next |
| 3 | Setup portal | Only matters once the device leaves this desk. Do it before gifting |
| 4 | ISS passes | On-brand (it reuses the dome), but a few passes a week |
| 5 | Alerts | 7700 overhead is vanishingly rare, there's no speaker, and "military" is only a database flag |
| 6 | Plane photos | Licensing, a full card, and 2.8" adds little |

**Radar, done this owner's way.**
- **Entry.** Gilbert sees measurable rain ~30 days a year, so the radar must not be a screen you visit to see nothing.
  - Tapping the weather hero (icon + temp, a big target) opens it.
  - With echoes within 40 mi, the condition line gains `· rain 18 mi W ›` in `COL_RAIN`. The header's right side stays reserved for degraded status.
- **Screen.**
  - Map chrome: `‹`, you-dot, `(c) OSM · IEM`.
  - The frame time, `3:40 PM`, is top right on **every frame**. A radar frame without a timestamp lies.
  - One fixed extent of about 60 mi radius, on its own coarse basemap.
- **Motion.**
  - 6 past n0q frames loop at 0.4 s each, and the newest holds for 2 s.
  - A 6-segment frame bar marks the current frame.
- **Colour.**
  - Blue → cyan → white → magenta, anchored on `COL_RAIN` so it matches the hourly precip.
  - **No amber or orange.**
  - It gets its own 16-colour palette.
- **Pipeline.**
  1. Stream the PNG to SD while TLS is open.
  2. Close TLS, then decode once and quantize to a 4bpp 38.4 KB layer on SD.
  3. Playback reuses the basemap `memcpy` path.
  - **The decoder's ~40+ KB working RAM vs the 43 KB largest block is the #1 risk. Spike it before the mocks are final.**
- **Behaviour.** The card still pops over the radar. The idle timeout is 120 s.

## Design guardrails for the mocks
**What I'll check in round 1:**
- Rendering: `tftsim`, real fonts, band rules and no AA. Every palette slot swapped or stolen is listed.
- Semantics:
  - amber = focus, and dim amber = will-pop only
  - one compass word per screen, and no degree bearings
  - the leader, pop point and pointer are drawn for the focus plane only
- Touch: no new gesture apart from the repeat tap, and targets are ≥ 36 px.
- Strip worst case: long operator + countdown + `2 of 4 here`. The type never truncates.
- A RAM/flash/SD budget per feature, including the measured heap from the decode spike.
- The radar is legible at 35 % backlight.

**Map states:**
- inbound focus with countdown, leader and pop point
- the same plane +30 s later
- the tap-again cycle in `z2-busy` (2 frames)
- the off-screen pointer
- the post-pop return
- `z2-busy` with climb/descend ticks
- the two unmocked round-3 fixes
- **all 7 existing states re-rendered**, to prove no regression

**Radar and weather states:**
- weather with and without the rain cue
- scattered showers
- a monsoon line close by
- no echoes (`No rain within 60 mi · 3:40 PM`)
- loading
- frames stale > 15 min
- offline
- night
- the card popping over the radar
