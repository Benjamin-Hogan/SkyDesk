#!/usr/bin/env sh
# Host tests for the pure-logic modules: geo, tracker, readsb parsing, route
# plausibility. Needs g++ (C++17) and `pio pkg install` (for ArduinoJson).
#   sh test/host/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/skydesk_host_test"
AJ="$ROOT/.pio/libdeps/usb/ArduinoJson/src"
[ -d "$AJ" ] || { echo "ArduinoJson missing: run 'pio pkg install' first"; exit 2; }
INC="-I$ROOT/test/host/stubs -I$ROOT/include -I$AJ"
# -DSKYDESK_HOST_TEST: config.h includes secrets.h.example, never the owner's secrets.h -
# the captured fixtures are relative to the documented default location.
INC="-DSKYDESK_HOST_TEST $INC"
g++ -std=c++17 -O1 -Wall $INC -DFIXTURES="\"$ROOT/test/host/fixtures\""   "$ROOT/test/host/test_tracker.cpp" "$ROOT/test/host/test_parse.cpp"   "$ROOT/firmware/geo.cpp" "$ROOT/firmware/tracker.cpp"   "$ROOT/firmware/adsb_parse.cpp" "$ROOT/firmware/route_plausible.cpp" "$ROOT/firmware/aircraft_names.cpp" "$ROOT/test/host/test_map.cpp" "$ROOT/firmware/map_model.cpp" "$ROOT/test/host/test_radar.cpp" "$ROOT/firmware/radar_model.cpp" "$ROOT/firmware/radar_table.cpp" "$ROOT/firmware/radar_clutter.cpp"   -o "$OUT"
"$OUT" | grep -v "^\[trk\]"
