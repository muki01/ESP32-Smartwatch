/*
 * nav.h - System navigation (Wear OS style) and the BOOT button.
 *
 *              quick panel (swipe down)
 *                     |
 *  notifications <- WATCH FACE -> activity -> weather -> music      (tiles, swipe left)
 *   (swipe right)     |
 *               app launcher (swipe up)          long press: watch face picker
 *
 * Everywhere: swipe right = back, BOOT button = home (on the watch face: apps).
 */
#pragma once

#include <lvgl.h>
#include "../kit/kit.h"

enum TileId : int32_t { TILE_ACTIVITY, TILE_WEATHER, TILE_MUSIC, TILE_COUNT };

void nav_init();                         // tiles, the chosen watch face, the BOOT button
void nav_go_home(bool animate);
bool nav_is_home();
lv_obj_t *nav_home_screen();
lv_obj_t *nav_face_screen(int face);
void nav_show_tile(int tile);
void nav_open_launcher();
void nav_open_quick_panel();
void nav_open_face_picker();
void nav_add_page_dots(lv_obj_t *screen, int index);
void nav_make_complication(lv_obj_t *obj, app_open_fn_t open);  // tap opens an app, long press still reaches the face
