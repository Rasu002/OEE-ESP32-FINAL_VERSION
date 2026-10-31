/**
 * ui_minute.c - Live Status screen & Setting Time screen
 */

#include "ui_minute.h"
#include "ui_common.h"
#include "ui_rs485_state.h"
#include "rs485_tx.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

extern void ui_graph_screen_create(void);

#define TAG_HMI_NVS "HMI_NVS"

/* colours */
#define C_TEXT_DARK   lv_color_hex(0x1F2A33)
#define C_CARD_BG     lv_color_hex(0xEAF4FB)
#define C_CARD_BORDER lv_color_hex(0x2F6F8F)
#define C_RUN         lv_color_hex(0x1A7A3C)
#define C_IDLE        lv_color_hex(0x1A5CA8)
#define C_SETTING     lv_color_hex(0xE67E22)  /* Amber / Orange for Setting */
#define C_ERROR       lv_color_hex(0xC0392B)
#define C_OUT_ON      lv_color_hex(0x1A7A3C)
#define C_OUT_OFF     lv_color_hex(0x7F8C9A)

/* Live Status screen handles */
static lv_obj_t   *minute_screen   = NULL;
static lv_timer_t *poll_timer      = NULL;

/* today total card handles (left box) */
static lv_obj_t *lbl_today_heading   = NULL;   /* "TODAY TOTAL" */
static lv_obj_t *lbl_today_time      = NULL;   /* "Last Updated: 16:00" */
static lv_obj_t *lbl_today_run       = NULL;   /* "3H 35M 50S" */
static lv_obj_t *lbl_today_idle      = NULL;
static lv_obj_t *lbl_today_err       = NULL;
static lv_obj_t *lbl_today_setting   = NULL;

/* current hour card handles (right box) */
static lv_obj_t *lbl_hr_heading      = NULL;   /* "CURRENT HOUR" */
static lv_obj_t *lbl_hr_time         = NULL;   /* "Last Updated: 17:15" */
static lv_obj_t *lbl_hr_run          = NULL;   /* "80 sec" */
static lv_obj_t *lbl_hr_run_m        = NULL;   /* "1min 20sec" */
static lv_obj_t *lbl_hr_idle         = NULL;
static lv_obj_t *lbl_hr_idle_m       = NULL;
static lv_obj_t *lbl_hr_err          = NULL;
static lv_obj_t *lbl_hr_err_m        = NULL;
static lv_obj_t *lbl_hr_setting      = NULL;
static lv_obj_t *lbl_hr_setting_m    = NULL;

/* output boxes */
static lv_obj_t *card_out1         = NULL;
static lv_obj_t *card_out2         = NULL;
static lv_obj_t *lbl_out1_val      = NULL;
static lv_obj_t *lbl_out2_val      = NULL;

/* machine state (plain text, no box) */
static lv_obj_t *lbl_state         = NULL;

/* Setting Time screen handles */
static lv_obj_t   *setting_screen     = NULL;
static lv_obj_t   *ta_setting_time    = NULL;
static lv_obj_t   *lbl_setting_status = NULL;
static lv_obj_t   *lbl_seted_seconds  = NULL;
static lv_obj_t   *kb_setting         = NULL;
static lv_timer_t *setting_timer      = NULL;
static int         s_setting_minutes  = 2;  /* default 2 min (120s) */
static bool        s_nvs_loaded       = false;

/* HMI Flash Memory (NVS) Persistence */
static void load_hmi_nvs_setting(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("hmi_cfg", NVS_READONLY, &h);
    if (err == ESP_OK) {
        int32_t val = 0;
        if (nvs_get_i32(h, "set_mins", &val) == ESP_OK && val > 0 && val <= 60) {
            s_setting_minutes = (int)val;
            ESP_LOGI(TAG_HMI_NVS, "Loaded setting minutes from HMI Flash: %d min (%d sec)",
                     s_setting_minutes, s_setting_minutes * 60);
        }
        nvs_close(h);
    }
    s_nvs_loaded = true;
}

static void save_hmi_nvs_setting(int mins)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("hmi_cfg", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "set_mins", (int32_t)mins);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        if (err == ESP_OK) {
            ESP_LOGI(TAG_HMI_NVS, "Saved setting minutes to HMI Flash: %d min (%d sec)",
                     mins, mins * 60);
        }
    }
}

void ui_set_active_setting_minutes(int mins)
{
    if (mins < 1) mins = 1;
    if (mins > 60) mins = 60;
    if (s_setting_minutes != mins) {
        s_setting_minutes = mins;
        save_hmi_nvs_setting(mins);
        if (ta_setting_time) {
            char buf[16];
            lv_snprintf(buf, sizeof(buf), "%d", s_setting_minutes);
            lv_textarea_set_text(ta_setting_time, buf);
        }
        if (lbl_seted_seconds) {
            char buf[64];
            lv_snprintf(buf, sizeof(buf), "%d Minutes  =  %d Seconds",
                        s_setting_minutes, s_setting_minutes * 60);
            lv_label_set_text(lbl_seted_seconds, buf);
        }
    }
}

