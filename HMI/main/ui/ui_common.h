#pragma once
#include "lvgl.h"

/* Welcome screen */
void ui_welcome_screen_create(void);
void ui_graph_screen_create(void);

/* Common UI elements */
extern lv_obj_t * ui_label_date;
extern lv_obj_t * ui_label_time;
extern lv_obj_t * ui_label_title;
extern lv_obj_t * ui_btn_back;

/* Common UI init */
void ui_common_create(lv_obj_t * parent);

typedef void (*ui_back_cb_t)(void);

void ui_common_set_back_cb(ui_back_cb_t cb);
void ui_common_load_screen(lv_obj_t *screen);

void ui_open_datetime_modal(void);
