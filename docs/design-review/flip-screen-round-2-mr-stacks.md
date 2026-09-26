# Flip screen — round 2 — Mr Stacks

**Score: 8/10.** All three must-fixes are answered in code, not just in prose. What's left is edge polish.

## What works
- **M1 ghost tap: closed.** A Tap already fires on release (3 empty polls ≈ 75 ms, so a bounce inside that merges into the same press). `setupTouch` flips, redraws and sets `g_quietUntil = now + 400`. `setupTick` pushes the window out on *any* contact, so a bounce press that starts inside the window can't release as a Tap before the window ends (TAP_MAX_MS 600 < the rolling 400 ms). Hold is not gated, which is right: the escape stays open.
- **M2 recovery: designed.** `touch_map.h` stores calibration in the BASE frame and applies the mirror last. `touchCalibrate` solves flipped crosses back to BASE. `test_touchmap.cpp` covers raw → flip → screen and a calibration run both ways up. I ran `test/host/run.sh`: all passed. Old NVS calibrations were made at rotation 1 = BASE, so they stay valid. The Hold has no position test. The portal's Hold-to-cancel goes through `touchPoint`, so it mirrors too.
- **M3 boot: done.** `settingsLoad()` runs before `setRotation()` and before the first `fillScreen`. The portal boot happens after that, so the error screen and the portal come up the right way.
- **Other screens survive the turn.** Settings exits to weather, plane or Today, which call `fillScreen` on enter, or to map and radar, which set dirty and repaint every band. Nothing reads pixels back or uses hardware scroll. The rotation stays 1 or 3, so every width and height is unchanged.
- The flip-moment render is the one I asked for: the ring at (159, 107) is the mirrored Flip row's centre (y 89–125), and it's clear that the bounce lands on *Dim at night*. The mock and firmware x positions match (value at 282, chevron at 290).
- `Normal` / `Turned` with "tap again to undo" is an acceptable answer to S4.

## Must-fix
None.

## Should-fix
1. **The doc says the back zone never overlaps row 1. It does.** `menuRowAt` starts row 0's band at y 32 (`MENU_Y0 - 2`), and `MENU_BACK` covers y < 34. So x < 48, y 32–33 belongs to both, and Back wins because it's tested first. That's harmless, but docs/12 claims otherwise. Either clamp row 0's band to start at 34, or change the doc to "Back wins at y 32–33".
2. **Dead strip under the bottom row.** Row 4's band is 192–231, so y 232–239 does nothing. That's under *Calibrate touch*, the row you and I both call the hardest to hit. Let the last band run to 239.
3. **The quiet timer wraps.** `g_quietUntil` starts at 0 and is never cleared. Once uptime passes about 24.8 days, `(int32_t)(millis() - 0) < 0` is true, so the first Settings tap (Back included) is silently eaten until the next press re-arms the window. The same happens 24.8 days after any flip. A desk unit gets there. Fix it with a `bool g_quiet` flag, or reset the timer in `setupEnter()`.
4. **The quiet window depends on a second, independent sample.** `setupTick` calls `touchPoint()` again rather than using what `touchPoll` saw. A marginal-pressure bounce can pass `touchPoll` and miss every `setupTick` sample, then release as a Tap. It's unlikely, but the robust version is cheap: reject a Tap whose touch-down time is before `g_quietUntil` (expose `g_downMs` on the event).
5. **`SCREEN_FLIP_DEFAULT true` is the owner's desk, baked into the repo.** NVS already remembers the choice across flashes, so the default only matters on a fresh unit, and there it boots upside down, portal included. Default it to `false`, or add a comment in config.h saying it's deliberate for this unit.

## Nice-to-have
6. Move the menu band math (`menuRowAt`, the back zone) into a pure header and host-test the edges: 31/32/33/34, 71/72, 231/232, x 5/6/313/314. Round 1's bugs were exactly here.
7. The flip-moment mock shows the finger. Add a second frame, the menu the right way up with `Turned` just after the flip, so the "what happens next" is on paper.
8. Carried from round 1: Flip in the phone portal as a no-touch recovery.

## Verdict
**Cleared to build and flash.** Fixes 1–3 are one-liners; do them in this pass if convenient. They don't block. On-device checks the host can't prove: flip, then tap-and-bounce on the Flip row (*Dim at night* must not change); calibrate while Turned, then flip back and check a tap in each corner. I scored 8 as predicted, and it goes to 9 with S1–S3 and N6.