int ui_get_active_setting_minutes(void)
{
    if (!s_nvs_loaded) {
        load_hmi_nvs_setting();
    }
    return s_setting_minutes;
}

/* Navigation */
static void back_to_graph(void)
{
    if (poll_timer) {
        lv_timer_del(poll_timer);
        poll_timer = NULL;
    }
    minute_screen = NULL;
    lbl_today_heading = NULL;
    lbl_today_time = NULL;
    lbl_today_run = NULL;
    lbl_today_idle = NULL;
    lbl_today_err = NULL;
    lbl_today_setting = NULL;
    lbl_hr_heading = NULL;
    lbl_hr_time = NULL;
    lbl_hr_run = NULL;
    lbl_hr_run_m = NULL;
    lbl_hr_idle = NULL;
    lbl_hr_idle_m = NULL;
    lbl_hr_err = NULL;
    lbl_hr_err_m = NULL;
    lbl_hr_setting = NULL;
    lbl_hr_setting_m = NULL;
    card_out1 = NULL;
    card_out2 = NULL;
    lbl_out1_val = NULL;
    lbl_out2_val = NULL;
    lbl_state = NULL;
    ui_graph_screen_create();
}

static void nav_to_setting_time_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (poll_timer) {
            lv_timer_del(poll_timer);
            poll_timer = NULL;
        }
        minute_screen = NULL;
        lbl_today_heading = NULL;
        lbl_today_time = NULL;
        lbl_today_run = NULL;
        lbl_today_idle = NULL;
        lbl_today_err = NULL;
        lbl_today_setting = NULL;
        lbl_hr_heading = NULL;
        lbl_hr_time = NULL;
        lbl_hr_run = NULL;
        lbl_hr_run_m = NULL;
        lbl_hr_idle = NULL;
        lbl_hr_idle_m = NULL;
        lbl_hr_err = NULL;
        lbl_hr_err_m = NULL;
        lbl_hr_setting = NULL;
        lbl_hr_setting_m = NULL;
        card_out1 = NULL;
        card_out2 = NULL;
        lbl_out1_val = NULL;
        lbl_out2_val = NULL;
        lbl_state = NULL;
        ui_setting_time_screen_create();
    }
}

static void back_to_live_status(void)
{
    if (setting_timer) {
        lv_timer_del(setting_timer);
        setting_timer = NULL;
    }
    if (kb_setting) {
        lv_obj_del(kb_setting);
        kb_setting = NULL;
    }
    setting_screen = NULL;
    ui_minute_screen_create(NULL);
}

/* Setting Time Configuration Logic */
static void update_setting_labels(void)
{
    if (ta_setting_time) {
        char buf[16];
        lv_snprintf(buf, sizeof(buf), "%d", s_setting_minutes);
        lv_textarea_set_text(ta_setting_time, buf);
    }

    if (lbl_seted_seconds) {
        char buf[64];
        lv_snprintf(buf, sizeof(buf), "%d Minutes  =  %d Seconds",
                    s_setting_minutes, s_setting_minutes * 60);
        lv_label_set_text(lbl_seted_seconds, buf);
    }

    if (lbl_setting_status) {
        char buf[64];
        if (g_rs485_setting_ack_sec > 0) {
            lv_snprintf(buf, sizeof(buf), "ESP32 Flash: Confirmed %d sec (%d min) OK",
                        g_rs485_setting_ack_sec, g_rs485_setting_ack_sec / 60);
            lv_obj_set_style_text_color(lbl_setting_status, lv_color_hex(0x27AE60), 0);
        } else {
            lv_snprintf(buf, sizeof(buf), "Active Setting Time: %d sec (%d min)",
                        s_setting_minutes * 60, s_setting_minutes);
            lv_obj_set_style_text_color(lbl_setting_status, C_CARD_BORDER, 0);
        }
        lv_label_set_text(lbl_setting_status, buf);
    }
}

static void apply_setting_minutes(int mins)
{
    if (mins < 1) mins = 1;
    if (mins > 60) mins = 60;
    s_setting_minutes = mins;

    /* Save to HMI Flash Memory */
    save_hmi_nvs_setting(s_setting_minutes);

    g_rs485_setting_ack_sec = 0;  /* Reset ACK to await fresh confirmation from ESP32 */

    update_setting_labels();

    /* Send new setting threshold in seconds to ESP32 over RS-485 */
    rs485_send_setting_limit(s_setting_minutes * 60);
}

static void btn_minus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        apply_setting_minutes(s_setting_minutes - 1);
    }
}

static void btn_plus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        apply_setting_minutes(s_setting_minutes + 1);
    }
}

static void btn_apply_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (ta_setting_time) {
            int mins = atoi(lv_textarea_get_text(ta_setting_time));
            apply_setting_minutes(mins);
        }
        if (kb_setting) {
            lv_obj_del(kb_setting);
            kb_setting = NULL;
        }
    }
}

