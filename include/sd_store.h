// sd_store.h - the microSD card (v3, docs/10-rain-radar.md -> Hardware).
// SD on its own SPI bus (HSPI); the TFT keeps VSPI and touch is bit-banged.
// SdFat (not the core's SD.h/FATFS: ~15 KB mounted, which starved TLS - sd_store.cpp),
// mounted ONCE at boot and kept.
#pragma once

#include <Arduino.h>
#include <SdFat.h>

// Probe at boot: mount, write/read-back self-test (tries slower clocks). Stays mounted.
bool sdInit();
bool sdOk();              // a working card was found at boot
const char *sdStatus();   // "ok 20 MHz", "no card or not FAT32", "write failed" ... (on screen)

// "May I use the card": true when the boot probe passed.
inline bool sdAcquire() { return sdOk(); }
inline void sdRelease() {}

// The volume. SdFat is NOT thread-safe: use it only while holding sdLock().
SdFat32 &sdFs();

// The card-wide lock. The net task waits for it (waitMs = UINT32_MAX); the UI takes it with
// waitMs = 0 around its short reads and skips (retries next tick) when it is busy.
bool sdLock(uint32_t waitMs);
void sdUnlock();
