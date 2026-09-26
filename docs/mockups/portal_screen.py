"""Setup portal device screens (3.0 feature 2, docs/12-setup-portal.md), drawn with tftsim.
Imported by screens.py. The phone page is docs/mockups/portal/index.html.

The QR is Nayuki's qrcodegen (pip install qrcodegen); the firmware uses ricmoo/QRCode (derived
from it) with the same parameters (version 4, ECC M, auto mask). test/host/test_setup.cpp
proves the device's modules equal this mock's, module for module.

Round 2 (answers design-review/3.0-portal-round-1-mr-stacks.md):
  M1 Cancel is a 36 px HOLD (1 s, with a fill); the boot button is 36 px
  S1 joined != page open: "No page? Open Safari to 192.168.4.1" until GET / is served
  S5 the hotspot password is lowercase letters only (no i/l/o) and kept per device
  S9 off-gate map: "No streets here - built for Gilbert, AZ"; radar adds what still works
  S11 a long SSID falls back to "Can't join your WiFi"
"""
from __future__ import annotations

import sys

from qrcodegen import QrCode, QrSegment

S = sys.modules["__main__"] if hasattr(sys.modules.get("__main__"), "plane_glyph") else __import__("screens")
import map_screen as M

QR_SCALE, QR_QUIET = 4, 4          # 33 modules x 4 px = 132 px, + 4 modules of quiet zone
QR_X, QR_Y = 8, 34
QR_BOX = (33 + 2 * QR_QUIET) * QR_SCALE    # 164 px
COL_X = QR_X + QR_BOX + 12                 # right column: 184
WHITE = "#FFFFFF"
INK = "#000000"
CANCEL = (8, 202, 112, 36)                 # >= 36 px, fires on a 1 s hold (R2-9)


def qr_modules(text: str) -> QrCode:
    return QrCode.encode_segments(QrSegment.make_segments(text), QrCode.Ecc.MEDIUM, 4, 4, -1, False)


def draw_qr(t, text):
    q = qr_modules(text)
    t.fillRect(QR_X, QR_Y, QR_BOX, QR_BOX, WHITE)   # camera wants dark-on-light, with a quiet zone
    ox = oy = QR_QUIET * QR_SCALE
    for y in range(q.get_size()):
        for x in range(q.get_size()):
            if q.get_module(x, y):
                t.fillRect(QR_X + ox + x * QR_SCALE, QR_Y + oy + y * QR_SCALE, QR_SCALE, QR_SCALE, INK)


def num_dot(t, x, y, n, c):
    t.fillCircle(x + 6, y - 5, 7, c)
    t.drawString(str(n), x + 6, y, "glcd", S.BG, "C")


