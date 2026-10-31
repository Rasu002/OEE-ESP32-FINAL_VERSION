#include "lvgl.h"
#include "ui_common.h"

/* Back destination */
void ui_auto_screen_init(void);

/* ================= COLORS ================= */
#define BLUE_LABEL   lv_color_hex(0x0E6BB8)
#define BOX_SET      lv_color_hex(0xFFFFFF)
#define BOX_ACTUAL   lv_color_hex(0x10D060)
#define COLOR_START  lv_color_hex(0x22C55E)
#define COLOR_STOP   lv_color_hex(0xEF4444)
#define COLOR_PAUSE  lv_color_hex(0xFACC15)

/* ================= PAUSE STATE ================= */

static lv_obj_t * btn_pause;
static lv_obj_t * lbl_pause;
static bool paused = false;

/* ================= HELPERS ================= */

static lv_obj_t * create_box(lv_obj_t * parent, lv_color_t bg, int x, int y)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, 120, 36);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, bg, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(box);
    lv_label_set_text(lbl, "--");
    lv_obj_center(lbl);

    return box;
}

static void set_button_shadow(lv_obj_t * btn, bool enable)
{
    if(enable) {
        lv_obj_set_style_shadow_width(btn, 12, 0);
        lv_obj_set_style_shadow_spread(btn, 2, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_black(), 0);
    } else {
        lv_obj_set_style_shadow_width(btn, 0, 0);
    }
}

/* ================= EVENTS ================= */

static void event_pause_toggle(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    paused = !paused;
    lv_label_set_text(lbl_pause, paused ? "RESUME" : "PAUSE");
    set_button_shadow(btn_pause, paused);
}

/* ================= BACK ================= */

static void back_to_auto(void)
{
    ui_auto_screen_init();
}

    static lv_obj_t * create_small_display(lv_obj_t * parent, const char * text, lv_color_t bg_color){
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, 80, 30);          /* small display size */
    lv_obj_set_style_bg_color(box, bg_color, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xD1D5DB), 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * lbl = lv_label_create(box);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return box;}

/* ================= SCREEN ================= */

void ui_dynamic_screen_init(uint8_t program_id)
{
    LV_UNUSED(program_id);

    /* ================= SCREEN (SCROLLABLE) ================= */

    lv_obj_t * scr = lv_obj_create(NULL);
    ui_common_load_screen(scr);

    /* WHOLE SCREEN SCROLLS */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_AUTO);

    ui_common_create(scr);
    ui_common_set_back_cb(back_to_auto);
    lv_label_set_text(ui_label_title, "Dynamic Impulse");

    /* ================= HEADERS ================= */

    lv_obj_t * lbl_model = lv_label_create(scr);
    lv_label_set_text(lbl_model, "Model");
    lv_obj_set_pos(lbl_model, 60, 120);

    lv_obj_t * lbl_set = lv_label_create(scr);
    lv_label_set_text(lbl_set, "Set");
    // lv_obj_set_style_text_color(lbl_set, BLUE_LABEL, 0);
    lv_obj_set_pos(lbl_set, 360, 120);

    lv_obj_t * lbl_act = lv_label_create(scr);
    lv_label_set_text(lbl_act, "Actual");
    // lv_obj_set_style_text_color(lbl_act, BLUE_LABEL, 0);
    lv_obj_set_pos(lbl_act, 520, 120);

    /* ================= MODEL BOX ================= */

    create_box(scr, BOX_SET, 120, 115);

    /* ================= PARAMETERS ================= */

    const char * names[] = {
        "Impulse Frequency (Hz)",
        "Amplitude (mm)",
        "No of Cycles",
        "Test Duration (min)"
    };

    int base_y = 175;
    int step   = 60;

    for(int i = 0; i < 4; i++) {
        lv_obj_t * lbl = lv_label_create(scr);
        lv_label_set_text(lbl, names[i]);
        lv_obj_set_style_text_color(lbl, BLUE_LABEL, 0);
        lv_obj_set_pos(lbl, 60, base_y + i * step);

        create_box(scr, BOX_SET,   340, base_y + i * step - 10);
        create_box(scr, BOX_ACTUAL,500, base_y + i * step - 10);

            if(i == 0) {
            lv_obj_t * fm = lv_obj_create(scr);
            lv_obj_set_size(fm, 80, 36);
            lv_obj_set_pos(fm, 680, base_y - 10);

            lv_obj_set_style_bg_color(fm, lv_color_hex(0xE5E7EB), 0);
            lv_obj_set_style_radius(fm, 6, 0);
            lv_obj_set_style_border_width(fm, 1, 0);
            lv_obj_set_style_border_color(fm, lv_color_hex(0xD1D5DB), 0);
            lv_obj_clear_flag(fm, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(fm, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t * lbl_fm = lv_label_create(fm);
            lv_label_set_text(lbl_fm, "F.Multi");
            lv_obj_center(lbl_fm);

            /* Small display below F.Multi */
            lv_obj_t * fm_val = create_small_display(scr, "1.00", BOX_SET);
            lv_obj_set_pos(fm_val, 680, base_y + 30);
        }

    }

    /* ================= CONTROL BUTTONS ================= */

    lv_obj_t * btn_start = lv_btn_create(scr);
    lv_obj_set_size(btn_start, 160, 60);
    lv_obj_set_pos(btn_start, 70, 400);
    lv_obj_set_style_bg_color(btn_start, COLOR_START, 0);
    lv_label_set_text(lv_label_create(btn_start), "START");
    lv_obj_center(lv_obj_get_child(btn_start, 0));

    lv_obj_t * btn_stop = lv_btn_create(scr);
    lv_obj_set_size(btn_stop, 160, 60);
    lv_obj_set_pos(btn_stop, 290, 400);
    lv_obj_set_style_bg_color(btn_stop, COLOR_STOP, 0);
    lv_label_set_text(lv_label_create(btn_stop), "STOP");
    lv_obj_center(lv_obj_get_child(btn_stop, 0));

    btn_pause = lv_btn_create(scr);
    lv_obj_set_size(btn_pause, 160, 60);
    lv_obj_set_pos(btn_pause, 510, 400);
    lv_obj_set_style_bg_color(btn_pause, COLOR_PAUSE, 0);
    lv_obj_add_event_cb(btn_pause, event_pause_toggle, LV_EVENT_CLICKED, NULL);

    lbl_pause = lv_label_create(btn_pause);
    lv_label_set_text(lbl_pause, "PAUSE");
    lv_obj_center(lbl_pause);

    /* ================= MESSAGE BOX (BELOW BUTTONS) ================= */

    lv_coord_t scr_w = lv_disp_get_hor_res(NULL);

    lv_obj_t * msg = lv_obj_create(scr);
    lv_obj_set_size(msg, scr_w - 120, 60);
    lv_obj_set_pos(msg, 60, 500);
    lv_obj_set_style_bg_color(msg, lv_color_hex(0xA7F3D0), 0);
    lv_obj_set_style_radius(msg, 10, 0);

    lv_label_set_text(lv_label_create(msg), "Ready to Start");
    lv_obj_center(lv_obj_get_child(msg, 0));
}