static void ta_setting_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        if (!kb_setting && setting_screen) {
            kb_setting = lv_keyboard_create(setting_screen);
            lv_keyboard_set_mode(kb_setting, LV_KEYBOARD_MODE_NUMBER);
            lv_keyboard_set_textarea(kb_setting, ta);
            lv_obj_set_size(kb_setting, 760, 190);
            lv_obj_align(kb_setting, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_move_foreground(kb_setting);
        }
    } else if (code == LV_EVENT_READY) {
        int mins = atoi(lv_textarea_get_text(ta));
        apply_setting_minutes(mins);
        if (kb_setting) {
            lv_obj_del(kb_setting);
            kb_setting = NULL;
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (kb_setting) {
            lv_obj_del(kb_setting);
            kb_setting = NULL;
        }
    }
}

static void setting_poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (lbl_setting_status) {
        char buf[64];
        if (g_rs485_setting_ack_sec > 0) {
            lv_snprintf(buf, sizeof(buf), "ESP32 Flash: Confirmed %d sec (%d min) OK",
                        g_rs485_setting_ack_sec, g_rs485_setting_ack_sec / 60);
            lv_obj_set_style_text_color(lbl_setting_status, lv_color_hex(0x27AE60), 0);
            lv_label_set_text(lbl_setting_status, buf);
        }
    }
}

static void format_min_sec(char *buf, size_t sz, int total_sec)
{
    if (total_sec < 0) total_sec = 0;
    int mins = total_sec / 60;
    int secs = total_sec % 60;
    lv_snprintf(buf, sz, "%dmin %dsec", mins, secs);
}

static void format_hms(char *buf, size_t sz, int total_sec)
{
    if (total_sec < 0) total_sec = 0;
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    lv_snprintf(buf, sz, "%dH %dM %dS", h, m, s);
}

