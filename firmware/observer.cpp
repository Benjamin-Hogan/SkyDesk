#include "observer.h"

#include <string.h>

#include "config.h"
#include "setup_model.h"

namespace {
Observer g_obs = {OBS_LAT, OBS_LON, OBS_ELEV_FT, OBS_PLACE};
}

const Observer &obs() { return g_obs; }

void obsSet(double lat, double lon, float elevFt, const char *place) {
  g_obs.lat = lat;
  g_obs.lon = lon;
  g_obs.elevFt = elevFt;
  strncpy(g_obs.place, place && place[0] ? place : "", sizeof(g_obs.place) - 1);
  g_obs.place[sizeof(g_obs.place) - 1] = '\0';
}

void obsSetElevation(float elevFt) { g_obs.elevFt = elevFt; }
double obsBuildLat() { return OBS_LAT; }
double obsBuildLon() { return OBS_LON; }
double obsMilesFromBuild() { return setupMilesBetween(g_obs.lat, g_obs.lon, OBS_LAT, OBS_LON); }
bool obsInGate() { return setupInGate(g_obs.lat, g_obs.lon, OBS_LAT, OBS_LON); }
bool obsInRadarGate() { return setupInRadarGate(g_obs.lat, g_obs.lon, OBS_LAT, OBS_LON); }
