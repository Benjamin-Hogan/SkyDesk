#include "aircraft_names.h"

#include <ctype.h>

namespace {

struct TypeRow { const char *icao, *mfr, *model; };

// Common types over the US Southwest. Short models read well at fsb12.
const TypeRow TYPES[] = {
  {"A19N", "Airbus", "A319neo"}, {"A20N", "Airbus", "A320neo"}, {"A21N", "Airbus", "A321neo"},
  {"A318", "Airbus", "A318"}, {"A319", "Airbus", "A319"}, {"A320", "Airbus", "A320"},
  {"A321", "Airbus", "A321"}, {"A332", "Airbus", "A330-200"}, {"A333", "Airbus", "A330-300"},
  {"A339", "Airbus", "A330-900"}, {"A359", "Airbus", "A350-900"}, {"A35K", "Airbus", "A350-1000"},
  {"A388", "Airbus", "A380"}, {"BCS1", "Airbus", "A220-100"}, {"BCS3", "Airbus", "A220-300"},
  {"B712", "Boeing", "717"}, {"B733", "Boeing", "737-300"}, {"B737", "Boeing", "737-700"},
  {"B738", "Boeing", "737-800"}, {"B739", "Boeing", "737-900"}, {"B37M", "Boeing", "737 MAX 7"},
  {"B38M", "Boeing", "737 MAX 8"}, {"B39M", "Boeing", "737 MAX 9"}, {"B3XM", "Boeing", "737 MAX 10"},
  {"B752", "Boeing", "757-200"}, {"B753", "Boeing", "757-300"}, {"B762", "Boeing", "767-200"},
  {"B763", "Boeing", "767-300"}, {"B764", "Boeing", "767-400"}, {"B772", "Boeing", "777-200"},
  {"B77L", "Boeing", "777-200LR"}, {"B77W", "Boeing", "777-300ER"}, {"B788", "Boeing", "787-8"},
  {"B789", "Boeing", "787-9"}, {"B78X", "Boeing", "787-10"}, {"B744", "Boeing", "747-400"},
  {"B748", "Boeing", "747-8"}, {"MD11", "Boeing", "MD-11"}, {"MD88", "Boeing", "MD-88"},
  {"CRJ2", "Bombardier", "CRJ-200"}, {"CRJ7", "Bombardier", "CRJ-700"}, {"CRJ9", "Bombardier", "CRJ-900"},
  {"CL30", "Bombardier", "Challenger 300"}, {"CL35", "Bombardier", "Challenger 350"},
  {"CL60", "Bombardier", "Challenger 600"}, {"GLEX", "Bombardier", "Global Express"},
  {"GL7T", "Bombardier", "Global 7500"}, {"DH8D", "De Havilland", "Dash 8-400"},
  {"E135", "Embraer", "ERJ-135"}, {"E145", "Embraer", "ERJ-145"}, {"E170", "Embraer", "E170"},
  {"E75L", "Embraer", "E175"}, {"E75S", "Embraer", "E175"}, {"E190", "Embraer", "E190"},
  {"E195", "Embraer", "E195"}, {"E290", "Embraer", "E190-E2"}, {"E55P", "Embraer", "Phenom 300"},
  {"E50P", "Embraer", "Phenom 100"}, {"E545", "Embraer", "Legacy 450"},
  {"C172", "Cessna", "172"}, {"C152", "Cessna", "152"}, {"C150", "Cessna", "150"},
  {"C182", "Cessna", "182"}, {"C206", "Cessna", "206"}, {"C208", "Cessna", "Caravan"},
  {"C210", "Cessna", "210"}, {"C310", "Cessna", "310"}, {"C340", "Cessna", "340"},
  {"C414", "Cessna", "414"}, {"C421", "Cessna", "421"}, {"C510", "Cessna", "Citation Mustang"},
  {"C525", "Cessna", "CitationJet"}, {"C25A", "Cessna", "Citation CJ2"}, {"C25B", "Cessna", "Citation CJ3"},
  {"C25C", "Cessna", "Citation CJ4"}, {"C560", "Cessna", "Citation V"}, {"C56X", "Cessna", "Citation Excel"},
  {"C68A", "Cessna", "Citation Latitude"}, {"C700", "Cessna", "Citation Longitude"},
  {"C750", "Cessna", "Citation X"}, {"P28A", "Piper", "PA-28"}, {"P28B", "Piper", "PA-28"},
  {"P28R", "Piper", "Arrow"}, {"PA32", "Piper", "PA-32"}, {"PA34", "Piper", "Seneca"},
  {"PA44", "Piper", "Seminole"}, {"PA46", "Piper", "Malibu"}, {"P46T", "Piper", "Meridian"},
  {"SR20", "Cirrus", "SR20"}, {"SR22", "Cirrus", "SR22"}, {"S22T", "Cirrus", "SR22T"},
  {"SF50", "Cirrus", "Vision Jet"}, {"BE20", "Beechcraft", "King Air 200"},
  {"BE35", "Beechcraft", "Bonanza"}, {"BE36", "Beechcraft", "Bonanza"}, {"BE58", "Beechcraft", "Baron"},
  {"BE9L", "Beechcraft", "King Air 90"}, {"B350", "Beechcraft", "King Air 350"},
  {"DA40", "Diamond", "DA40"}, {"DA42", "Diamond", "DA42"}, {"DA62", "Diamond", "DA62"},
  {"DA20", "Diamond", "DA20"}, {"M20P", "Mooney", "M20"}, {"M20T", "Mooney", "M20"},
  {"PC12", "Pilatus", "PC-12"}, {"PC24", "Pilatus", "PC-24"}, {"TBM7", "Daher", "TBM 700"},
  {"TBM9", "Daher", "TBM 900"}, {"GLF4", "Gulfstream", "G450"}, {"GLF5", "Gulfstream", "G550"},
  {"GLF6", "Gulfstream", "G650"}, {"G280", "Gulfstream", "G280"}, {"LJ45", "Learjet", "45"},
  {"LJ60", "Learjet", "60"}, {"HDJT", "Honda", "HondaJet"}, {"FA7X", "Dassault", "Falcon 7X"},
  {"F2TH", "Dassault", "Falcon 2000"}, {"F900", "Dassault", "Falcon 900"},
  {"C130", "Lockheed", "C-130"}, {"C17", "Boeing", "C-17"}, {"K35R", "Boeing", "KC-135"},
  {"F16", "Lockheed", "F-16"}, {"F35", "Lockheed", "F-35"}, {"A10", "Fairchild", "A-10"},
  {"V22", "Bell-Boeing", "V-22 Osprey"}, {"H60", "Sikorsky", "Black Hawk"},
  {"EC35", "Airbus", "H135"}, {"EC45", "Airbus", "H145"}, {"EC30", "Airbus", "H130"},
  {"AS50", "Airbus", "H125"}, {"B06", "Bell", "206"}, {"B407", "Bell", "407"},
  {"B429", "Bell", "429"}, {"R44", "Robinson", "R44"}, {"R22", "Robinson", "R22"},
  {"R66", "Robinson", "R66"}, {"A139", "Leonardo", "AW139"}, {"A109", "Leonardo", "AW109"},
  {"H500", "MD", "500"}, {"EC20", "Airbus", "H120"}, {"B505", "Bell", "505"}, {"C82R", "Cessna", "182RG"},
};

// ICAO airline prefix -> short name, used when adsbdb doesn't name the operator
// (adsbdb is only queried within 6 nm). adsb.fi's ownOp is not kept (3.0: RAM, and it
// names private owners).
struct AirlineRow { const char *icao, *name; };
const AirlineRow AIRLINES[] = {
  {"SWA", "Southwest"}, {"AAL", "American"}, {"UAL", "United"}, {"DAL", "Delta"},
  {"ASA", "Alaska"}, {"FFT", "Frontier"}, {"NKS", "Spirit"}, {"JBU", "JetBlue"},
  {"SKW", "SkyWest"}, {"ENY", "Envoy"}, {"ASH", "Mesa"}, {"RPA", "Republic"},
  {"WJA", "WestJet"}, {"ACA", "Air Canada"}, {"AMX", "Aeromexico"}, {"VOI", "Volaris"},
  {"BAW", "British"}, {"FDX", "FedEx"}, {"UPS", "UPS"}, {"AAY", "Allegiant"},
  {"SCX", "Sun Country"}, {"HAL", "Hawaiian"}, {"QXE", "Horizon"}, {"EJA", "NetJets"},
};

const char *const SUFFIXES[] = {" AIRLINES", " AIR LINES", " AIRWAYS", " AIR LINE", " INC", " CO",
                                " CORP", " CORPORATION", " LLC", " LTD", " TRUSTEE", ","};

void copyStr(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}

// "SWA1637" -> true (3 letters + digit)
bool looksLikeAirlineCallsign(const char *cs) {
  return strlen(cs) >= 4 && isalpha(cs[0]) && isalpha(cs[1]) && isalpha(cs[2]) && isdigit(cs[3]);
}

}  // namespace

