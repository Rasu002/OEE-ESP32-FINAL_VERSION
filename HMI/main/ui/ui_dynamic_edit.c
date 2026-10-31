#include "lvgl.h"
#include "ui_common.h"

/* Back destination */
void ui_edit_program_screen_init(void);
static lv_obj_t * kb = NULL;
static char ta_backup[16];

static lv_obj_t * popup = NULL;
static lv_obj_t * popup_label = NULL;

/* =======================
 * Colors
 * =======================*/
#define COLOR_LABEL   lv_color_hex(0x0E6BB8)
#define COLOR_BOX     lv_color_hex(0xFFFFFF)
#define COLOR_SAVE    lv_color_hex(0x22C55E)
#define COLOR_TEXT    lv_color_hex(0x1F2A33)


static void popup_create(void)
{
    if(popup) return;

    popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(popup, 760, 70);
    lv_obj_align(popup, LV_ALIGN_BOTTOM_MID, 0, -210);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_radius(popup, 10, 0);
    lv_obj_set_style_shadow_width(popup, 20, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    popup_label = lv_label_create(popup);
    lv_label_set_text(popup_label, "");
    lv_obj_set_style_text_color(popup_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(
        popup_label,
        lv_theme_get_font_large(popup_label),
        0
    );
    lv_obj_center(popup_label);
}



static void popup_delete(void)
{
    if(popup) {
        lv_obj_del(popup);
        popup = NULL;
        popup_label = NULL;
    }
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);

    if(code == LV_EVENT_FOCUSED) {

        strncpy(ta_backup, lv_textarea_get_text(ta), sizeof(ta_backup) - 1);
        ta_backup[sizeof(ta_backup) - 1] = '\0';

        popup_create();
        lv_label_set_text(popup_label, ta_backup);

        if(kb == NULL) {
            kb = lv_keyboard_create(lv_scr_act());
            lv_obj_set_size(kb, 760, 200);
            lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
        }

        lv_keyboard_set_textarea(kb, ta);
    }

    else if(code == LV_EVENT_VALUE_CHANGED) {
        if(popup_label) {
            lv_label_set_text(popup_label, lv_textarea_get_text(ta));
        }
    }

    else if(code == LV_EVENT_READY) {

        popup_delete();

        if(kb) {
            lv_obj_del(kb);
            kb = NULL;
        }
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }

    else if(code == LV_EVENT_CANCEL) {

        lv_textarea_set_text(ta, ta_backup);

        popup_delete();

        if(kb) {
            lv_obj_del(kb);
            kb = NULL;
        }
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}


/* =======================
 * PLC DATA (EDITABLE)
 * =======================*/
typedef struct {
    float freq;
    float amp;
    float fmulti;
    int   cycles;
    float duration;
} plc_edit_t;

static plc_edit_t plc = {
    .freq     = 20.0f,
    .amp      = 1.50f,
    .fmulti   = 1.00f,
    .cycles   = 100,
    .duration = 5.0f
};

/* =======================
 * Helpers (input creators)
 * =======================*/
static lv_obj_t * create_input(lv_obj_t * parent, const char * init)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 160, 36);
    lv_obj_set_style_bg_color(ta, COLOR_BOX, 0);
    lv_obj_set_style_radius(ta, 6, 0);

    lv_textarea_set_text(ta, init);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    lv_textarea_set_max_length(ta, 10);

    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_text_color(ta, COLOR_TEXT, 0);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);

    return ta;
}

static lv_obj_t * create_input_int(lv_obj_t * parent, const char * init)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 160, 36);
    lv_obj_set_style_bg_color(ta, COLOR_BOX, 0);
    lv_obj_set_style_radius(ta, 6, 0);

    lv_textarea_set_text(ta, init);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_max_length(ta, 10);

    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_text_color(ta, COLOR_TEXT, 0);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);

    return ta;
}

/* Small static box (non-editable) used for min/max and tiny value boxes */
static lv_obj_t * create_small_box(lv_obj_t * parent, const char * init)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 60, 36);

    /* Same behavior as other inputs */
    lv_textarea_set_text(ta, init);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    lv_textarea_set_max_length(ta, 6);

    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);

    /* Same visual style */
    lv_obj_set_style_bg_color(ta, COLOR_BOX, 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xD1D5DB), 0);

    /* Focus style */
    lv_obj_set_style_border_color(ta, COLOR_LABEL, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);

    lv_obj_set_style_text_color(ta, COLOR_TEXT, 0);

    /* IMPORTANT: same event handler */
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);

    return ta;
}



/* Small colored button used for MIN / MAX */
static lv_obj_t * create_small_label(lv_obj_t * parent, const char * txt)
{
    /* Container */
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, 60, 30);
    lv_obj_set_style_bg_color(box, COLOR_LABEL, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);   /* no touch action */

    /* Text */
    lv_obj_t * lbl = lv_label_create(box);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    return box;
}


