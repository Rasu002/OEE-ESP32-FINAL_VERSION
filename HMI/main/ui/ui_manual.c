#include "lvgl.h"
#include "ui_common.h"
#include "ui_rs485_state.h"
#include "rs485_tx.h"
#include "esp_log.h"

static lv_obj_t * btn_relay1 = NULL;
static lv_obj_t * btn_relay2 = NULL;
static lv_timer_t * manual_poll_timer = NULL;

/* ================= COLORS ================= */

#define COLOR_LABEL   lv_color_hex(0x0E6BB8)
#define COLOR_CARD_BG   lv_color_hex(0xD9ECF5)
#define COLOR_PILL_OFF  lv_color_hex(0x475569)
#define COLOR_PILL_ON   lv_color_hex(0x22C55E)
#define COLOR_SUBTEXT   lv_color_hex(0x9CA3AF)
#define COLOR_TEXT_DARK  lv_color_hex(0x1F2A33)


/* ================= EVENTS ================= */
static void relay_btn_set(lv_obj_t *btn, bool on)
{
    if (!btn) return;

    lv_obj_t *lbl = lv_obj_get_child(btn, 0);

    if (on) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(lbl, "ON");
        lv_obj_set_style_bg_color(btn, COLOR_PILL_ON, 0);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(lbl, "OFF");
        lv_obj_set_style_bg_color(btn, COLOR_PILL_OFF, 0);
    }
}

static void relay_btn_event_cb(lv_event_t * e)
{
    ESP_LOGI("UI_BTN", "Manual button pressed");

    lv_obj_t * btn = lv_event_get_target(e);
    bool on = !lv_obj_has_state(btn, LV_STATE_CHECKED);

    relay_btn_set(btn, on);

    if (btn == btn_relay1) {
        rs485_send_cmd(1, on);
    } else if (btn == btn_relay2) {
        rs485_send_cmd(2, on);
    }
}


static void ui_rs485_poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    if (!btn_relay1 || !btn_relay2) return;
    if (!lv_obj_is_valid(btn_relay1) || !lv_obj_is_valid(btn_relay2)) return;
    if (!g_rs485_relay_state.valid) return;

    relay_btn_set(btn_relay1, g_rs485_relay_state.relay1);
    relay_btn_set(btn_relay2, g_rs485_relay_state.relay2);
}

/* Back destination */
void ui_menu_screen_create(void);


static void back_to_main(void)
{
    ui_menu_screen_create();
}

static lv_obj_t * create_relay_card(
    lv_obj_t * parent,
    int x, int y,
    const char * title,
    const char * subtitle)
{
    /* Card */
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 520, 100);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t * lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_DARK, 0);
    lv_obj_set_pos(lbl_title, 20, 15);

    /* Subtitle */
    lv_obj_t * lbl_sub = lv_label_create(card);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sub, COLOR_SUBTEXT, 0);
    lv_obj_set_pos(lbl_sub, 20, 45);

   /* OFF pill button */
    lv_obj_t * btn = lv_btn_create(card);
    lv_obj_set_size(btn, 110, 48);
    lv_obj_set_style_radius(btn, 24, 0);
    lv_obj_set_style_bg_color(btn, COLOR_PILL_OFF, 0);
    lv_obj_set_pos(btn, 300, 21);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(btn, relay_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_btn = lv_label_create(btn);
    lv_label_set_text(lbl_btn, "OFF");
    lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_btn, lv_color_white(), 0);
    lv_obj_center(lbl_btn);

    return btn; /* return button if you want to attach events */
}

/* ================= SCREEN ================= */

void ui_manual_screen_init(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    ui_common_load_screen(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Common header + back button */
    ui_common_create(scr);
    ui_common_set_back_cb(back_to_main);
    lv_label_set_text(ui_label_title, "Manual Mode");

    /* ================= RELAY CARDS ================= */

    btn_relay1 = create_relay_card(
        scr, 140, 140, "Indicator 1", "Main Load"
    );

    btn_relay2 = create_relay_card(
        scr, 140, 270, "Indicator 2", "Auxiliary Load"
    );

    if (manual_poll_timer) {
        lv_timer_del(manual_poll_timer);
        manual_poll_timer = NULL;
    }
    manual_poll_timer = lv_timer_create(ui_rs485_poll_cb, 200, NULL);

}