/* Live Status display update */
static void update_display(void)
{
    char buf[40];

    /* ---- 1. TODAY TOTAL card (Left) - ONLY completed hours (starting to last closed hour, NOT including current hour) ---- */
    int tot_run = 0;
    int tot_idle = 0;
    int tot_err = 0;
    int tot_setting = 0;
    char last_closed_ts[32] = "--:--";

    if (today_csv_count > 0) {
        for (int i = 0; i < today_csv_count; i++) {
            tot_run     += (int)today_csv[i].run;
            tot_idle    += (int)today_csv[i].idle;
            tot_err     += (int)today_csv[i].error;
            tot_setting += (int)today_csv[i].setting;
        }
        /* Extract HH:MM from the last completed hour timestamp: "YYYY-MM-DD HH:MM:SS" */
        const char *sp = strchr(today_csv[today_csv_count - 1].timestamp, ' ');
        if (sp && strlen(sp) >= 6) {
            strncpy(last_closed_ts, sp + 1, 5);
            last_closed_ts[5] = '\0';
        }
    } else if (g_rs485_oee_graph_state.valid) {
        int last_h = -1;
        for (int h = 0; h < RS485_OEE_HOURS; h++) {
            uint16_t r = g_rs485_oee_graph_state.run[h];
            uint16_t idl = g_rs485_oee_graph_state.idle[h];
            uint16_t err = g_rs485_oee_graph_state.error[h];
            uint16_t set = g_rs485_oee_graph_state.setting[h];
            if (r > 0 || idl > 0 || err > 0 || set > 0) {
                tot_run     += r;
                tot_idle    += idl;
                tot_err     += err;
                tot_setting += set;
                last_h = h;
            }
        }
        if (last_h >= 0) {
            lv_snprintf(last_closed_ts, sizeof(last_closed_ts), "%02d:00", last_h);
        }
    }

    if (lbl_today_time && lv_obj_is_valid(lbl_today_time)) {
        lv_snprintf(buf, sizeof(buf), "Last Updated: %s", last_closed_ts);
        lv_label_set_text(lbl_today_time, buf);
    }

    if (lbl_today_run && lv_obj_is_valid(lbl_today_run)) {
        format_hms(buf, sizeof(buf), tot_run);
        lv_label_set_text(lbl_today_run, buf);
    }
    if (lbl_today_idle && lv_obj_is_valid(lbl_today_idle)) {
        format_hms(buf, sizeof(buf), tot_idle);
        lv_label_set_text(lbl_today_idle, buf);
    }
    if (lbl_today_err && lv_obj_is_valid(lbl_today_err)) {
        format_hms(buf, sizeof(buf), tot_err);
        lv_label_set_text(lbl_today_err, buf);
    }
    if (lbl_today_setting && lv_obj_is_valid(lbl_today_setting)) {
        format_hms(buf, sizeof(buf), tot_setting);
        lv_label_set_text(lbl_today_setting, buf);
    }

    /* ---- 2. CURRENT HOUR card (Right) - current ongoing hour minute slice ---- */
    if (g_rs485_minute_state.valid && g_rs485_minute_state.count > 0) {
        int head = g_rs485_minute_state.head;
        int last = (head - 1 + RS485_MINUTE_SLOTS) % RS485_MINUTE_SLOTS;
        const rs485_minute_entry_t *m = &g_rs485_minute_state.entries[last];

        if (lbl_hr_time && lv_obj_is_valid(lbl_hr_time)) {
            lv_snprintf(buf, sizeof(buf), "Last Updated: %02d:%02d", m->hour, m->minute);
            lv_label_set_text(lbl_hr_time, buf);
        }

        if (lbl_hr_run && lv_obj_is_valid(lbl_hr_run)) {
            lv_snprintf(buf, sizeof(buf), "%d sec", (int)m->run_sec);
            lv_label_set_text(lbl_hr_run, buf);
        }
        if (lbl_hr_run_m && lv_obj_is_valid(lbl_hr_run_m)) {
            format_min_sec(buf, sizeof(buf), (int)m->run_sec);
            lv_label_set_text(lbl_hr_run_m, buf);
        }

        if (lbl_hr_idle && lv_obj_is_valid(lbl_hr_idle)) {
            lv_snprintf(buf, sizeof(buf), "%d sec", (int)m->idle_sec);
            lv_label_set_text(lbl_hr_idle, buf);
        }
        if (lbl_hr_idle_m && lv_obj_is_valid(lbl_hr_idle_m)) {
            format_min_sec(buf, sizeof(buf), (int)m->idle_sec);
            lv_label_set_text(lbl_hr_idle_m, buf);
        }

        if (lbl_hr_err && lv_obj_is_valid(lbl_hr_err)) {
            lv_snprintf(buf, sizeof(buf), "%d sec", (int)m->error_sec);
            lv_label_set_text(lbl_hr_err, buf);
        }
        if (lbl_hr_err_m && lv_obj_is_valid(lbl_hr_err_m)) {
            format_min_sec(buf, sizeof(buf), (int)m->error_sec);
            lv_label_set_text(lbl_hr_err_m, buf);
        }

        if (lbl_hr_setting && lv_obj_is_valid(lbl_hr_setting)) {
            lv_snprintf(buf, sizeof(buf), "%d sec", (int)m->setting_sec);
            lv_label_set_text(lbl_hr_setting, buf);
        }
        if (lbl_hr_setting_m && lv_obj_is_valid(lbl_hr_setting_m)) {
            format_min_sec(buf, sizeof(buf), (int)m->setting_sec);
            lv_label_set_text(lbl_hr_setting_m, buf);
        }
    }

    /* ---- 3. output indicators + machine state ---- */
    if (g_rs485_relay_state.valid) {
        bool o1 = g_rs485_relay_state.relay1;
        bool o2 = g_rs485_relay_state.relay2;

        lv_obj_set_style_bg_color(card_out1, o1 ? C_OUT_ON : C_OUT_OFF, 0);
        lv_label_set_text(lbl_out1_val, o1 ? "ON" : "OFF");

        lv_obj_set_style_bg_color(card_out2, o2 ? C_OUT_ON : C_OUT_OFF, 0);
        lv_label_set_text(lbl_out2_val, o2 ? "ON" : "OFF");

        if (o2) {
            lv_obj_set_style_text_color(lbl_state, C_ERROR, 0);
            lv_label_set_text(lbl_state, "ERROR");
        } else if (o1) {
            lv_obj_set_style_text_color(lbl_state, C_RUN, 0);
            lv_label_set_text(lbl_state, "RUN");
        } else {
            lv_obj_set_style_text_color(lbl_state, C_IDLE, 0);
            lv_label_set_text(lbl_state, "IDLE");
        }
    }
}

static void poll_cb(lv_timer_t *t) { LV_UNUSED(t); update_display(); }