bool airlineByCallsign(const char *cs, char *out, size_t n) {
  out[0] = '\0';
  if (!cs || !looksLikeAirlineCallsign(cs)) return false;
  for (const auto &al : AIRLINES)
    if (strncmp(cs, al.icao, 3) == 0) {
      copyStr(out, n, al.name);
      return true;
    }
  return false;
}

namespace {

// "WN1637" -> "WN 1637"
void spaceFlight(const char *in, char *out, size_t n) {
  size_t i = 0;
  while (in[i] && !(isdigit(in[i]) && i >= 2)) i++;
  if (in[i] && i < n - 2) snprintf(out, n, "%.*s %s", (int)i, in, in + i);
  else copyStr(out, n, in);
}

void titleCase(char *s) {
  bool start = true;
  for (; *s; ++s) {
    if (isalpha(*s)) {
      *s = start ? toupper(*s) : tolower(*s);
      start = false;
    } else {
      start = (*s == ' ' || *s == '-' || *s == '/');
    }
  }
}

}  // namespace

bool typeLookup(const char *icao, const char *&mfr, const char *&model) {
  if (!icao || !icao[0]) return false;
  for (const auto &r : TYPES) {
    if (strcmp(r.icao, icao) == 0) {
      mfr = r.mfr;
      model = r.model;
      return true;
    }
  }
  return false;
}

