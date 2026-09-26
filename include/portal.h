// portal.h - the setup portal boot (docs/12-setup-portal.md). A separate boot mode: the
// normal app (net task, TLS, sprites) never starts, so the hotspot + DNS + web server's RAM
// never competes with TLS. Never returns: it reboots on Save, Cancel or a timeout.
#pragma once

#include <TFT_eSPI.h>

[[noreturn]] void portalRun(TFT_eSPI &tft, bool automatic);
