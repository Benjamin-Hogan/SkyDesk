// Route plausibility (docs/04 §Route plausibility). Pure math: host-testable.
#include "route_client.h"
#include "geo.h"

#include <math.h>

bool routePlausible(const RouteInfo &r, const Aircraft &a) {
  if (!r.hasRoute) return false;
  const geo::LatLon o{r.oLat, r.oLon}, d{r.dLat, r.dLon}, p{a.lat, a.lon};
  const double NEAR_M = 40 * geo::M_PER_NM;

  double dO, dD, brg;
  geo::distBearing(p, o, dO, brg);
  geo::distBearing(p, d, dD, brg);   // brg = bearing aircraft -> destination
  if (dO <= NEAR_M || dD <= NEAR_M) return true;   // departing / arriving

  double routeLen, b0;
  geo::distBearing(o, d, routeLen, b0);
  double xt, at;
  geo::crossAlongTrack(o, d, p, xt, at);
  const double maxXt = fmax(60 * geo::M_PER_NM, 0.15 * routeLen);
  if (fabs(xt) > maxXt) return false;
  if (at < -NEAR_M || at > routeLen + NEAR_M) return false;
  if (a.track >= 0) {
    double diff = fabs(fmod(a.track - brg + 540.0, 360.0) - 180.0);
    if (diff > 100) return false;
  }
  return true;
}