/* =======================
 * Back override
 * =======================*/
static void back_to_edit_menu(void)
{
    ui_edit_program_screen_init();
}

/* =======================
 * Screen init (absolute positioned layout)
 * =======================*/
void ui_dynamic_edit_screen_init(uint8_t pr_id)
{
    LV_UNUSED(pr_id);

    lv_obj_t * scr = lv_obj_create(NULL);
    ui_common_load_screen(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_common_create(scr);
    ui_common_set_back_cb(back_to_edit_menu);

    /* Title */
    lv_label_set_text(ui_label_title, "Dynamic Impulse Edit");
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_MID, 0, 18);

    /* Main container */
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 760, 360);
    lv_obj_set_pos(cont, 20, 80);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    char buf[32];

    /* ================= MODEL ================= */
    lv_obj_t * lbl_model = lv_label_create(cont);
    lv_label_set_text(lbl_model, "Model");
    lv_obj_set_style_text_color(lbl_model, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_model, 40, 10);

    lv_obj_t * model_ta = create_input(cont, "--");
    lv_obj_set_pos(model_ta, 110, 5);
    lv_obj_add_state(model_ta, LV_STATE_DISABLED);

    /* ================= FREQUENCY ================= */
    lv_obj_t * lbl_freq = lv_label_create(cont);
    lv_label_set_text(lbl_freq, "Impulse Frequency (Hz)");
    lv_obj_set_style_text_color(lbl_freq, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_freq, 40, 60);

    lv_snprintf(buf, sizeof(buf), "%.2f", plc.freq);
    lv_obj_t * freq_ta = create_input(cont, buf);
    lv_obj_set_pos(freq_ta, 320, 55);

    /* ================= MULTIPLIER ================= */
    lv_obj_t * lbl_fm = lv_label_create(cont);
    lv_label_set_text(lbl_fm, "Frequency Multiplier");
    lv_obj_set_style_text_color(lbl_fm, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_fm, 40, 110);

    lv_snprintf(buf, sizeof(buf), "%.2f", plc.fmulti);
    lv_obj_t * fm_ta = create_input(cont, buf);
    lv_obj_set_pos(fm_ta, 320, 105);

    /* ================= AMPLITUDE ================= */
    lv_obj_t * lbl_amp = lv_label_create(cont);
    lv_label_set_text(lbl_amp, "Amplitude (mm)");
    lv_obj_set_style_text_color(lbl_amp, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_amp, 40, 160);

    lv_snprintf(buf, sizeof(buf), "%.2f", plc.amp);
    lv_obj_t * amp_ta = create_input(cont, buf);
    lv_obj_set_pos(amp_ta, 320, 155);

    /* MIN / MAX — absolute */
    lv_obj_t * min_lbl = create_small_label(cont, "MIN");
    lv_obj_set_pos(min_lbl, 500, 105);

    lv_obj_t * max_lbl = create_small_label(cont, "MAX");
    lv_obj_set_pos(max_lbl, 576, 105);



    /* ================= CYCLES ================= */
    lv_obj_t * lbl_cycles = lv_label_create(cont);
    lv_label_set_text(lbl_cycles, "No of Cycles");
    lv_obj_set_style_text_color(lbl_cycles, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_cycles, 40, 210);

    lv_snprintf(buf, sizeof(buf), "%d", plc.cycles);
    lv_obj_t * cycles_ta = create_input_int(cont, buf);
    lv_obj_set_pos(cycles_ta, 320, 205);

    /* small editable boxes near amplitude */
    lv_obj_t * amp_small1 = create_small_box(cont, "0.01");
    lv_obj_set_pos(amp_small1, 500, 158);

    lv_obj_t * amp_small2 = create_small_box(cont, "--");
    lv_obj_set_pos(amp_small2, 576, 158);

    /* ================= DURATION ================= */
    lv_obj_t * lbl_dur = lv_label_create(cont);
    lv_label_set_text(lbl_dur, "Test Duration (min)");
    lv_obj_set_style_text_color(lbl_dur, COLOR_LABEL, 0);
    lv_obj_set_pos(lbl_dur, 40, 260);

    lv_snprintf(buf, sizeof(buf), "%.1f", plc.duration);
    lv_obj_t * dur_ta = create_input(cont, buf);
    lv_obj_set_pos(dur_ta, 320, 255);

    /* ================= SAVE ================= */
    lv_obj_t * save = lv_btn_create(cont);
    lv_obj_set_size(save, 170, 55);
    lv_obj_set_style_bg_color(save, COLOR_SAVE, 0);
    lv_obj_set_style_radius(save, 12, 0);
    lv_obj_set_pos(save, 500, 250);
    lv_label_set_text(lv_label_create(save), "SAVE");
}
