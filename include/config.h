// ==========================================================================
//  config.h  -  non-secret, compile-time configuration
// ==========================================================================
//
//  Every threshold named in docs/01-product-spec.md lives here. Location
//  defaults can be overridden in secrets.h (included first).
//
#pragma once

#include <Arduino.h>
#ifdef SKYDESK_HOST_TEST
#include "secrets.h.example"   // host tests: fixtures are relative to the default location
#else
#include "secrets.h"
#endif

#define FW_VERSION        "3.0.0-dev"
#define USER_AGENT        "SkyDesk/" FW_VERSION " (ESP32 CYD)"

// --------------------------------------------------------------------------
//  Observer location (Gilbert, AZ by default)
// --------------------------------------------------------------------------
#ifndef OBS_LAT
#define OBS_LAT           33.3528f
#define OBS_LON           (-111.7890f)
#define OBS_ELEV_FT       1240.0f
#define OBS_PLACE         "Gilbert, AZ"
#endif

// POSIX TZ string for the clock. Arizona has no DST.
#ifndef NTP_TZ
#define NTP_TZ            "MST7"
#endif
#define NTP_SERVER1       "pool.ntp.org"
#define NTP_SERVER2       "time.nist.gov"

// Default "up" direction for the sky dome: 0 = north-up. Setup -> Facing
// stores the owner's choice in NVS, which overrides this.
#define VIEW_UP_DEG_DEFAULT 0

#define UNITS_IMPERIAL    1     // mph / ft / mi / F (only imperial in v1)
#define CLOCK_24H         0

// --------------------------------------------------------------------------
//  Overhead trigger (docs/01-product-spec.md -> Core behavior)
// --------------------------------------------------------------------------
#define ENTER_RADIUS_NM       3.0f
#define ENTER_MIN_ELEV_DEG    25.0f
#define EXIT_RADIUS_NM        4.5f
#define EXIT_MIN_ELEV_DEG     15.0f
#define MIN_AGL_FT            300
#define MAX_SEEN_POS_S        15.0f
#define LOST_TIMEOUT_S        20
#define STALE_AFTER_S         6      // plane card shows "last seen" after this
#define MIN_PLANE_DWELL_S     12
#define DEPART_GRACE_S        4
#define FEATURE_SWITCH_DEG    10.0f  // challenger must beat featured el by this
#define FORCED_SHOW_S         20
#define OVERHEAD_EL_DEG       75.0f  // "UP / overhead" presentation
#define TRAIL_S               60

// --------------------------------------------------------------------------
//  Polling (docs/04-data-sources.md)
// --------------------------------------------------------------------------
#define POLL_RADIUS_NM        25    // V4 Sky Trails: PHX (~17 nm) streams (docs/13); was 12
#define NEARBY_NM             12    // "N nearby" and Today's nearby keep their 3.0 meaning
#define LOOKUP_RADIUS_NM      6.0f
#define ADSB_POLL_WEATHER_MS  5000
#define ADSB_POLL_PLANE_MS    2000
#define ADSB_FAILOVER_AFTER   3
#define ADSB_PRIMARY_RETRY_MS 600000UL   // go back to primary after 10 min
#define WEATHER_POLL_MS       600000UL   // 10 min
#define WEATHER_RETRY_MS      60000UL
#define WEATHER_STALE_S       1800       // header warns after 30 min
#define ROUTE_CACHE_N         8     // 3.0: was 16; pays for Today's Sky (docs/11 -> RAM)
#define ROUTE_TTL_MS          (6UL * 3600UL * 1000UL)
#define HTTP_TIMEOUT_MS       7000
#define WIFI_RETRY_MS         12000
#define MAX_AIRCRAFT          40

