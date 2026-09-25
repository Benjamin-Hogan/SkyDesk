#include "tracker.h"

#include <math.h>

namespace {

constexpr int MAX_TRACKED = 8;

struct Tracked {
  Aircraft a;
  uint32_t freshMs;   // millis() when its position was last fresh
  bool dismissed;
  bool used;
};

Tracked   g_trk[MAX_TRACKED];
TrackView g_view;
char      g_featured[7] = "";
bool      g_manual = false;         // user picked this plane via tap
bool      g_arrival = false;
uint32_t  g_planeSinceMs = 0;
uint32_t  g_departEndMs = 0;
uint32_t  g_forcedEndMs = 0;
uint32_t  g_lastFreshFeatured = 0;

bool airborne(const Aircraft &a) {
  return !a.onGround && a.altFt - OBS_ELEV_FT >= MIN_AGL_FT;
}

bool meetsEnter(const Aircraft &a) {
  return airborne(a) && a.seenPos <= MAX_SEEN_POS_S &&
         a.distNm <= ENTER_RADIUS_NM && a.elDeg >= ENTER_MIN_ELEV_DEG;
}

bool meetsStay(const Aircraft &a) {
  return airborne(a) && a.distNm <= EXIT_RADIUS_NM && a.elDeg >= EXIT_MIN_ELEV_DEG;
}

Tracked *find(const char *hex) {
  for (auto &t : g_trk)
    if (t.used && strcmp(t.a.hex, hex) == 0) return &t;
  return nullptr;
}

Tracked *freeSlot() {
  for (auto &t : g_trk)
    if (!t.used) return &t;
  return nullptr;
}

const Aircraft *inSnapshot(const Traffic &t, const char *hex) {
  for (uint8_t i = 0; i < t.n; i++)
    if (strcmp(t.ac[i].hex, hex) == 0) return &t.ac[i];
  return nullptr;
}

uint32_t freshMsOf(const Traffic &t, const Aircraft &a) {
  return t.fetchedMs - (uint32_t)(a.seenPos * 1000.0f);
}

// Candidates = tracked, not dismissed. Returns count; best = highest elevation.
int candidates(Tracked **best) {
  int n = 0;
  *best = nullptr;
  for (auto &t : g_trk) {
    if (!t.used || t.dismissed) continue;
    n++;
    if (!*best || t.a.elDeg > (*best)->a.elDeg) *best = &t;
  }
  return n;
}

void feature(const Tracked &t, bool manual) {
  strncpy(g_featured, t.a.hex, sizeof(g_featured));
  g_manual = manual;
}

void goLive(uint32_t now) {
  if (g_view.mode == PlaneMode::None || g_view.mode == PlaneMode::Forced) {
    g_planeSinceMs = now;
    g_arrival = true;
    Serial.printf("[trk] -> PLANE %s\n", g_featured);
  }
  g_view.mode = PlaneMode::Live;
}

void goWeather(const char *why) {
  if (g_view.mode != PlaneMode::None) Serial.printf("[trk] -> WEATHER (%s)\n", why);
  g_view.mode = PlaneMode::None;
  g_featured[0] = '\0';
  g_manual = false;
}

}  // namespace

