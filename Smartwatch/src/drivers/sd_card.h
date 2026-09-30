/*
 * sd_card.h - microSD card over SDMMC (1-bit). Optional: without a card the watch works
 * normally; only the music player and the recorder have nothing to show.
 */
#pragma once

#include <stdint.h>

bool sd_init();
bool sd_available();
uint64_t sd_total_bytes();
uint64_t sd_used_bytes();

// The files (not folders) of a folder, in directory order. Returns how many were listed,
// -1 without a card or folder. Paths start at the card's root ("/recordings").
typedef void (*sd_file_cb_t)(const char *name, uint32_t size, void *arg);
int sd_list(const char *dir, sd_file_cb_t fn, void *arg);
bool sd_mkdir(const char *path);   // true when it exists afterwards
bool sd_remove(const char *path);
