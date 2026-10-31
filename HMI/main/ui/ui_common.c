#include "lvgl.h"
#include "ui_common.h"
#include "ui_rs485_state.h"
#include "rs485_tx.h"

/* =======================
 * Back callback handling
 * =======================*/
static ui_back_cb_t back_cb = NULL;
static bool back_nav_pending = false;

static void event_back_async(void * user_data)
{
    LV_UNUSED(user_data);
    back_nav_pending = false;
    if (back_cb) {
        back_cb();
    }
}

static void event_back(lv_event_t * e)
{
    LV_UNUSED(e);
    if (back_cb && !back_nav_pending) {
        back_nav_pending = true;
        lv_async_call(event_back_async, NULL);
    }
}

void ui_common_set_back_cb(ui_back_cb_t cb)
{
    back_cb = cb;
}

void ui_common_load_screen(lv_obj_t *screen)
{
    lv_obj_t *old = lv_scr_act();
    lv_scr_load(screen);
    if (old && old != screen) {
        lv_obj_del_async(old);
    }
}

/* =======================
 * Colors
 * =======================*/
#define COLOR_BG_SKY     lv_color_hex(0xD9ECF5)
#define COLOR_TEXT_DARK  lv_color_hex(0x1F2A33)
#define COLOR_BACK_BTN   lv_color_hex(0x2F6F8F)
#define COLOR_BACK_TEXT  lv_color_hex(0xFFFFFF)

/* =======================
 * Global UI objects
 * =======================*/
lv_obj_t * ui_label_date = NULL;
lv_obj_t * ui_label_time = NULL;
lv_obj_t * ui_label_title = NULL;
lv_obj_t * ui_btn_back = NULL;

/* =======================
 * Date & Time State
 * =======================*/
static uint16_t s_year = 2026;
static uint8_t  s_month = 10;
static uint8_t  s_day = 29;
static uint8_t  s_hour = 12;
static uint8_t  s_min = 0;
static uint8_t  s_sec = 0;
static uint32_t s_last_time_seq = 0;
static lv_timer_t * time_timer = NULL;

/* Modal objects */
static lv_obj_t * s_modal_bg = NULL;
static int edit_day = 29, edit_mon = 10, edit_yr = 2026, edit_hr = 12, edit_min = 0;
static lv_obj_t *lbl_val_day = NULL, *lbl_val_mon = NULL, *lbl_val_yr = NULL;
static lv_obj_t *lbl_val_hr = NULL, *lbl_val_min = NULL;

static void update_modal_display(void)
{
    char buf[16];
    if (lbl_val_day && lv_obj_is_valid(lbl_val_day)) {
        lv_snprintf(buf, sizeof(buf), "%02d", edit_day);
        lv_label_set_text(lbl_val_day, buf);
    }
    if (lbl_val_mon && lv_obj_is_valid(lbl_val_mon)) {
        lv_snprintf(buf, sizeof(buf), "%02d", edit_mon);
        lv_label_set_text(lbl_val_mon, buf);
    }
    if (lbl_val_yr && lv_obj_is_valid(lbl_val_yr)) {
        lv_snprintf(buf, sizeof(buf), "%04d", edit_yr);
        lv_label_set_text(lbl_val_yr, buf);
    }
    if (lbl_val_hr && lv_obj_is_valid(lbl_val_hr)) {
        lv_snprintf(buf, sizeof(buf), "%02d", edit_hr);
        lv_label_set_text(lbl_val_hr, buf);
    }
    if (lbl_val_min && lv_obj_is_valid(lbl_val_min)) {
        lv_snprintf(buf, sizeof(buf), "%02d", edit_min);
        lv_label_set_text(lbl_val_min, buf);
    }
}

