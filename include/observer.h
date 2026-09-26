// observer.h - where SkyDesk is (docs/12-setup-portal.md -> Settings and the observer).
// The BUILD location (OBS_LAT/OBS_LON in secrets.h / config.h) is what the basemaps, the radar
// basemap and the clutter mask were generated for. The portal can save a different location;
// weather, ADS-B and the sky geometry follow it, and the maps check the gates.
// Pure: no NVS (the device calls obsSet() at boot from settings; host tests use the build).
#pragma once

struct Observer {
  double lat, lon;
  float elevFt;
  char place[24];
};

const Observer &obs();
void obsSet(double lat, double lon, float elevFt, const char *place);
void obsSetElevation(float elevFt);        // Open-Meteo's elevation for a saved location
double obsBuildLat();
double obsBuildLon();
bool obsInGate();                          // streets / towns / airports still fit (SETUP_GATE_MI)
bool obsInRadarGate();                     // the radar basemap + clutter mask still fit
double obsMilesFromBuild();
