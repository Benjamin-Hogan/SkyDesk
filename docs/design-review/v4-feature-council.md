# SkyDesk V4: the team's single feature, "Sky Trails"

> **Team:** Captain Vega (aviation expert), Iris (UI/UX expert), Claude (moderator, systems).
> **Result:** unanimous, 2026-09-25. Ideas only: the mocks and Mr Stacks' 3 rounds come before any
> firmware.

## How we got here
- **Round 1 (competing slates) was rejected outright by the owner:**
  - flight-phase line, special-status markers, PHX flow/METAR, data-source marker, notables digest
  - night face, wayfinding, context-home radar, overnight recap, "first here"
  - runway banner, flight progress, history browser

  **Don't bring any of these back.** Heads-up is also dropped (3.0).
- **The owner's direction:** work as one team, and deliver one bigger feature. What they've loved
  is big and visual: the plane map, the radar loop, Today's Sky, the portal.
- **Step 1:** all three members independently pitched the same two concepts: a trails
  timelapse and a first-person "window" view.
- **Step 2:** Vega and Iris each crossed toward the other's pick. The Window depends on the desk
  facing a window and a visible plane to calibrate, so it was parked for V5. Trails won, framed as
  **live first, replay second** (never a history browser).

## The feature: Sky Trails
One new screen: **"what today drew"**. The map stays "what's here now".

- **Live canvas on a dimmed basemap.**
  - Every heard aircraft paints its path as it flies, with a bright head (TEXT).
  - The plane whose card just popped is amber on return, so the card hands off to the canvas.
- **Altitude colouring** (Vega), in feet MSL, 4 bands: < 3k, 3–8k, 8–15k, > 15k. There's a key in
  the strip. Gilbert is about 1,240 ft, so pattern traffic around 2,200 MSL is in the < 3k band.
- **Age** (Iris): the last hour draws bright, older paths draw dim. There are two levels per hue and
  no overlap step, which the palette doesn't allow.
- **The day:**
  - The pill reads `since 6:00 AM · 214 heard`.
  - At midnight the finished painting is saved to SD and a new dark canvas starts.
- **Replay:**
  - One play pill: the day in about 30 s.
  - A 24 h timeline in the bottom strip; tap to jump to an hour.
  - No date picker, no list.
- **Entry:** tap Today's hourly panel (the same day, seen two ways).
- **Honesty rules** (Vega):
  - Fresh ADS-B positions only; TIS-B `~` hexes skipped.
  - A gap over 30 s breaks the line; nothing is interpolated.
  - The wording is "heard", never "flew".
- **Radius** (Vega's amendment): Sky Harbor is about 17 nm away, so the PHX streams and the
  east/west flow flip need about **25 nm**.
  - Payload and RAM are to be measured against the real 25 nm and 35 nm fixtures.
  - If 25 nm doesn't fit, the pill says `Gateway & Chandler` and doesn't claim the PHX flow.

## Engineering (Claude)
- **Storage:** an SD point log (about 6 B/point, 1–6 MB/day), plus a 4-bit trail raster on SD
  with hourly snapshots for the replay.
- **Drawing:** strip-drawn through the existing map band pipeline, with no full-screen sprite.
- **Network:** no new host (the 5 s adsb.fi polls we already make).
- **Palette:** one palette. A dimmed basemap takes about 3 slots, then 4 altitude hues × 2
  brightness levels, DIM, MUTED, TEXT and the amber head: 15–16 slots, to be verified in the mock.
- **Risks:**
  - **Mud** at PHX density (Iris: if the 6 PM frame is mud at 2 m, the concept fails before any
    firmware).
  - **Payload** at 25 nm (Vega).

## The mock gate, before any firmware
- **Data:** mock from a **real recorded day**: adsb.fi at 25 nm every 5 s for 24 h, ideally a
  day with a PHX flow change. No synthetic streams. If the day has no flip, the mock shows none.
- **Frames** at 20 mi, at 1× and at 35 % night backlight:
  - 7 AM (sparse)
  - 6 PM (the peak)
  - a monsoon day
  - replay paused mid-day
  - a traffic-outage gap
  - no SD card
- Then Mr Stacks, 3 rounds.

## Parked for V5: the Window
A first-person view from the desk: the real ridgeline, live planes at their true azimuth and
elevation, and the trails in perspective.
- **Alignment:** drag the baked ridgeline to match the real one (Vega), then confirm with a plane
  tap.
- **Terrain honesty** (Iris): draw terrain only when aligned within 7 days and within 5° of the setup
  facing; otherwise, compass ticks only.

## Housekeeping (not the feature)
Stop showing the registration of, and stop looking up, aircraft flagged LADD/PIA (`dbFlags` & 8 / & 4). The live
feed and our fixtures both carry them today (Vega).
