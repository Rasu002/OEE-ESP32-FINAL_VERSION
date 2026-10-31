#include "lvgl.h"
#include "ui_common.h"

/* Forward declaration */
void ui_menu_screen_create(void);
void ui_graph_screen_create(void);

static void open_servo_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_menu_screen_create();
}

static void open_oee_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_graph_screen_create();
}

void ui_welcome_screen_create(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    ui_common_load_screen(scr);

    ui_common_create(scr);

    /* Hide BACK button on welcome screen */
    lv_obj_add_flag(ui_btn_back, LV_OBJ_FLAG_HIDDEN);

    /* Set page title */
    lv_label_set_text(ui_label_title, "WELCOME");

    /* Center content */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "Control Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1F2A33), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -90);

    lv_obj_t * servo_btn = lv_btn_create(scr);
    lv_obj_set_size(servo_btn, 220, 70);
    lv_obj_align(servo_btn, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(servo_btn, lv_color_hex(0x2E8B57), 0);
    lv_obj_add_event_cb(servo_btn, open_servo_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * servo_lbl = lv_label_create(servo_btn);
    lv_label_set_text(servo_lbl, "Servo Actuator");
    lv_obj_center(servo_lbl);

    lv_obj_t * oee_btn = lv_btn_create(scr);
    lv_obj_set_size(oee_btn, 220, 70);
    lv_obj_align(oee_btn, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_style_bg_color(oee_btn, lv_color_hex(0x1E5AA8), 0);
    lv_obj_add_event_cb(oee_btn, open_oee_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * oee_lbl = lv_label_create(oee_btn);
    lv_label_set_text(oee_lbl, "OEE Bar Graph");
    lv_obj_center(oee_lbl);
}