void shortOperator(const char *in, char *out, size_t n) {
  char buf[48];
  copyStr(buf, sizeof(buf), in);
  for (char *p = buf; *p; ++p) *p = toupper(*p);
  bool changed = true;
  while (changed) {                     // strip suffixes repeatedly ("... AIRLINES CO")
    changed = false;
    for (const char *suf : SUFFIXES) {
      const size_t L = strlen(buf), S = strlen(suf);
      if (L > S && strcmp(buf + L - S, suf) == 0) {
        buf[L - S] = '\0';
        changed = true;
      }
    }
  }
  titleCase(buf);
  copyStr(out, n, buf);
}

void planeLabels(const Aircraft &a, const RouteInfo *r, PlaneLabels &o) {
  memset(&o, 0, sizeof(o));

  // --- type ---
  const char *mfr = nullptr, *model = nullptr;
  const char *icao = a.type[0] ? a.type : (r && r->icaoType[0] ? r->icaoType : "");
  const bool known = typeLookup(icao, mfr, model);

  // --- operator ---
  const bool airlineCs = looksLikeAirlineCallsign(a.callsign);
  if (r && r->hasRoute && r->airline[0]) {
    shortOperator(r->airline, o.op, sizeof(o.op));
  } else if (airlineCs && r && r->owner[0]) {
    shortOperator(r->owner, o.op, sizeof(o.op));
  } else if (airlineCs) {
    for (const auto &al : AIRLINES)
      if (strncmp(a.callsign, al.icao, 3) == 0) { copyStr(o.op, sizeof(o.op), al.name); break; }
  }
  o.airline = o.op[0] != '\0';

  if (known) {
    if (o.airline) copyStr(o.type, sizeof(o.type), model);
    else snprintf(o.type, sizeof(o.type), "%s %s", mfr, model);
  } else if (a.desc[0]) {
    copyStr(o.type, sizeof(o.type), a.desc);
    titleCase(o.type);
  } else if (icao[0]) {
    copyStr(o.type, sizeof(o.type), icao);
  } else {
    copyStr(o.type, sizeof(o.type), "Unknown type");
  }

  // --- line 2 ---
  if (o.airline) {
    if (r && r->flightIata[0]) spaceFlight(r->flightIata, o.line2a, sizeof(o.line2a));
    else copyStr(o.line2a, sizeof(o.line2a), a.callsign);
    copyStr(o.line2b, sizeof(o.line2b), a.reg);
  } else {
    copyStr(o.line2a, sizeof(o.line2a), a.reg[0] ? a.reg : (a.callsign[0] ? a.callsign : a.hex));
    // Never show a GA owner's name: registries list private individuals.
    copyStr(o.line2b, sizeof(o.line2b), "Private");
  }
}
