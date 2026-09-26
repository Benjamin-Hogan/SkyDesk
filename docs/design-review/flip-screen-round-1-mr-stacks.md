# Flip screen — round 1 — Mr Stacks

**Score: 6/10.** The layout is right. The flip moment isn't specified yet.

## What works
- Back pill + `Settings` title matches map, radar and Today. One way out, everywhere. Good trade for a fifth row.
- 5 × 36 px rows stay at our 36 px floor; the bottom row ending at y 230 leaves a 10 px margin. Acceptable.

## Must-fix
1. **Ghost tap after the flip.** The Flip row (y 114–150) mirrors to y 89–125, mostly **Dim at night**. A resistive release bounce or a second tap flips that setting unseen. Spec it: the flip fires **on release**, then touch is ignored until a clean release **plus 400 ms**. Mirror it in `settings_menu()`'s docstring and in docs/12.
2. **Touch-mapping recovery must be designed, not assumed.** Calibration has to be stored in the panel's native frame, with flip applied as a transform on top. Re-running calibration while flipped must yield a working map in *both* orientations, and host tests must cover that (`raw → flip → screen`). If the mapping is wrong, 3 s hold → calibrate is the escape. That only works if the hold is position-independent and calibration draws its crosshairs in the current orientation. State both explicitly.
3. **Apply flip before the first pixel at boot.** Boot, error and portal screens must come up the right way. A flipped device showing an upside-down "Can't join" is a support call.

## Should-fix
4. **Off/On is ambiguous.** "On" relative to what? Name the physical cue: row `Screen way up`, value `USB left` / `USB right` (check which side the CYD2USB port is on). Then the owner can match the value to the device without guessing. `Normal`/`Turned` is better than Off/On but still abstract.
5. **No dead gaps.** Each row should own a full 40 px band (the 4 px gaps split between neighbours), so a ±5 px resistive miss still lands on the row.
6. **Back zone overlaps row 1.** x < 48, y < 36 collides with row 1 at y 34–35. Make the back zone y < 34, or say which one wins.
7. **The flipped render proves nothing.** It's identical except `On`. Add a frame of what the owner actually sees at the moment of the tap: the menu upside down, with the finger position marked.

## Nice-to-have
8. Also expose Flip in the phone portal as a no-touch recovery path. Keep the device row as the primary control, because it needs instant physical feedback.
9. Calibrate touch sits on the bottom row, where resistive drift is worst: the row you need when touch is bad is the hardest to hit. The hold already covers this, so note it in the doc.
10. A one-line note that Flip is not Facing direction: turning the unit on the desk doesn't change which way it looks at the sky.

No confirmation dialog is needed: tapping the row again is the undo, as long as Must-fix 1 keeps that tap deliberate.

## Verdict
**Not cleared to build.** Answer Must-fix 1–3 in writing (docs/12 + docstring) and add the Should-fix 4 labels, then I'll re-score. I expect 8.
