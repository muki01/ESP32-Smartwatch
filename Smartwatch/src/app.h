/*
 * app.h - The firmware as a whole: start-up in dependency order and the main loop.
 * Smartwatch.ino only calls these two functions.
 */
#pragma once

void app_init();
void app_loop();
