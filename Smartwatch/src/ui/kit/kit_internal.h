/*
 * kit_internal.h - What the kit's own files share: the styles and a few helpers.
 * Not for use outside src/ui/kit/.
 */
#pragma once

#include "kit.h"

#define KIT_BG     0x000000
#define KIT_TRACK  0x39393D

extern lv_style_t kit_st_screen, kit_st_header, kit_st_content, kit_st_scrollbar;
extern lv_style_t kit_st_card, kit_st_card_pressed, kit_st_disabled;
extern lv_style_t kit_st_title, kit_st_text, kit_st_sub, kit_st_section, kit_st_icon, kit_st_value;
extern lv_style_t kit_st_button, kit_st_button_pressed, kit_st_chip_on, kit_st_toast;

lv_obj_t *kit_styled_label(lv_obj_t *parent, lv_style_t *style, const char *text);
lv_obj_t *kit_card_base(lv_obj_t *parent);

void kit_nav_unblock();     // the next page may open while a transition runs (alerts)
bool kit_stack_full();
void kit_pop_now();         // close the top page even while a transition runs
void kit_alert_forget();    // the stack was dropped with an alert in it
