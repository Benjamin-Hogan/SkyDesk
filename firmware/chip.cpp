#include "chip.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

void chipMessage(const ChipInputs &in, ChipWidthFn width, ChipMsg &out) {
  memset(&out, 0, sizeof(out));
  // 1. Traffic offline: status beats history (the nearby count would be a lie).
  if (!in.trafficUp) {
    out.kind = ChipKind::Offline;
    snprintf(out.left, sizeof(out.left), "Traffic offline");
    snprintf(out.right, sizeof(out.right), "retrying in %d s", (int)(in.retryS > 0 ? in.retryS : 0));
    return;
  }
  out.chevron = true;
  // 2. Passed overhead within TODAY_PASSED_S of its close.
  if (in.hasPass && in.passAgeS < TODAY_PASSED_S) {
    out.kind = ChipKind::Passed;
    const uint32_t mins = in.passAgeS / 60;
    if (mins < 1) snprintf(out.left, sizeof(out.left), "Passed just now");
    else snprintf(out.left, sizeof(out.left), "Passed %u min ago", (unsigned)mins);
    const int16_t avail = CHIP_RIGHT_X - (CHIP_LEFT_X + width(out.left) + CHIP_GAP);
    char full[48], coded[24] = "";
    if (in.passOp && in.passOp[0]) snprintf(full, sizeof(full), "%s %s", in.passOp, in.passType ? in.passType : "");
    else snprintf(full, sizeof(full), "%s", in.passType ? in.passType : "");
    if (in.passCode && in.passCode[0]) snprintf(coded, sizeof(coded), "%s %s", in.passCode, in.passType ? in.passType : "");
    const char *cands[] = {full, coded, in.passType, in.passIcao};   // the Today row's chain
    const char *pick = in.passIcao ? in.passIcao : "";
    for (const char *c : cands)
      if (c && c[0] && width(c) <= avail) {
        pick = c;
        break;
      }
    snprintf(out.right, sizeof(out.right), "%s", pick);
    snprintf(out.focusHex, sizeof(out.focusHex), "%s", in.passHex ? in.passHex : "");
    return;
  }
  // 3. Nearby / 4. quiet.
  snprintf(out.left, sizeof(out.left), "%u nearby", (unsigned)in.nNearby);
  if (in.nearestType) {
    out.kind = ChipKind::Nearby;
    snprintf(out.right, sizeof(out.right), "%s  %.1f mi %s", in.nearestType, (double)in.nearestMi,
             in.nearestDir ? in.nearestDir : "");
  } else {
    out.kind = ChipKind::Quiet;
    snprintf(out.right, sizeof(out.right), "quiet skies");
  }
}

bool headerEntryFit(uint16_t overhead, int16_t dateEndX, int16_t statusLeftX, ChipWidthFn width, char out[16]) {
  char num[8];
  if (overhead >= 10000) snprintf(num, sizeof(num), "%uk", (unsigned)(overhead / 1000));   // no "10.0k"
  else if (overhead >= 1000) snprintf(num, sizeof(num), "%u.%uk", (unsigned)(overhead / 1000), (unsigned)(overhead % 1000 / 100));
  else snprintf(num, sizeof(num), "%u", (unsigned)overhead);
  const int16_t x = dateEndX + HEADER_SEP;
  const int16_t limit = statusLeftX - (statusLeftX < SCREEN_W ? CHIP_GAP : 4);
  char full[16];
  snprintf(full, sizeof(full), "%s overhead", num);
  for (const char *c : {(const char *)full, (const char *)num}) {
    if (x + width(c) + HEADER_CHEV_W <= limit) {
      snprintf(out, 16, "%s", c);
      return true;
    }
  }
  out[0] = '\0';
  return false;
}