static void modal_field_cb(lv_event_t * e)
{
    intptr_t tag = (intptr_t)lv_event_get_user_data(e);
    switch (tag) {
        case 1:  edit_day = (edit_day >= 31) ? 1 : edit_day + 1; break;
        case -1: edit_day = (edit_day <= 1) ? 31 : edit_day - 1; break;
        case 2:  edit_mon = (edit_mon >= 12) ? 1 : edit_mon + 1; break;
        case -2: edit_mon = (edit_mon <= 1) ? 12 : edit_mon - 1; break;
        case 3:  edit_yr = (edit_yr >= 2040) ? 2024 : edit_yr + 1; break;
        case -3: edit_yr = (edit_yr <= 2024) ? 2040 : edit_yr - 1; break;
        case 4:  edit_hr = (edit_hr >= 23) ? 0 : edit_hr + 1; break;
        case -4: edit_hr = (edit_hr <= 0) ? 23 : edit_hr - 1; break;
        case 5:  edit_min = (edit_min >= 59) ? 0 : edit_min + 1; break;
        case -5: edit_min = (edit_min <= 0) ? 59 : edit_min - 1; break;
    }
    update_modal_display();
}

static void modal_close_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (s_modal_bg && lv_obj_is_valid(s_modal_bg)) {
        lv_obj_del(s_modal_bg);
        s_modal_bg = NULL;
    }
}

static void modal_sync_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    rs485_request_time();
    edit_day = s_day;
    edit_mon = s_month;
    edit_yr  = s_year;
    edit_hr  = s_hour;
    edit_min = s_min;
    update_modal_display();
}

static void modal_save_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    rs485_send_set_time((uint16_t)edit_yr, (uint8_t)edit_mon, (uint8_t)edit_day,
                       (uint8_t)edit_hr, (uint8_t)edit_min, 0);

    /* Update locally immediately */
    s_year  = edit_yr;
    s_month = edit_mon;
    s_day   = edit_day;
    s_hour  = edit_hr;
    s_min   = edit_min;
    s_sec   = 0;

    char d_buf[20];
    char t_buf[16];
    lv_snprintf(d_buf, sizeof(d_buf), "%02d/%02d/%04d", s_day, s_month, s_year);
    lv_snprintf(t_buf, sizeof(t_buf), "%02d:%02d:%02d", s_hour, s_min, s_sec);

    if (ui_label_date && lv_obj_is_valid(ui_label_date)) {
        lv_label_set_text(ui_label_date, d_buf);
    }
    if (ui_label_time && lv_obj_is_valid(ui_label_time)) {
        lv_label_set_text(ui_label_time, t_buf);
    }

    if (s_modal_bg && lv_obj_is_valid(s_modal_bg)) {
        lv_obj_del(s_modal_bg);
        s_modal_bg = NULL;
    }
}

static lv_obj_t * create_spin_col(lv_obj_t * parent, int x, int y, const char * header,
                                   int tag, lv_obj_t ** out_lbl)
{
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 84, 115);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xEDF2F7), 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0xCBD5E1), 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_hdr = lv_label_create(cont);
    lv_label_set_text(lbl_hdr, header);
    lv_obj_align(lbl_hdr, LV_ALIGN_TOP_MID, 0, -2);
    lv_obj_set_style_text_font(lbl_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_hdr, lv_color_hex(0x64748B), 0);

    /* + Button */
    lv_obj_t * btn_up = lv_btn_create(cont);
    lv_obj_set_size(btn_up, 70, 26);
    lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(btn_up, 4, 0);
    lv_obj_add_event_cb(btn_up, modal_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)tag);
    lv_obj_t * lup = lv_label_create(btn_up);
    lv_label_set_text(lup, "+");
    lv_obj_center(lup);
    lv_obj_set_style_text_font(lup, &lv_font_montserrat_16, 0);

    /* Value */
    lv_obj_t * lbl_val = lv_label_create(cont);
    lv_label_set_text(lbl_val, "00");
    lv_obj_align(lbl_val, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x0F172A), 0);
    if (out_lbl) *out_lbl = lbl_val;

    /* - Button */
    lv_obj_t * btn_dn = lv_btn_create(cont);
    lv_obj_set_size(btn_dn, 70, 26);
    lv_obj_align(btn_dn, LV_ALIGN_BOTTOM_MID, 0, 4);
    lv_obj_set_style_bg_color(btn_dn, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_radius(btn_dn, 4, 0);
    lv_obj_add_event_cb(btn_dn, modal_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(-tag));
    lv_obj_t * ldn = lv_label_create(btn_dn);
    lv_label_set_text(ldn, "-");
    lv_obj_center(ldn);
    lv_obj_set_style_text_font(ldn, &lv_font_montserrat_16, 0);

    return cont;
}