/* small output box helper */
static lv_obj_t *make_output_box(lv_obj_t *parent, int x, int y,
                                  const char *name, lv_obj_t **val_lbl)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 110, 78);
    lv_obj_set_style_bg_color(card, C_OUT_OFF, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_shadow_width(card, 5, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x80A0B0), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Name label - full width, text centred, fixed y */
    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, name);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(lbl_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(lbl_name, 110, 20);
    lv_obj_set_pos(lbl_name, 0, 10);

    /* Value label - full width, text centred, fixed y below name */
    *val_lbl = lv_label_create(card);
    lv_label_set_text(*val_lbl, "---");
    lv_obj_set_style_text_font(*val_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(*val_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(*val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(*val_lbl, 110, 22);
    lv_obj_set_pos(*val_lbl, 0, 42);

    return card;
}

/* =========================================================================
 * 1. LIVE STATUS SCREEN
 * ========================================================================= */
void ui_minute_screen_create(lv_obj_t *back_screen)
{
    LV_UNUSED(back_screen);

    if (!s_nvs_loaded) {
        load_hmi_nvs_setting();
    }

    minute_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(minute_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_common_create(minute_screen);
    ui_common_set_back_cb(back_to_graph);
    lv_label_set_text(ui_label_title, "LIVE STATUS");

    /* TWO STATUS CARDS: Box 1 (TODAY TOTAL) and Box 2 (CURRENT HOUR) */
    const int card_w = 366, card_h = 236;
    const int card_y = 70;

    /* -------------------------------------------------------------
     * 1. Box 1: TODAY TOTAL (Left, x=24)
     * ------------------------------------------------------------- */
    lv_obj_t *card_today = lv_obj_create(minute_screen);
    lv_obj_set_pos(card_today, 24, card_y);
    lv_obj_set_size(card_today, card_w, card_h);
    lv_obj_set_style_bg_color(card_today, C_CARD_BG, 0);
    lv_obj_set_style_border_color(card_today, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_today, 2, 0);
    lv_obj_set_style_radius(card_today, 10, 0);
    lv_obj_set_style_shadow_width(card_today, 8, 0);
    lv_obj_set_style_shadow_color(card_today, lv_color_hex(0xA0BDD0), 0);
    lv_obj_set_style_shadow_opa(card_today, LV_OPA_50, 0);
    lv_obj_clear_flag(card_today, LV_OBJ_FLAG_SCROLLABLE);

    /* Heading: Top Center */
    lbl_today_heading = lv_label_create(card_today);
    lv_label_set_text(lbl_today_heading, "TODAY TOTAL");
    lv_obj_set_style_text_font(lbl_today_heading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_today_heading, C_CARD_BORDER, 0);
    lv_obj_set_width(lbl_today_heading, card_w - 28);
    lv_obj_set_style_text_align(lbl_today_heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_today_heading, 14, 8);

    /* Sub-heading: Last Updated: --:-- (Top Center below Heading) */
    lbl_today_time = lv_label_create(card_today);
    lv_label_set_text(lbl_today_time, "Last Updated: --:--");
    lv_obj_set_style_text_font(lbl_today_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_today_time, lv_color_hex(0x506070), 0);
    lv_obj_set_width(lbl_today_time, card_w - 28);
    lv_obj_set_style_text_align(lbl_today_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_today_time, 14, 28);

    /* Divider */
    lv_obj_t *div1 = lv_obj_create(card_today);
    lv_obj_set_size(div1, card_w - 28, 2);
    lv_obj_set_pos(div1, 14, 48);
    lv_obj_set_style_bg_color(div1, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(div1, 0, 0);
    lv_obj_set_style_radius(div1, 0, 0);

    /* Today Total: Only 1 data column (3H 35M 50S), SEC column removed */
    const int t_name_x = 24;
    const int t_val_x  = 180;
    const int row_y[4] = { 68, 108, 148, 188 };

    /* RUN */
    lv_obj_t *tr1 = lv_label_create(card_today);
    lv_label_set_text(tr1, "RUN");
    lv_obj_set_style_text_font(tr1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tr1, C_RUN, 0);
    lv_obj_set_pos(tr1, t_name_x, row_y[0]);
    lbl_today_run = lv_label_create(card_today);
    lv_label_set_text(lbl_today_run, "---");
    lv_obj_set_style_text_font(lbl_today_run, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_today_run, C_RUN, 0);
    lv_obj_set_pos(lbl_today_run, t_val_x, row_y[0]);

    /* IDLE */
    lv_obj_t *tr2 = lv_label_create(card_today);
    lv_label_set_text(tr2, "IDLE");
    lv_obj_set_style_text_font(tr2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tr2, C_IDLE, 0);
    lv_obj_set_pos(tr2, t_name_x, row_y[1]);
    lbl_today_idle = lv_label_create(card_today);
    lv_label_set_text(lbl_today_idle, "---");
    lv_obj_set_style_text_font(lbl_today_idle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_today_idle, C_IDLE, 0);
    lv_obj_set_pos(lbl_today_idle, t_val_x, row_y[1]);

    /* ERROR */
    lv_obj_t *tr3 = lv_label_create(card_today);
    lv_label_set_text(tr3, "ERROR");
    lv_obj_set_style_text_font(tr3, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tr3, C_ERROR, 0);
    lv_obj_set_pos(tr3, t_name_x, row_y[2]);
    lbl_today_err = lv_label_create(card_today);
    lv_label_set_text(lbl_today_err, "---");
    lv_obj_set_style_text_font(lbl_today_err, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_today_err, C_ERROR, 0);
    lv_obj_set_pos(lbl_today_err, t_val_x, row_y[2]);

    /* SETTING */
    lv_obj_t *tr4 = lv_label_create(card_today);
    lv_label_set_text(tr4, "SETTING");
    lv_obj_set_style_text_font(tr4, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tr4, C_SETTING, 0);
    lv_obj_set_pos(tr4, t_name_x, row_y[3]);
    lbl_today_setting = lv_label_create(card_today);
    lv_label_set_text(lbl_today_setting, "---");
    lv_obj_set_style_text_font(lbl_today_setting, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_today_setting, C_SETTING, 0);
    lv_obj_set_pos(lbl_today_setting, t_val_x, row_y[3]);

    /* -------------------------------------------------------------
     * 2. Box 2: CURRENT HOUR (Right, x=410)
     * ------------------------------------------------------------- */
    lv_obj_t *card_last = lv_obj_create(minute_screen);
    lv_obj_set_pos(card_last, 410, card_y);
    lv_obj_set_size(card_last, card_w, card_h);
    lv_obj_set_style_bg_color(card_last, C_CARD_BG, 0);
    lv_obj_set_style_border_color(card_last, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_last, 2, 0);
    lv_obj_set_style_radius(card_last, 10, 0);
    lv_obj_set_style_shadow_width(card_last, 8, 0);
    lv_obj_set_style_shadow_color(card_last, lv_color_hex(0xA0BDD0), 0);
    lv_obj_set_style_shadow_opa(card_last, LV_OPA_50, 0);
    lv_obj_clear_flag(card_last, LV_OBJ_FLAG_SCROLLABLE);

    /* Heading: Top Center */
    lbl_hr_heading = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_heading, "CURRENT HOUR");
    lv_obj_set_style_text_font(lbl_hr_heading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_heading, C_CARD_BORDER, 0);
    lv_obj_set_width(lbl_hr_heading, card_w - 28);
    lv_obj_set_style_text_align(lbl_hr_heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_hr_heading, 14, 8);

    /* Sub-heading: Last Updated: --:-- (Top Center below Heading) */
    lbl_hr_time = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_time, "Last Updated: --:--");
    lv_obj_set_style_text_font(lbl_hr_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_hr_time, lv_color_hex(0x506070), 0);
    lv_obj_set_width(lbl_hr_time, card_w - 28);
    lv_obj_set_style_text_align(lbl_hr_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_hr_time, 14, 28);

    /* Divider */
    lv_obj_t *div2 = lv_obj_create(card_last);
    lv_obj_set_size(div2, card_w - 28, 2);
    lv_obj_set_pos(div2, 14, 48);
    lv_obj_set_style_bg_color(div2, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(div2, 0, 0);
    lv_obj_set_style_radius(div2, 0, 0);

    /* Column headers: SEC and MIN */
    const int h_sec_x = 115;
    const int h_min_x = 225;

    lv_obj_t *hdr_sec = lv_label_create(card_last);
    lv_label_set_text(hdr_sec, "SEC");
    lv_obj_set_style_text_font(hdr_sec, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr_sec, C_CARD_BORDER, 0);
    lv_obj_set_pos(hdr_sec, h_sec_x, 52);

    lv_obj_t *hdr_min = lv_label_create(card_last);
    lv_label_set_text(hdr_min, "MIN");
    lv_obj_set_style_text_font(hdr_min, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr_min, C_CARD_BORDER, 0);
    lv_obj_set_pos(hdr_min, h_min_x, 52);

    /* Current Hour Rows: RUN, IDLE, ERROR, SETTING with SEC and MIN */
    const int cur_row_y[4] = { 68, 108, 148, 188 };

    /* RUN */
    lv_obj_t *k1 = lv_label_create(card_last);
    lv_label_set_text(k1, "RUN");
    lv_obj_set_style_text_font(k1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(k1, C_RUN, 0);
    lv_obj_set_pos(k1, 16, cur_row_y[0]);
    lbl_hr_run = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_run, "---");
    lv_obj_set_style_text_font(lbl_hr_run, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_run, C_RUN, 0);
    lv_obj_set_pos(lbl_hr_run, h_sec_x, cur_row_y[0]);
    lbl_hr_run_m = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_run_m, "---");
    lv_obj_set_style_text_font(lbl_hr_run_m, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_run_m, C_RUN, 0);
    lv_obj_set_pos(lbl_hr_run_m, h_min_x, cur_row_y[0]);

    /* IDLE */
    lv_obj_t *k2 = lv_label_create(card_last);
    lv_label_set_text(k2, "IDLE");
    lv_obj_set_style_text_font(k2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(k2, C_IDLE, 0);
    lv_obj_set_pos(k2, 16, cur_row_y[1]);
    lbl_hr_idle = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_idle, "---");
    lv_obj_set_style_text_font(lbl_hr_idle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_idle, C_IDLE, 0);
    lv_obj_set_pos(lbl_hr_idle, h_sec_x, cur_row_y[1]);
    lbl_hr_idle_m = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_idle_m, "---");
    lv_obj_set_style_text_font(lbl_hr_idle_m, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_idle_m, C_IDLE, 0);
    lv_obj_set_pos(lbl_hr_idle_m, h_min_x, cur_row_y[1]);

    /* ERROR */
    lv_obj_t *k3 = lv_label_create(card_last);
    lv_label_set_text(k3, "ERROR");
    lv_obj_set_style_text_font(k3, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(k3, C_ERROR, 0);
    lv_obj_set_pos(k3, 16, cur_row_y[2]);
    lbl_hr_err = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_err, "---");
    lv_obj_set_style_text_font(lbl_hr_err, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_err, C_ERROR, 0);
    lv_obj_set_pos(lbl_hr_err, h_sec_x, cur_row_y[2]);
    lbl_hr_err_m = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_err_m, "---");
    lv_obj_set_style_text_font(lbl_hr_err_m, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_err_m, C_ERROR, 0);
    lv_obj_set_pos(lbl_hr_err_m, h_min_x, cur_row_y[2]);

    /* SETTING */
    lv_obj_t *k4 = lv_label_create(card_last);
    lv_label_set_text(k4, "SETTING");
    lv_obj_set_style_text_font(k4, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(k4, C_SETTING, 0);
    lv_obj_set_pos(k4, 16, cur_row_y[3]);
    lbl_hr_setting = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_setting, "---");
    lv_obj_set_style_text_font(lbl_hr_setting, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_setting, C_SETTING, 0);
    lv_obj_set_pos(lbl_hr_setting, h_sec_x, cur_row_y[3]);
    lbl_hr_setting_m = lv_label_create(card_last);
    lv_label_set_text(lbl_hr_setting_m, "---");
    lv_obj_set_style_text_font(lbl_hr_setting_m, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_hr_setting_m, C_SETTING, 0);
    lv_obj_set_pos(lbl_hr_setting_m, h_min_x, cur_row_y[3]);

    /* -------------------------------------------------------------
     * 3. Machine State section
     * ------------------------------------------------------------- */
    const int box_w   = 110;
    const int box_gap = 20;
    const int total_w = 2 * box_w + box_gap;          /* 240 */
    const int boxes_x = (800 - total_w) / 2;          /* 280 */

    lv_obj_t *sec_lbl = lv_label_create(minute_screen);
    lv_label_set_text(sec_lbl, "Machine State");
    lv_obj_set_style_text_font(sec_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sec_lbl, C_TEXT_DARK, 0);
    lv_obj_set_width(sec_lbl, total_w);
    lv_obj_set_style_text_align(sec_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sec_lbl, boxes_x, 316);

    /* small output boxes */
    card_out1 = make_output_box(minute_screen, boxes_x,              338, "OUTPUT 1", &lbl_out1_val);
    card_out2 = make_output_box(minute_screen, boxes_x + box_w + box_gap, 338, "OUTPUT 2", &lbl_out2_val);

    /* machine state: plain large text, centred below boxes */
    lbl_state = lv_label_create(minute_screen);
    lv_label_set_text(lbl_state, "---");
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_state, C_IDLE, 0);
    lv_obj_set_width(lbl_state, total_w);
    lv_obj_set_style_text_align(lbl_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_state, boxes_x, 422);

    /* NEXT BUTTON: in bottom-right corner */
    lv_obj_t *btn_next = lv_btn_create(minute_screen);
    lv_obj_set_size(btn_next, 125, 46);
    lv_obj_set_pos(btn_next, 650, 414);
    lv_obj_set_style_bg_color(btn_next, C_SETTING, 0);  /* Amber accent */
    lv_obj_set_style_radius(btn_next, 8, 0);
    lv_obj_set_style_shadow_width(btn_next, 8, 0);
    lv_obj_set_style_shadow_color(btn_next, lv_color_hex(0xA0BDD0), 0);
    lv_obj_set_style_shadow_opa(btn_next, LV_OPA_50, 0);
    lv_obj_add_event_cb(btn_next, nav_to_setting_time_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, "NEXT >");
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_next);

    /* load + poll */
    ui_common_load_screen(minute_screen);
    poll_timer = lv_timer_create(poll_cb, 1000, NULL);
    lv_timer_ready(poll_timer);
}

