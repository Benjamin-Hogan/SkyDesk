"""Python reference of docs/05-sky-geometry.md (mirrors firmware/geo.cpp).

Used so every number on a mockup (bearing, elevation, fists, distance, trail)
is computed from a real aircraft position - never typed in by hand.
"""
import math

R = 6371008.8
R_EFF = R * 4 / 3
NM = 1852.0
FT = 0.3048

COMPASS8 = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


def dist_bearing(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    d = 2 * R * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    az = math.degrees(math.atan2(math.sin(dl) * math.cos(p2),
                                 math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)))
    return d, (az + 360) % 360


def elevation(d_m, alt_ft, obs_elev_ft):
    if d_m < 1:
        return 90.0
    dh = (alt_ft - obs_elev_ft) * FT
    el = math.degrees(math.atan2(dh - d_m * d_m / (2 * R_EFF), d_m))
    return max(-10.0, min(90.0, el))


def destination(lat, lon, bearing_deg, dist_m):
    p1, l1, th = math.radians(lat), math.radians(lon), math.radians(bearing_deg)
    dl = dist_m / R
    p2 = math.asin(math.sin(p1) * math.cos(dl) + math.cos(p1) * math.sin(dl) * math.cos(th))
    l2 = l1 + math.atan2(math.sin(th) * math.sin(dl) * math.cos(p1), math.cos(dl) - math.sin(p1) * math.sin(p2))
    return math.degrees(p2), math.degrees(l2)


COMPASS16 = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
             "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"]


def compass16(az):
    return COMPASS16[int((az + 11.25) // 22.5) % 16]


def compass8(az):
    return COMPASS8[int((az + 22.5) // 45) % 8]


def look(obs, ac):
    """obs=(lat,lon,elev_ft); ac=dict(lat,lon,alt). -> (az, el, d_m)"""
    d, az = dist_bearing(obs[0], obs[1], ac["lat"], ac["lon"])
    return az, elevation(d, ac["alt"], obs[2]), d


def place(obs, az, ground_nm, alt_ft):
    """Put an aircraft at a given bearing/ground distance from the observer."""
    lat, lon = destination(obs[0], obs[1], az, ground_nm * NM)
    return {"lat": lat, "lon": lon, "alt": alt_ft}


def ahead(ac, track, gs_kt, secs, vrate_fpm=0):
    lat, lon = destination(ac["lat"], ac["lon"], track, gs_kt * 0.514444 * secs)
    return {"lat": lat, "lon": lon, "alt": ac["alt"] + vrate_fpm * secs / 60}


def relative_words(az, view_up):
    """Only when the owner configured VIEW_UP_DEG (dome is you-relative)."""
    rel = (az - view_up + 360) % 360
    words = ["ahead", "ahead-right", "to your right", "behind-right",
             "behind you", "behind-left", "to your left", "ahead-left"]
    return words[int((rel + 22.5) // 45) % 8]