void trackerUpdate(const Traffic &t, uint32_t now) {
  // 1. Refresh tracked aircraft from the snapshot; apply ENTER/EXIT hysteresis.
  for (uint8_t i = 0; i < t.n; i++) {
    const Aircraft &a = t.ac[i];
    Tracked *tr = find(a.hex);
    if (tr) {
      if (!meetsStay(a)) {
        Serial.printf("[trk] exit %s d=%.1fnm el=%.0f\n", a.hex, a.distNm, a.elDeg);
        tr->used = false;
        continue;
      }
      tr->a = a;
      if (a.seenPos <= MAX_SEEN_POS_S) tr->freshMs = freshMsOf(t, a);
    } else if (now - freshMsOf(t, a) <= MAX_SEEN_POS_S * 1000UL && meetsEnter(a)) {
      // (freshness vs *now*: a frozen snapshot must not re-enter planes it just lost)
      if ((tr = freeSlot())) {
        tr->a = a;
        tr->freshMs = freshMsOf(t, a);
        tr->dismissed = false;
        tr->used = true;
        Serial.printf("[trk] enter %s %s d=%.1fnm el=%.0f\n", a.hex, a.callsign, a.distNm, a.elDeg);
      }
    }
  }
  for (auto &tr : g_trk) {   // lost from the feed
    if (tr.used && now - tr.freshMs > LOST_TIMEOUT_S * 1000UL) {
      Serial.printf("[trk] lost %s\n", tr.a.hex);
      tr.used = false;
    }
  }

  // 2. Keep the display copy of the featured plane current (even when it no
  //    longer qualifies: Departing / Forced still show it).
  if (g_featured[0]) {
    if (const Aircraft *a = inSnapshot(t, g_featured)) {
      g_view.ac = *a;
      if (a->seenPos <= MAX_SEEN_POS_S) g_lastFreshFeatured = freshMsOf(t, *a);
    }
  }

  // 3. Choose what to show.
  Tracked *best;
  const int n = candidates(&best);
  Tracked *cur = g_featured[0] ? find(g_featured) : nullptr;
  if (cur && cur->dismissed) cur = nullptr;

  if (n > 0) {
    if (!cur) {
      feature(*best, false);
      cur = best;
      g_lastFreshFeatured = cur->freshMs;
    } else if (!g_manual && best != cur && best->a.elDeg >= cur->a.elDeg + FEATURE_SWITCH_DEG) {
      Serial.printf("[trk] feature %s -> %s\n", cur->a.hex, best->a.hex);
      feature(*best, false);
      cur = best;
      g_lastFreshFeatured = cur->freshMs;
    }
    g_view.ac = cur->a;
    goLive(now);
  } else {
    switch (g_view.mode) {
      case PlaneMode::Live: {
        const uint32_t dwellEnd = g_planeSinceMs + MIN_PLANE_DWELL_S * 1000UL;
        g_departEndMs = max<uint32_t>(now + DEPART_GRACE_S * 1000UL, dwellEnd);
        g_view.graceTotalS = (uint8_t)((g_departEndMs - now + 999) / 1000);
        g_view.mode = PlaneMode::Departing;
        Serial.printf("[trk] departing %s\n", g_featured);
        break;
      }
      case PlaneMode::Departing:
        if ((int32_t)(now - g_departEndMs) >= 0) goWeather("departed");
        break;
      case PlaneMode::Forced:
        if ((int32_t)(now - g_forcedEndMs) >= 0) goWeather("forced timeout");
        break;
      case PlaneMode::None:
        break;
    }
  }

  // 4. Derived view fields.
  const uint32_t age = now - g_lastFreshFeatured;
  g_view.ageS = (uint8_t)min<uint32_t>(age / 1000, 255);
  g_view.stale = g_view.mode == PlaneMode::Live && age > STALE_AFTER_S * 1000UL;
  g_view.graceLeftS = g_view.mode == PlaneMode::Departing
                          ? (uint8_t)(((int32_t)(g_departEndMs - now) + 999) / 1000) : 0;
  g_view.forcedLeftS = g_view.mode == PlaneMode::Forced
                           ? (uint8_t)(((int32_t)(g_forcedEndMs - now) + 999) / 1000) : 0;
  g_view.extra = n > 0 ? n - 1 : 0;
  g_view.nOthers = 0;
  for (auto &tr : g_trk) {
    if (!tr.used || tr.dismissed || strcmp(tr.a.hex, g_featured) == 0) continue;
    if (g_view.nOthers < 3) g_view.others[g_view.nOthers++] = tr.a;
  }
}

const TrackView &trackerView() { return g_view; }

bool trackerTakeArrival() {
  const bool a = g_arrival;
  g_arrival = false;
  return a;
}

void trackerTapNext() {
  if (g_view.mode != PlaneMode::Live || g_view.extra == 0) return;
  // Next candidate after the featured one, in slot order (wraps).
  int curIdx = -1;
  for (int i = 0; i < MAX_TRACKED; i++)
    if (g_trk[i].used && strcmp(g_trk[i].a.hex, g_featured) == 0) curIdx = i;
  for (int k = 1; k <= MAX_TRACKED; k++) {
    Tracked &t = g_trk[(curIdx + k + MAX_TRACKED) % MAX_TRACKED];
    if (t.used && !t.dismissed && strcmp(t.a.hex, g_featured) != 0) {
      feature(t, true);
      g_view.ac = t.a;
      g_lastFreshFeatured = t.freshMs;
      Serial.printf("[trk] manual pick %s\n", t.a.hex);
      return;
    }
  }
}

void trackerDismiss(uint32_t now) {
  if (Tracked *t = find(g_featured)) t->dismissed = true;
  Tracked *best;
  if (candidates(&best) > 0) {
    feature(*best, false);
    g_view.ac = best->a;
    g_lastFreshFeatured = best->freshMs;
  } else {
    goWeather("dismissed");
  }
}

namespace {
void force(const Traffic &t, const Aircraft &a, uint32_t now) {
  strncpy(g_featured, a.hex, sizeof(g_featured));
  g_manual = true;
  g_view.ac = a;
  g_lastFreshFeatured = freshMsOf(t, a);
  g_forcedEndMs = now + FORCED_SHOW_S * 1000UL;
  g_view.mode = PlaneMode::Forced;
  g_view.forcedLeftS = FORCED_SHOW_S;
  Serial.printf("[trk] forced %s\n", a.hex);
}
}  // namespace

bool trackerIsDismissed(const char *hex) {
  const Tracked *t = find(hex);
  return t && t->dismissed;
}

bool trackerForceNearest(const Traffic &t, uint32_t now) {
  for (uint8_t i = 0; i < t.n; i++) {
    if (!airborne(t.ac[i])) continue;
    force(t, t.ac[i], now);
    return true;
  }
  return false;
}

bool trackerForceHex(const Traffic &t, const char *hex, uint32_t now) {
  for (uint8_t i = 0; i < t.n; i++) {
    if (strcmp(t.ac[i].hex, hex) != 0 || t.ac[i].onGround) continue;
    force(t, t.ac[i], now);
    return true;
  }
  return false;
}