// Plane map (docs/08-plane-map.md)
#define MAP_IDLE_S            120    // no touch on the map -> back to weather
#define MAP_POLL_SCALE        2.0f   // poll radius = zoom radius * this (corners are 1.98x)
#define MAP_POLL_MAX_NM       35
#define MAP_POLL_Z0_MS        3000   // 5 mi zoom polls faster
#define MAP_DEFAULT_ZOOM      1      // 0 = 5 mi, 1 = 10 mi, 2 = 20 mi
#define MAP_TAP_RADIUS_PX     28     // fingertip on resistive touch is ~56 px
#define MAP_CLUSTER_PX        12     // 20 mi zoom: merge planes closer than this
#define MAP_OFFLINE_KEEP_S    60     // radar down: keep dimmed positions this long
// v3 (docs/09-map-v3.md)
#define MIN_VRATE_FPM       300    // climbing / descending shown beyond this (card + map ticks)
#define MAP_POP_STEP_S      5      // will-pop path sampling step (12 samples to +60 s)
#define MAP_CYCLE_MS        3000   // a repeat tap within this time (and 28 px) cycles
#define MAP_AFTER_POP_S     30     // the popped plane stays focused this long
#define MAP_LEADER_S        60     // focus leader length in seconds of flight
// Today's Sky (docs/11-today.md)
#define TODAY_MERGE_S       600    // the same hex back within this is the same pass
#define TODAY_PASSED_S      600    // the chip says "Passed N min ago" this long
#define TODAY_PROMISE_TOL_S 15     // a Heads-up promise is kept within max(this, 30 % of lead)
#define TODAY_RARE_MAX_DAYS 3      // a type seen on this many earlier days is never "rarest"
#define TODAY_LEARN_DAYS    3      // rarest says "learning" until this many days are logged
#define TODAY_GATE_MIN_N    30     // Heads-up gate: at least this many promises in 7 days ...
#define TODAY_GATE_PCT      80     // ... and at least this share kept
#define TODAY_IDLE_S        120    // Today page: no touch -> weather
#define TODAY_OUTAGE_MIN    5      // an hour with more traffic outage than this is dotted
// Sky Trails (docs/13-sky-trails.md)
#define TRAILS_KEEP_DAYS    30     // day files older than this are deleted (a day is ~3-11 MB)
// Setup portal (docs/12-setup-portal.md)
#define SETUP_GATE_MI       0.1    // a saved location this close to the build centre keeps the street maps
#define SETUP_RADAR_GATE_MI 1.0    // ... and this close keeps the radar (a radar pixel is ~0.5 mi)
#define SETUP_AUTO_S        180    // network visible but joining fails this long -> the portal opens
#define SETUP_IDLE_S        900    // the portal with no phone joined this long -> reboot to normal
#define SETUP_AUTO_IDLE_S   300    // ... after an AUTOMATIC entry (the router may just be refusing briefly)
#define SETUP_SESSION_S     1800   // a joined session with no request this long ends (abandoned)
#define SETUP_AUTO_CAP_S    600    // an AUTOMATIC portal lasts at most this unless the page is open
#define SETUP_HOLD_MS       1000   // Cancel fires on a hold this long (R2-9)

// Rain radar (docs/10-rain-radar.md)
#define RADAR_DBZ_LIGHT     20     // level thresholds (n0q dBZ); below LIGHT = dropped
#define RADAR_DBZ_MODERATE  30
#define RADAR_DBZ_HEAVY     40
#define RADAR_DBZ_VHEAVY    50
#define RADAR_DBZ_EXTREME   60
#define RADAR_MIN_BLOB_PX   6      // 8-connected, after the static clutter mask (docs/10 -> Clutter)
#define RADAR_FRAMES        6      // loop length, 10 min apart
#define RADAR_FRAME_STEP_S  600
#define RADAR_CUE_MAX_MI    40     // weather rain cue: 2..40 mi
#define RADAR_CUE_MIN_MI    2
#define RADAR_STALE_MIN     15     // newest frame older: WARN chip, loop stops
#define RADAR_CLEAR_MIN     60     // older: no rain drawn (offline)
#define RADAR_POLL_WET_S    600    // cue fetch cadence when rain is plausible
#define RADAR_POLL_DRY_S    1800   // ... otherwise
#define RADAR_POLL_OPEN_S   120    // radar screen open: n0q_0.json every 2 min (v3-R1-9)
#define RADAR_IDLE_S        120
#define RADAR_DEFER_MAX_S   120    // radar work deferred for a hot sky at most this long (v3-R2-1)
#define RADAR_QUORUM_MIN    95     // % of radars reporting, below = 'partial coverage'
#define RADAR_MISS_PERMILLE 5      // > 0.5 % non-n0q colours = frame unreadable (v3-R1-1)
#define RADAR_NAME_MIN_PX   12     // a blob is NAMED (strip/cue) if >= this with moderate+ ...
#define RADAR_NAME_LIGHT_PX 40     // ... or >= this of any rain (virga filter)  (v3-R2-2)
#define RADAR_NAME_ALWAYS_MI 5     // ... or ANY size this close to you (v3-R3-1)
#define RADAR_CUE_MAX_AGE_MIN 30   // the weather cue is never drawn from an older frame
#define MAP_IDLE_MAX_S      300    // since the last REAL touch; card returns don't reset it (v3-R3-2)