void ui_open_datetime_modal(void)
{
    if (s_modal_bg && lv_obj_is_valid(s_modal_bg)) {
        return;
    }

    edit_day = s_day;
    edit_mon = s_month;
    edit_yr  = s_year;
    edit_hr  = s_hour;
    edit_min = s_min;

    lv_obj_t * scr = lv_scr_act();
    s_modal_bg = lv_obj_create(scr);
    lv_obj_set_size(s_modal_bg, 800, 480);
    lv_obj_set_pos(s_modal_bg, 0, 0);
    lv_obj_set_style_bg_color(s_modal_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal_bg, LV_OPA_60, 0);
    lv_obj_clear_flag(s_modal_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Card Box */
    lv_obj_t * card = lv_obj_create(s_modal_bg);
    lv_obj_set_size(card, 540, 340);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_shadow_width(card, 25, 0);
    lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t * title = lv_label_create(card);
    lv_label_set_text(title, "SET DATE & TIME (RTC)");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1E293B), 0);

    /* Section Date */
    create_spin_col(card, 15,  48, "DAY",   1, &lbl_val_day);
    create_spin_col(card, 110, 48, "MONTH", 2, &lbl_val_mon);
    create_spin_col(card, 205, 48, "YEAR",  3, &lbl_val_yr);

    /* Section Time */
    create_spin_col(card, 320, 48, "HOUR",  4, &lbl_val_hr);
    create_spin_col(card, 415, 48, "MIN",   5, &lbl_val_min);

    update_modal_display();

    /* Buttons row */
    /* 1. SYNC FROM RTC */
    lv_obj_t * btn_sync = lv_btn_create(card);
    lv_obj_set_size(btn_sync, 135, 44);
    lv_obj_align(btn_sync, LV_ALIGN_BOTTOM_LEFT, 15, -12);
    lv_obj_set_style_bg_color(btn_sync, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_radius(btn_sync, 8, 0);
    lv_obj_add_event_cb(btn_sync, modal_sync_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_sync = lv_label_create(btn_sync);
    lv_label_set_text(l_sync, "SYNC RTC");
    lv_obj_center(l_sync);
    lv_obj_set_style_text_font(l_sync, &lv_font_montserrat_14, 0);

    /* 2. CANCEL / CLOSE */
    lv_obj_t * btn_close = lv_btn_create(card);
    lv_obj_set_size(btn_close, 115, 44);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 15, -12);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_radius(btn_close, 8, 0);
    lv_obj_add_event_cb(btn_close, modal_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_cls = lv_label_create(btn_close);
    lv_label_set_text(l_cls, "CLOSE");
    lv_obj_center(l_cls);
    lv_obj_set_style_text_font(l_cls, &lv_font_montserrat_14, 0);

    /* 3. APPLY & SAVE */
    lv_obj_t * btn_save = lv_btn_create(card);
    lv_obj_set_size(btn_save, 160, 44);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -15, -12);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x16A34A), 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_add_event_cb(btn_save, modal_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_save = lv_label_create(btn_save);
    lv_label_set_text(l_save, "SAVE TO RTC");
    lv_obj_center(l_save);
    lv_obj_set_style_text_font(l_save, &lv_font_montserrat_14, 0);
}

static void event_datetime_click(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_open_datetime_modal();
}

/* =======================
 * Clock Timer Callback
 * =======================*/