def cancel_button(t, hold=0.0, nudge=False):
    """The fill starts on touch-down and grows over 1 s; dropouts < 150 ms don't reset it.
    Released early -> the label flashes "Keep holding" for 1.5 s (round 2)."""
    x, y, w, h = CANCEL
    t.fillRoundRect(x, y, w, h, h // 2, S.PANEL2)
    if hold > 0:
        t.fillRoundRect(x, y, max(h, round(w * hold)), h, h // 2, S.PLANE_DIM)
    t.drawString("Keep holding" if nudge else "Hold to cancel", x + w // 2, y + 23, "f2",
                 S.PLANE if nudge else S.TEXT, "C")


def fit_title(t, text, fallback, max_w, tok):
    return text if t.textWidth(text, tok) <= max_w else fallback


def portal(t, d):
    """d: ssid, pw, state (waiting | joined | page | saved), why_ssid (auto entry), mins_left, hold."""
    t.fillScreen(S.BG)
    st = d.get("state", "waiting")
    if st == "saved":
        S.check(t, 150, 84, S.OK)
        t.drawString("Saved", 160, 128, "fsb12", S.TEXT, "C")
        msg = fit_title(t, f'Restarting, joining "{d["new_ssid"]}"', "Restarting, joining your WiFi", 300, "f2")
        t.drawString(msg, 160, 152, "f2", S.MUTED, "C")
        return
    if d.get("why_ssid") is not None:
        title = fit_title(t, f'Can\'t join "{d["why_ssid"]}"', "Can't join your WiFi", 300, "fsb9")
        t.drawString(title, 10, 22, "fsb9", S.WARN)
    else:
        t.drawString("Phone setup", 10, 24, "fsb12", S.TEXT)
    draw_qr(t, f'WIFI:T:WPA;S:{d["ssid"]};P:{d["pw"]};;')

    x = COL_X
    if st == "joined":                                   # joined, but no page served yet
        S.check(t, x, 50, S.OK)
        t.drawString("Phone joined", x, 82, "f2", S.TEXT)
        t.drawString("No page? Open", x, 100, "f2", S.MUTED)
        t.drawString("Safari to", x, 116, "f2", S.MUTED)
        t.drawString("192.168.4.1", x, 132, "f2", S.TEXT)
    elif st == "page":                                   # GET / served
        S.check(t, x, 50, S.OK)
        t.drawString("Page open", x, 82, "f2", S.TEXT)
        t.drawString("Finish on your", x, 100, "f2", S.MUTED)
        t.drawString("phone", x, 116, "f2", S.MUTED)
    else:
        t.drawString("ON YOUR PHONE", x, 44, "glcd", S.DIM)
        for i, step in enumerate(("Open Camera", "Aim at the code", "Tap Join", "Page opens")):
            y = 62 + i * 19
            num_dot(t, x, y, i + 1, S.MUTED)
            t.drawString(step, x + 18, y, "f2", S.TEXT)
    t.drawString("NETWORK", x, 150, "glcd", S.DIM)
    t.drawString(d["ssid"], x, 168, "f2", S.TEXT)
    t.drawString("PASSWORD", x, 182, "glcd", S.DIM)
    S.num(t, d["pw"], x, 200, "fsb9", S.TEXT)

    cancel_button(t, d.get("hold", 0.0), d.get("nudge", False))
    if st == "waiting":                                  # once a phone joins, the 30-min session
        t.drawString("or open 192.168.4.1", 312, 220, "f2", S.MUTED, "R")   # rule applies: no countdown
        t.drawString(f'closes in {d["mins_left"]} min', 312, 234, "glcd", S.DIM, "R")


def settings_menu(t, d):
    """5 rows x 36 px (Flip screen added, 2026-09-26). Done became the back pill at (4,4) - the
    map / radar / Today chrome - which freed the bottom row. Holding 3 s still jumps to
    calibration. Flip screen reads Normal / Turned and applies on the finger's RELEASE: the menu
    redraws the other way up and touch is ignored until a clean release + 400 ms (flip round 1
    M1, no ghost tap). Each row owns its full 40 px band for touch; the back zone is y < 34."""
    t.fillScreen(S.BG)
    t.fillRoundRect(4, 4, 36, 24, 12, S.PANEL2)
    M.chevron_left(t, 17, 16, S.TEXT)
    t.drawString("Settings", 48, 23, "fsb12", S.TEXT)
    rows = [("Facing direction", d["facing"]), ("Dim at night", "On"), ("Flip screen", d.get("flip", "Normal")),
            ("Phone setup", "WiFi, location"), ("Calibrate touch", "done")]
    for i, (lab, val) in enumerate(rows):
        y = 34 + i * 40
        t.fillRoundRect(14, y, 292, 36, 8, S.PANEL2)
        t.drawString(lab, 26, y + 24, "fs9", S.TEXT)
        t.drawString(val, 282, y + 23, "f2", S.MUTED, "R")
        S.chevron(t, 290, y + 18, S.MUTED)


def settings_flip_moment(t, d):
    """Flip round 1 S7: what the owner actually sees at the tap. The unit sits turned on the desk
    with Flip screen = Normal, so the menu reads upside down; the finger (ring) is on the Flip row.
    On release the menu redraws this way up and the next 400 ms (plus a clean release) are ignored,
    so the bounce landing on "Dim at night" (the mirrored band) does nothing."""
    settings_menu(t, dict(d, flip="Normal"))
    t.img.paste(t.img.rotate(180))
    fx, fy = 319 - 160, 239 - (114 + 18)            # the Flip row's centre, as seen turned
    for r in (13, 14):
        t.drawCircle(fx, fy, r, S.PLANE)


def boot_error(t, d):
    """The retry countdown sits at the right of the failed row; the next line is the reason's
    words only (round 2 M3: 'Not found - 2.4 GHz only?  Retrying' overflowed the panel)."""
    S.boot(t, dict(d, hint=[]))
    t.drawString(d["countdown"], 280, 110, "f2", S.DIM, "R")
    t.fillRoundRect(50, 200, 220, 36, 18, S.PLANE)                     # 36 px (R1-M1)
    t.drawString("Set up from phone", 160, 224, "fsb9", S.BG, "C")


def radar_offgate(t, d):
    t.fillScreen(S.BG)
    t.fillRoundRect(4, 4, 36, 24, 12, S.PANEL2)
    M.chevron_left(t, 17, 16, S.TEXT)
    t.drawString("Radar", 48, 23, "fsb12", S.TEXT)
    t.drawString("The radar map is built for", 160, 84, "f2", S.MUTED, "C")
    t.drawString(d["build"], 160, 110, "fsb12", S.TEXT, "C")
    t.drawString(f'Your location is {d["mi"]} mi away.', 160, 138, "f2", S.MUTED, "C")
    t.drawString("Weather and planes use your location.", 160, 162, "f2", S.TEXT, "C")
    t.drawString("Rebuild the firmware for your area", 160, 192, "f2", S.DIM, "C")
    t.drawString("to see rain here.", 160, 210, "f2", S.DIM, "C")


# ---------------------------------------------------------------------------
SSID, PW = "SkyDesk-Setup-7F3A", "kpxwmqth"      # PW: lowercase, no i/l/o/r/v, kept per device (docs/12)
LONG_SSID = "Hogwarts Great Hall Guest 2.4GHz"  # 32 bytes
BOOT_FAIL = dict(ver="v3.0.0", steps=[("fail", 'Can\'t join "HomeNet"', ""), ("todo", "Clock", ""),
                                      ("todo", "Weather, traffic", "")], retry="Wrong password?", countdown="retry 12 s")
BOOT_NOTFOUND = dict(BOOT_FAIL, retry="Not found - 2.4 GHz only?")

SCENARIOS = {
    "settings-menu": (settings_menu, dict(facing="north-up")),
    "settings-menu-flipped": (settings_menu, dict(facing="north-up", flip="Turned")),
    "settings-menu-flip-moment": (settings_flip_moment, dict(facing="north-up")),
    "portal-waiting": (portal, dict(ssid=SSID, pw=PW, mins_left=14)),
    "portal-cancel-hold": (portal, dict(ssid=SSID, pw=PW, mins_left=14, hold=0.55)),
    "portal-cancel-nudge": (portal, dict(ssid=SSID, pw=PW, mins_left=14, nudge=True)),
    "portal-joined-nopage": (portal, dict(ssid=SSID, pw=PW, mins_left=13, state="joined")),
    "portal-page-open": (portal, dict(ssid=SSID, pw=PW, mins_left=13, state="page")),
    "portal-auto": (portal, dict(ssid=SSID, pw=PW, mins_left=4, why_ssid="HomeNet")),
    "portal-auto-long-ssid": (portal, dict(ssid=SSID, pw=PW, mins_left=4, why_ssid=LONG_SSID)),
    "portal-saved": (portal, dict(state="saved", new_ssid="HomeNet")),
    "portal-saved-long-ssid": (portal, dict(state="saved", new_ssid=LONG_SSID)),
    "boot-error-portal": (boot_error, BOOT_FAIL),
    "boot-error-notfound": (boot_error, BOOT_NOTFOUND),
    "radar-offgate": (radar_offgate, dict(build="Gilbert, AZ", mi="2,127")),
    "map-offgate": (M.map_screen, lambda: dict(M.SCENARIOS["map-z1"][1]() if callable(M.SCENARIOS["map-z1"][1])
                                               else M.SCENARIOS["map-z1"][1], offgate="Gilbert, AZ")),
}
