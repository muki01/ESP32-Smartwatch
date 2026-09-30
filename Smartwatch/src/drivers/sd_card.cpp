/*
 * sd_card.cpp - microSD over SDMMC (1-bit), mounted at /sdcard.
 */
#include "sd_card.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include "../core/board.h"

static bool sd_ok;

bool sd_init() {
  SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
  // 40 MHz high-speed mode; the driver falls back to 20 MHz for cards that lack it.
  sd_ok = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_HIGHSPEED, 3) && SD_MMC.cardType() != CARD_NONE;
  if (!sd_ok) {
    Serial.println("[SD] no card");
    return false;
  }
  Serial.printf("[SD] %s, %llu MB\n", SD_MMC.cardType() == CARD_MMC ? "MMC" : SD_MMC.cardType() == CARD_SD ? "SDSC" : "SDHC/SDXC",
                SD_MMC.cardSize() / (1024 * 1024));
  return true;
}

bool sd_available() {
  return sd_ok;
}

uint64_t sd_total_bytes() {
  return sd_ok ? SD_MMC.totalBytes() : 0;
}

uint64_t sd_used_bytes() {
  return sd_ok ? SD_MMC.usedBytes() : 0;
}

int sd_list(const char *dir, sd_file_cb_t fn, void *arg) {
  if (!sd_ok) return -1;
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return -1;
  int n = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) {
      fn(f.name(), f.size(), arg);
      n++;
    }
    f.close();
  }
  d.close();
  return n;
}

bool sd_mkdir(const char *path) {
  return sd_ok && (SD_MMC.exists(path) || SD_MMC.mkdir(path));
}

bool sd_remove(const char *path) {
  return sd_ok && SD_MMC.remove(path);
}
