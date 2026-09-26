#include "sd_store.h"
#include "config.h"

#include <SPI.h>

// Why SdFat and not the core's SD.h (v3, measured - docs/10 -> Hardware and budget):
// the core's FATFS uses 4 KB sectors with a 4 KB cache PER allowed open file, allocated as
// one block at mount (6,248 + 4,136 x max_files bytes). Kept mounted it starved TLS
// (mbedTLS SSL_ALLOC_FAILED); mounted lazily, the one big block no longer fit a fragmented
// heap ("mount failed"). SdFat uses one shared 512 B cache and ~100 B per open file, so the
// card is mounted once at boot for ~1.5 KB.
//
// SdFat is not thread-safe: every access, from either core, happens under sdLock().
// The probe tries SD_FREQ_HZ, then slower clocks (CYD boards differ).

namespace {
SPIClass g_sdSpi(HSPI);
SdFat32 g_sd;
bool g_ok = false;
SemaphoreHandle_t g_lock = nullptr;
char g_why[32] = "not probed";

bool probe(uint32_t hz) {
  // SHARED_SPI, not DEDICATED_SPI: dedicated mode leaves the SPI transaction (and the
  // core's per-bus mutex) OPEN after a read, owned by the task that read. The next access
  // from the other core's task then releases a mutex it doesn't hold -> FreeRTOS assert
  // (xQueueGenericSend), a boot loop in 3.0. Shared mode ends every transaction.
  // USER_SPI_BEGIN: we start the bus with OUR pins on every attempt. After a failed
  // self-test g_sd.end() stops the bus, and SdFat's own begin() would restart HSPI on its
  // default pins 14/12/13/15 - the TFT's (review).
  g_sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!g_sd.begin(SdSpiConfig(SD_CS, SHARED_SPI | USER_SPI_BEGIN, hz, &g_sdSpi))) {
    snprintf(g_why, sizeof(g_why), "no card or not FAT32");
    return false;
  }
  g_sd.mkdir("/skydesk");
  const uint32_t stamp = esp_random();
  bool wrote = false, read = false;
  uint32_t back = 0;
  File32 f = g_sd.open("/skydesk/selftest.bin", O_WRONLY | O_CREAT | O_TRUNC);
  if (f) {
    wrote = f.write(&stamp, sizeof(stamp)) == sizeof(stamp);
    f.close();
  }
  f = g_sd.open("/skydesk/selftest.bin", O_RDONLY);
  if (f) {
    read = f.read(&back, sizeof(back)) == (int)sizeof(back);
    f.close();
  }
  const bool ok = wrote && read && back == stamp;
  if (!wrote) snprintf(g_why, sizeof(g_why), "write failed");
  else if (!read) snprintf(g_why, sizeof(g_why), "read failed");
  else if (!ok) snprintf(g_why, sizeof(g_why), "read-back mismatch");
  else snprintf(g_why, sizeof(g_why), "ok %lu MHz", (unsigned long)(hz / 1000000));
  Serial.printf("[sd] card %lu MB at %lu MHz: %s\n",
                (unsigned long)(g_sd.card()->sectorCount() / 2048), (unsigned long)(hz / 1000000), g_why);
  if (!ok) g_sd.end();
  return ok;
}
}  // namespace

bool sdInit() {
  g_lock = xSemaphoreCreateMutex();
  for (uint32_t hz : {(uint32_t)SD_FREQ_HZ, 4000000u, 1000000u}) {
    if (probe(hz)) {
      g_ok = true;
      return true;                             // stays mounted for good
    }
    if (strcmp(g_why, "no card or not FAT32") == 0) break;   // a slower clock won't find a card
  }
  Serial.printf("[sd] disabled: %s\n", g_why);
  return false;
}

bool sdOk() { return g_ok; }
const char *sdStatus() { return g_why; }
SdFat32 &sdFs() { return g_sd; }

bool sdLock(uint32_t waitMs) {
  return g_lock && xSemaphoreTake(g_lock, waitMs == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
void sdUnlock() {
  // Belt and braces: never leave the card mid-read/write when another task may be next.
  if (g_ok) g_sd.card()->syncDevice();
  if (g_lock) xSemaphoreGive(g_lock);
}