static void time_update_cb(lv_timer_t * t)
{
    LV_UNUSED(t);

    if (g_rs485_time_state.valid && g_rs485_time_state.update_seq != s_last_time_seq) {
        s_last_time_seq = g_rs485_time_state.update_seq;
        s_year  = g_rs485_time_state.year;
        s_month = g_rs485_time_state.month;
        s_day   = g_rs485_time_state.day;
        s_hour  = g_rs485_time_state.hour;
        s_min   = g_rs485_time_state.minute;
        s_sec   = g_rs485_time_state.second;
    } else {
        s_sec++;
        if (s_sec >= 60) {
            s_sec = 0;
            s_min++;
            if (s_min >= 60) {
                s_min = 0;
                s_hour++;
                if (s_hour >= 24) {
                    s_hour = 0;
                    s_day++;
                }
            }
        }
    }

    char d_buf[20];
    char t_buf[16];
    lv_snprintf(d_buf, sizeof(d_buf), "%02d/%02d/%04d", s_day, s_month, s_year);
    lv_snprintf(t_buf, sizeof(t_buf), "%02d:%02d:%02d", s_hour, s_min, s_sec);

    if (ui_label_date && lv_obj_is_valid(ui_label_date)) {
        lv_label_set_text(ui_label_date, d_buf);
    }
    if (ui_label_time && lv_obj_is_valid(ui_label_time)) {
        lv_label_set_text(ui_label_time, t_buf);
    }
}

/* =======================
 * Create common UI
 * =======================*/
void ui_common_create(lv_obj_t * parent)
{
    /* Full screen background */
    lv_obj_set_style_bg_color(parent, COLOR_BG_SKY, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Formatted initial strings */
    char d_buf[20];
    char t_buf[16];
    lv_snprintf(d_buf, sizeof(d_buf), "%02d/%02d/%04d", s_day, s_month, s_year);
    lv_snprintf(t_buf, sizeof(t_buf), "%02d:%02d:%02d", s_hour, s_min, s_sec);

    /* Date (top-right) */
    ui_label_date = lv_label_create(parent);
    lv_label_set_text(ui_label_date, d_buf);
    lv_obj_align(ui_label_date, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_text_color(ui_label_date, COLOR_TEXT_DARK, 0);
    lv_obj_set_style_text_font(ui_label_date, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(ui_label_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_label_date, event_datetime_click, LV_EVENT_CLICKED, NULL);

    /* Time (below date) */
    ui_label_time = lv_label_create(parent);
    lv_label_set_text(ui_label_time, t_buf);
    lv_obj_align(ui_label_time, LV_ALIGN_TOP_RIGHT, -20, 30);
    lv_obj_set_style_text_color(ui_label_time, COLOR_TEXT_DARK, 0);
    lv_obj_set_style_text_font(ui_label_time, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(ui_label_time, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_label_time, event_datetime_click, LV_EVENT_CLICKED, NULL);

    /* Page title (top-center) */
    ui_label_title = lv_label_create(parent);
    lv_label_set_text(ui_label_title, "Servo Actuator");
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_text_color(ui_label_title, COLOR_TEXT_DARK, 0);
    lv_obj_set_style_text_font(ui_label_title, &lv_font_montserrat_20, 0);

    /* Back button (top-left) */
    ui_btn_back = lv_btn_create(parent);
    lv_obj_set_size(ui_btn_back, 110, 42);
    lv_obj_align(ui_btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_set_style_bg_color(ui_btn_back, COLOR_BACK_BTN, 0);
    lv_obj_set_style_radius(ui_btn_back, 6, 0);
    lv_obj_add_event_cb(ui_btn_back, event_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl = lv_label_create(ui_btn_back);
    lv_label_set_text(lbl, "BACK");
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, COLOR_BACK_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    /* Start clock timer once */
    if(time_timer == NULL) {
        time_timer = lv_timer_create(time_update_cb, 1000, NULL);
    }
}