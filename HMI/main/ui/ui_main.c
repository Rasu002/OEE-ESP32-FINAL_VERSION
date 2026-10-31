#include "lvgl.h"
#include "ui_common.h"
void ui_welcome_screen_create(void);

/* External screen init functions */
void ui_auto_screen_init(void);
void ui_manual_screen_init(void);
void ui_edit_program_screen_init(void);

static bool s_main_nav_in_progress = false;

static void back_to_welcome(void)
{
    ui_welcome_screen_create();
}

/* =======================
 * Color scheme
 * =======================*/
#define COLOR_BTN_AUTO     lv_color_hex(0x2E8B57)
#define COLOR_BTN_MANUAL   lv_color_hex(0x1E5AA8)
#define COLOR_BTN_EDIT     lv_color_hex(0xA8741E)

static void open_auto_async(void *ctx)
{
    LV_UNUSED(ctx);
    ui_auto_screen_init();
    s_main_nav_in_progress = false;
}

static void open_manual_async(void *ctx)
{
    LV_UNUSED(ctx);
    ui_manual_screen_init();
    s_main_nav_in_progress = false;
}

static void open_edit_async(void *ctx)
{
    LV_UNUSED(ctx);
    ui_edit_program_screen_init();
    s_main_nav_in_progress = false;
}

/* =======================
 * Event handlers
 * =======================*/
static void event_auto(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_main_nav_in_progress) return;
    s_main_nav_in_progress = true;
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);
    lv_async_call(open_auto_async, NULL);
}

static void event_manual(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_main_nav_in_progress) return;
    s_main_nav_in_progress = true;
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);
    lv_async_call(open_manual_async, NULL);
}

static void event_edit(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_main_nav_in_progress) return;
    s_main_nav_in_progress = true;
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);
    lv_async_call(open_edit_async, NULL);
}

/* =======================
 * MAIN menu screen
 * =======================*/
void ui_menu_screen_create(void)
{
    s_main_nav_in_progress = false;

    lv_obj_t * screen = lv_obj_create(NULL);
    ui_common_load_screen(screen);

    /* Common background + time/date */
    ui_common_create(screen);
    ui_common_set_back_cb(back_to_welcome);

    /* Header positions */
    lv_obj_align(ui_btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_align(ui_label_date, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_align(ui_label_time, LV_ALIGN_TOP_RIGHT, -20, 30);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_RIGHT, -20, 52);

    /* -------- Page title -------- */
    lv_obj_t * label_main = lv_label_create(screen);
    lv_label_set_text(label_main, "MAIN");
    lv_obj_align(label_main, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_text_font(label_main, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_main, lv_color_hex(0x1F2A33), 0);

    /* -------- Auto Button container -------- */
    lv_obj_t * btn_auto = lv_btn_create(screen);
    lv_obj_set_size(btn_auto, 220, 70);
    lv_obj_align(btn_auto, LV_ALIGN_CENTER, -160, -10);
    lv_obj_set_style_bg_color(btn_auto, COLOR_BTN_AUTO, 0);
    lv_obj_set_style_radius(btn_auto, 12, 0);
    lv_obj_add_event_cb(btn_auto, event_auto, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_auto = lv_label_create(btn_auto);
    lv_label_set_text(lbl_auto, "AUTO");
    lv_obj_center(lbl_auto);

    /* MANUAL button */
    lv_obj_t * btn_manual = lv_btn_create(screen);
    lv_obj_set_size(btn_manual, 220, 70);
    lv_obj_align(btn_manual, LV_ALIGN_CENTER, 160, -10);
    lv_obj_set_style_bg_color(btn_manual, COLOR_BTN_MANUAL, 0);
    lv_obj_set_style_radius(btn_manual, 12, 0);
    lv_obj_add_event_cb(btn_manual, event_manual, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_manual = lv_label_create(btn_manual);
    lv_label_set_text(lbl_manual, "MANUAL");
    lv_obj_center(lbl_manual);

    /* EDIT PROGRAM */
    lv_obj_t * btn_edit = lv_btn_create(screen);
    lv_obj_set_size(btn_edit, 240, 80);
    lv_obj_align(btn_edit, LV_ALIGN_BOTTOM_LEFT, 40, -40);
    lv_obj_set_style_bg_color(btn_edit, COLOR_BTN_EDIT, 0);
    lv_obj_set_style_radius(btn_edit, 14, 0);
    lv_obj_add_event_cb(btn_edit, event_edit, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_edit = lv_label_create(btn_edit);
    lv_label_set_text(lbl_edit, "Edit Program");
    lv_obj_center(lbl_edit);
}