/* =========================================================================
 * 2. SETTING TIME CONFIGURATION SCREEN
 * ========================================================================= */
void ui_setting_time_screen_create(void)
{
    load_hmi_nvs_setting();

    setting_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(setting_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui_common_create(setting_screen);
    ui_common_set_back_cb(back_to_live_status);
    lv_label_set_text(ui_label_title, "SETTING TIME");

    /* Main Card: 560 x 330, centred at x=120, y=80 */
    const int card_w = 560, card_h = 330;
    const int card_x = (800 - card_w) / 2;  /* 120 */
    const int card_y = 80;

    lv_obj_t *card = lv_obj_create(setting_screen);
    lv_obj_set_pos(card, card_x, card_y);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_style_bg_color(card, C_CARD_BG, 0);
    lv_obj_set_style_border_color(card, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0xA0BDD0), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "SETTING TIME CONFIGURATION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, C_SETTING, 0);
    lv_obj_set_width(title, card_w - 40);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 20, 12);

    /* Divider */
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, card_w - 40, 2);
    lv_obj_set_pos(div, 20, 44);
    lv_obj_set_style_bg_color(div, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    /* Instruction Label */
    lv_obj_t *lbl_inst = lv_label_create(card);
    lv_label_set_text(lbl_inst, "Enter Setting Time duration in Minutes:");
    lv_obj_set_style_text_font(lbl_inst, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_inst, C_TEXT_DARK, 0);
    lv_obj_set_width(lbl_inst, card_w - 40);
    lv_obj_set_style_text_align(lbl_inst, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_inst, 20, 56);

    /* Stepper row: [-]  [ textarea ]  [+]  MINUTES */
    const int stepper_y = 86;
    const int start_x = 135;

    /* Minus Button */
    lv_obj_t *btn_minus = lv_btn_create(card);
    lv_obj_set_size(btn_minus, 46, 42);
    lv_obj_set_pos(btn_minus, start_x, stepper_y);
    lv_obj_set_style_bg_color(btn_minus, C_CARD_BORDER, 0);
    lv_obj_set_style_radius(btn_minus, 6, 0);
    lv_obj_add_event_cb(btn_minus, btn_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_minus);

    /* Text Area */
    ta_setting_time = lv_textarea_create(card);
    lv_obj_set_size(ta_setting_time, 84, 42);
    lv_obj_set_pos(ta_setting_time, start_x + 56, stepper_y);
    lv_textarea_set_one_line(ta_setting_time, true);
    lv_textarea_set_max_length(ta_setting_time, 3);
    char init_buf[8];
    lv_snprintf(init_buf, sizeof(init_buf), "%d", s_setting_minutes);
    lv_textarea_set_text(ta_setting_time, init_buf);
    lv_obj_set_style_text_align(ta_setting_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ta_setting_time, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(ta_setting_time, ta_setting_event_cb, LV_EVENT_ALL, NULL);

    /* Plus Button */
    lv_obj_t *btn_plus = lv_btn_create(card);
    lv_obj_set_size(btn_plus, 46, 42);
    lv_obj_set_pos(btn_plus, start_x + 150, stepper_y);
    lv_obj_set_style_bg_color(btn_plus, C_CARD_BORDER, 0);
    lv_obj_set_style_radius(btn_plus, 6, 0);
    lv_obj_add_event_cb(btn_plus, btn_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_plus);

    /* Minutes unit label */
    lv_obj_t *lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, "MINUTES");
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_unit, C_TEXT_DARK, 0);
    lv_obj_set_pos(lbl_unit, start_x + 208, stepper_y + 11);

    /* APPLY Button */
    lv_obj_t *btn_apply = lv_btn_create(card);
    lv_obj_set_size(btn_apply, 180, 42);
    lv_obj_set_pos(btn_apply, (card_w - 180) / 2, 142);
    lv_obj_set_style_bg_color(btn_apply, C_SETTING, 0);
    lv_obj_set_style_radius(btn_apply, 8, 0);
    lv_obj_add_event_cb(btn_apply, btn_apply_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_apply = lv_label_create(btn_apply);
    lv_label_set_text(lbl_apply, "APPLY");
    lv_obj_set_style_text_font(lbl_apply, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_apply);

    /* Inner box for Seted Seconds display */
    const int ibox_w = 480, ibox_h = 100;
    const int ibox_x = (card_w - ibox_w) / 2;
    const int ibox_y = 198;

    lv_obj_t *ibox = lv_obj_create(card);
    lv_obj_set_pos(ibox, ibox_x, ibox_y);
    lv_obj_set_size(ibox, ibox_w, ibox_h);
    lv_obj_set_style_bg_color(ibox, lv_color_hex(0xDAECF5), 0);
    lv_obj_set_style_border_color(ibox, C_CARD_BORDER, 0);
    lv_obj_set_style_border_width(ibox, 1, 0);
    lv_obj_set_style_radius(ibox, 8, 0);
    lv_obj_clear_flag(ibox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_sec_hdr = lv_label_create(ibox);
    lv_label_set_text(lbl_sec_hdr, "SETTING TIME SETED SECONDS");
    lv_obj_set_style_text_font(lbl_sec_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sec_hdr, C_CARD_BORDER, 0);
    lv_obj_set_width(lbl_sec_hdr, ibox_w - 20);
    lv_obj_set_style_text_align(lbl_sec_hdr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_sec_hdr, 10, 8);

    lbl_seted_seconds = lv_label_create(ibox);
    lv_obj_set_style_text_font(lbl_seted_seconds, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_seted_seconds, C_SETTING, 0);
    lv_obj_set_width(lbl_seted_seconds, ibox_w - 20);
    lv_obj_set_style_text_align(lbl_seted_seconds, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_seted_seconds, 10, 32);

    lbl_setting_status = lv_label_create(ibox);
    lv_obj_set_style_text_font(lbl_setting_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_setting_status, C_CARD_BORDER, 0);
    lv_obj_set_width(lbl_setting_status, ibox_w - 20);
    lv_obj_set_style_text_align(lbl_setting_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_setting_status, 10, 64);

    update_setting_labels();

    ui_common_load_screen(setting_screen);
    setting_timer = lv_timer_create(setting_poll_cb, 500, NULL);
    lv_timer_ready(setting_timer);
}