#define ADSB_PRIMARY_URL      "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d"
#define ADSB_FALLBACK_URL     "https://api.adsb.lol/v2/point/%.4f/%.4f/%d"
#define ADSBDB_URL            "https://api.adsbdb.com/v0/aircraft/%s"

// --------------------------------------------------------------------------
//  Display
// --------------------------------------------------------------------------
#define TFT_ROTATION      1      // BASE landscape; Settings -> Flip screen turns it 180 (rotation 3)
#define SCREEN_FLIP_DEFAULT true // the owner's desk: flipped (2026-09-26)
#define SCREEN_W          320
#define SCREEN_H          240
#define UI_TICK_MS        25

// Backlight (LEDC PWM on TFT_BL = GPIO 21)
#define BL_LEDC_CH        0
#define BL_FULL           255
#define BL_NIGHT          90         // ~35 %
#define BL_NIGHT_PAD_MIN  30         // night = sunset+30 .. sunrise-30
#define LDR_PIN           34
#define LDR_AUTO          0

// --------------------------------------------------------------------------
//  Touch (XPT2046, bit-banged in touch_input.cpp - HSPI belongs to the SD card)
// --------------------------------------------------------------------------
#define TOUCH_SCLK        25
#define TOUCH_MOSI        32
#define TOUCH_MISO        39
#define TOUCH_CS          33
// Uncalibrated raw extents, in the BASE orientation (rotation 1). The flip is applied to screen
// coordinates after mapping (touch_input.cpp), so these never change with it.
#define TOUCH_RAW_MIN_X   200
#define TOUCH_RAW_MAX_X   3700
#define TOUCH_RAW_MIN_Y   240
#define TOUCH_RAW_MAX_Y   3800
#define TOUCH_PRESSURE_MIN 300
#define TAP_MAX_MS        600
#define LONG_PRESS_MS     1000
#define SETTINGS_HOLD_MS  3000

// --------------------------------------------------------------------------
//  microSD (v3, docs/10): own SPI bus = the HSPI peripheral on the CYD slot pins
// --------------------------------------------------------------------------
#define SD_SCLK           18
#define SD_MISO           19
#define SD_MOSI           23
#define SD_CS             5
#define SD_FREQ_HZ        20000000
#define SD_MAX_FILES      2          // each costs ~4.6 KB of heap, mounted for good at boot (sd_store.cpp)

// Onboard RGB LED (active-LOW)
#define LED_R_PIN         4
#define LED_G_PIN         16
#define LED_B_PIN         17

// --------------------------------------------------------------------------
//  Palette (docs/06-ui-spec.md -> Palette). Keep in sync with screens.py.
// --------------------------------------------------------------------------
#define RGB565(r, g, b) \
  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_BG          RGB565(0x0A, 0x11, 0x20)
#define COL_PANEL       RGB565(0x13, 0x1C, 0x2E)
#define COL_PANEL2      RGB565(0x1C, 0x27, 0x40)
#define COL_HAIR        RGB565(0x2A, 0x37, 0x52)
#define COL_TEXT        RGB565(0xEE, 0xF2, 0xF8)
#define COL_MUTED       RGB565(0x9A, 0xA6, 0xBE)
#define COL_DIM         RGB565(0x78, 0x86, 0xA2)
#define COL_PLANE       RGB565(0xFF, 0xB0, 0x2E)
#define COL_PLANE_DIM   RGB565(0x8A, 0x64, 0x24)
#define COL_PLANE_FAINT RGB565(0x3A, 0x33, 0x26)
#define COL_OK          RGB565(0x3D, 0xDC, 0x84)
#define COL_WARN        RGB565(0xFF, 0x8A, 0x3D)
#define COL_ERR         RGB565(0xFF, 0x5A, 0x5F)
#define COL_SUN         RGB565(0xFF, 0xD2, 0x4A)
#define COL_CLOUD       RGB565(0xC9, 0xD3, 0xE3)
#define COL_RAIN        RGB565(0x4A, 0xA3, 0xFF)
#define COL_MOON        RGB565(0xE8, 0xE3, 0xC8)
