#pragma once
#include "lvgl.h"

void ui_minute_screen_create(lv_obj_t *back_screen);
void ui_setting_time_screen_create(void);
void ui_set_active_setting_minutes(int mins);
int ui_get_active_setting_minutes(void);
