#!/usr/bin/env sh
# Host tests for the pure-logic modules: geo, tracker, readsb parsing, route
# plausibility. Needs g++ (C++17) and `pio pkg install` (for ArduinoJson).
#   sh test/host/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/skydesk_host_test"
AJ="$ROOT/.pio/libdeps/usb/ArduinoJson/src"
[ -d "$AJ" ] || { echo "ArduinoJson missing: run 'pio pkg install' first"; exit 2; }
INC="-I$ROOT/test/host/stubs -I$ROOT/include -I$AJ -I$ROOT/.pio/libdeps/usb/QRCode/src"
# -DSKYDESK_HOST_TEST: config.h includes secrets.h.example, never the owner's secrets.h -
# the captured fixtures are relative to the documented default location.
INC="-DSKYDESK_HOST_TEST $INC"
gcc -std=gnu99 -O1 -c "$ROOT/.pio/libdeps/usb/QRCode/src/qrcode.c" -o "$OUT.qrcode.o"   # ricmoo/QRCode (docs/12)
g++ -std=c++17 -O1 -Wall $INC -DFIXTURES="\"$ROOT/test/host/fixtures\""   "$ROOT/test/host/test_tracker.cpp" "$ROOT/test/host/test_parse.cpp"   "$ROOT/firmware/geo.cpp" "$ROOT/firmware/tracker.cpp"   "$ROOT/firmware/adsb_parse.cpp" "$ROOT/firmware/route_plausible.cpp" "$ROOT/firmware/aircraft_names.cpp" "$ROOT/test/host/test_map.cpp" "$ROOT/firmware/map_model.cpp" "$ROOT/test/host/test_radar.cpp" "$ROOT/firmware/radar_model.cpp" "$ROOT/firmware/radar_table.cpp" "$ROOT/firmware/radar_clutter.cpp" "$ROOT/test/host/test_today.cpp" "$ROOT/firmware/spotter.cpp" "$ROOT/firmware/chip.cpp" "$ROOT/test/host/test_setup.cpp" "$ROOT/firmware/setup_model.cpp" "$ROOT/firmware/observer.cpp" "$ROOT/firmware/trails_log.cpp" "$ROOT/test/host/test_trails.cpp" "$ROOT/test/host/test_touchmap.cpp" "$OUT.qrcode.o"   -o "$OUT"
"$OUT" | grep -v "^\[trk\]"
