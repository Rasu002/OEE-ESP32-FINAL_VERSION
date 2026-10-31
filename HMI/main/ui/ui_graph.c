#include "ui_common.h"
#include "ui_rs485_state.h"
#include "ui_minute.h"

static lv_timer_t * graph_poll_timer = NULL;
static uint32_t last_update_seq = 0;
static uint16_t last_max_y = 0;

/* ================= CONFIG ================= */
#define TOTAL_HOURS     24      // total stored hours
#define VISIBLE_HOURS   6       // hours visible on screen
#define HOUR_WIDTH      120     // pixels per hour
#define MAX_Y_VALUE     100

static bool show_run     = true;
static bool show_idle    = true;
static bool show_error   = true;
static bool show_setting = true;

#define Y_LABEL_COUNT 6

/* ================= DATA BUFFERS ================= */
static uint16_t run_buf[TOTAL_HOURS];
static uint16_t idle_buf[TOTAL_HOURS];
static uint16_t err_buf[TOTAL_HOURS];
static uint16_t setting_buf[TOTAL_HOURS];

/* Last rendered values to avoid redundant invalidation */
static uint16_t last_run_drawn[TOTAL_HOURS];
static uint16_t last_idle_drawn[TOTAL_HOURS];
static uint16_t last_err_drawn[TOTAL_HOURS];
static uint16_t last_setting_drawn[TOTAL_HOURS];

#define X_LABEL_COUNT   TOTAL_HOURS
static lv_obj_t * x_label_cont;
static lv_obj_t * x_labels[X_LABEL_COUNT];

static lv_obj_t * y_label_cont;
static lv_obj_t * y_labels[Y_LABEL_COUNT];

// ===== UI NAVIGATION ADD-ON =====
static lv_obj_t *ui_graph_screen_ref = NULL;

static int data_head  = 0;   // circular write index
static int data_count = 0;   // max = TOTAL_HOURS

/* ================= UI OBJECTS ================= */
static lv_obj_t * chart;
static lv_obj_t * graph_cont;
static lv_chart_series_t * s_run;
static lv_chart_series_t * s_idle;
static lv_chart_series_t * s_error;
static lv_chart_series_t * s_setting;

/* ================= FORWARD DECLARATIONS ================= */
static void update_chart_window(void);
static void chart_draw_event_cb(lv_event_t * e);
static void rs485_graph_poll_cb(lv_timer_t * t);
static lv_obj_t * create_legend_toggle(lv_obj_t * parent,
                                       int x, int y,
                                       int w,
                                       lv_color_t color,
                                       const char * text,
                                       bool * flag);

static void legend_toggle_cb(lv_event_t * e);

/* ================= NAVIGATION ================= */
static void back_to_welcome(void)
{
    if (graph_poll_timer) {
        lv_timer_del(graph_poll_timer);
        graph_poll_timer = NULL;
    }
    ui_welcome_screen_create();
}

/* ================= DATA PUSH (1 HOUR DATA) ================= */
void ui_graph_push_data(uint16_t run,
                        uint16_t idle,
                        uint16_t err,
                        uint16_t setting)
{
    run_buf[data_head]     = run;
    idle_buf[data_head]    = idle;
    err_buf[data_head]     = err;
    setting_buf[data_head] = setting;

    data_head = (data_head + 1) % TOTAL_HOURS;

    if (data_count < TOTAL_HOURS)
        data_count++;

    update_chart_window();
}

static void update_x_axis_labels(void)
{
    for (int i = 0; i < TOTAL_HOURS; i++) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%02d:00", i); // i = actual hour 0-23
        lv_label_set_text(x_labels[i], buf);
    }
}

static void open_new_page_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    /* stop graph poll before leaving so it doesn't update deleted widgets */
    if (graph_poll_timer) {
        lv_timer_del(graph_poll_timer);
        graph_poll_timer = NULL;
    }
    ui_minute_screen_create(ui_graph_screen_ref);
}

/* ================= SCREEN ================= */
void ui_graph_screen_create(void)
{
    // Reset all statics on every screen open
    last_update_seq = 0;
    last_max_y      = 0;
    data_head       = 0;
    data_count      = 0;
    memset(run_buf,     0, sizeof(run_buf));
    memset(idle_buf,    0, sizeof(idle_buf));
    memset(err_buf,     0, sizeof(err_buf));
    memset(setting_buf, 0, sizeof(setting_buf));
    memset(last_run_drawn,     0xFF, sizeof(last_run_drawn));
    memset(last_idle_drawn,    0xFF, sizeof(last_idle_drawn));
    memset(last_err_drawn,     0xFF, sizeof(last_err_drawn));
    memset(last_setting_drawn, 0xFF, sizeof(last_setting_drawn));

    lv_obj_t * scr = lv_obj_create(NULL);
    ui_graph_screen_ref = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_common_load_screen(scr);

    ui_common_create(scr);
    ui_common_set_back_cb(back_to_welcome);
    lv_label_set_text(ui_label_title, "GRAPH DATA");

    /* ===== Legend ===== */
    lv_obj_t * btn_run =
        create_legend_toggle(scr, 20, 430, 115,
            lv_color_hex(0x22C55E), "RUN", &show_run);

    lv_obj_t * btn_idle =
        create_legend_toggle(scr, 145, 430, 115,
            lv_color_hex(0x3B82F6), "IDLE", &show_idle);

    lv_obj_t * btn_error =
        create_legend_toggle(scr, 270, 430, 115,
            lv_color_hex(0xEF4444), "ERROR", &show_error);

    lv_obj_t * btn_setting =
        create_legend_toggle(scr, 395, 430, 125,
            lv_color_hex(0xE67E22), "SETTING", &show_setting);

    /* Attach toggle callbacks */
    lv_obj_add_event_cb(btn_run,     legend_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(btn_idle,    legend_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(btn_error,   legend_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(btn_setting, legend_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ===== Graph scroll container ===== */
    graph_cont = lv_obj_create(scr);
    lv_obj_set_pos(graph_cont, 60, 80);
    lv_obj_set_size(graph_cont, VISIBLE_HOURS * HOUR_WIDTH, 340);
    lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(graph_cont, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(graph_cont, LV_SCROLLBAR_MODE_OFF);

    /* ===== Chart inside container ===== */
    chart = lv_chart_create(graph_cont);
    lv_obj_set_size(chart, TOTAL_HOURS * HOUR_WIDTH, 260);
    lv_obj_align(chart, LV_ALIGN_LEFT_MID, 0, -5);

    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, MAX_Y_VALUE);
    lv_chart_set_point_count(chart, TOTAL_HOURS);

    s_run     = lv_chart_add_series(chart, lv_color_hex(0x22C55E), LV_CHART_AXIS_PRIMARY_Y);
    s_idle    = lv_chart_add_series(chart, lv_color_hex(0x3B82F6), LV_CHART_AXIS_PRIMARY_Y);
    s_error   = lv_chart_add_series(chart, lv_color_hex(0xEF4444), LV_CHART_AXIS_PRIMARY_Y);
    s_setting = lv_chart_add_series(chart, lv_color_hex(0xE67E22), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    /* ===== X-axis label container ===== */
    x_label_cont = lv_obj_create(graph_cont);
    lv_obj_set_size(x_label_cont, TOTAL_HOURS * HOUR_WIDTH, 40);
    lv_obj_align_to(x_label_cont, chart, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lv_obj_clear_flag(x_label_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(x_label_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(x_label_cont, 0, 0);

    /* Create empty labels */
    for (int i = 0; i < TOTAL_HOURS; i++) {
        x_labels[i] = lv_label_create(x_label_cont);
        lv_label_set_text(x_labels[i], "--");
        lv_obj_set_width(x_labels[i], HOUR_WIDTH);
        lv_obj_set_pos(x_labels[i], i * HOUR_WIDTH, 0);
        lv_obj_set_style_text_align(x_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    update_x_axis_labels();

    y_label_cont = lv_obj_create(scr);
    lv_obj_set_size(y_label_cont, 50, 310);
    lv_obj_set_pos(y_label_cont, 5, 80);

    lv_obj_clear_flag(y_label_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(y_label_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(y_label_cont, 0, 0);

    for (int i = 0; i < Y_LABEL_COUNT; i++) {
        y_labels[i] = lv_label_create(y_label_cont);
        lv_label_set_text(y_labels[i], "0");
        lv_obj_set_style_text_align(y_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
    }

    if (graph_poll_timer) {
        lv_timer_del(graph_poll_timer);
        graph_poll_timer = NULL;
    }

    graph_poll_timer = lv_timer_create(rs485_graph_poll_cb, 1000, NULL);
    lv_timer_ready(graph_poll_timer);

    /* ===== NEXT PAGE BUTTON ===== */
    lv_obj_t *btn_next = lv_btn_create(scr);
    lv_obj_set_size(btn_next, 100, 36);
    lv_obj_set_pos(btn_next, 680, 430);

    lv_obj_t *lbl = lv_label_create(btn_next);
    lv_label_set_text(lbl, "Next");
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn_next, open_new_page_event_cb, LV_EVENT_CLICKED, NULL);
}

static void rs485_graph_poll_cb(lv_timer_t * t)
{
    LV_UNUSED(t);

    if (!g_rs485_oee_graph_state.valid)
        return;

    if (g_rs485_oee_graph_state.update_seq == last_update_seq)
        return;

    last_update_seq = g_rs485_oee_graph_state.update_seq;

    // Write all 24 hours directly into buffers by index
    for (int i = 0; i < TOTAL_HOURS; i++) {
        run_buf[i]     = g_rs485_oee_graph_state.run[i];
        idle_buf[i]    = g_rs485_oee_graph_state.idle[i];
        err_buf[i]     = g_rs485_oee_graph_state.error[i];
        setting_buf[i] = g_rs485_oee_graph_state.setting[i];
    }

    data_head  = 0;
    data_count = TOTAL_HOURS;

    update_chart_window();
}

/* ================= LEGEND ITEM  ================= */
static lv_obj_t * create_legend_toggle(lv_obj_t * parent,
                                       int x, int y,
                                       int w,
                                       lv_color_t color,
                                       const char * text,
                                       bool * flag)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 36);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_state(btn, *flag ? LV_STATE_CHECKED : 0);

    /* OFF state (unselected) */
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    /* ON state (selected / glow) */
    lv_obj_set_style_bg_color(btn, color, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_color(btn, color, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(btn, 20, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_CHECKED);

    /* Label */
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    /* Store flag pointer */
    lv_obj_set_user_data(btn, flag);

    return btn;
}

static void legend_toggle_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    bool * flag = lv_obj_get_user_data(btn);

    *flag = lv_obj_has_state(btn, LV_STATE_CHECKED);

    update_chart_window();
}

static void update_y_axis_labels(uint16_t max_y)
{
    if (max_y == 0) max_y = 1;

    for (int i = 0; i < Y_LABEL_COUNT; i++) {

        /* Logical index from bottom */
        int li = (Y_LABEL_COUNT - 1) - i;

        /* Value: 0 at bottom -> max_y at top */
        uint16_t val = (max_y * i) / (Y_LABEL_COUNT - 1);

        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", val);
        lv_label_set_text(y_labels[li], buf);

        /* Position bottom -> top */
        lv_coord_t y =
            260 - ((260 * i) / (Y_LABEL_COUNT - 1)) - 8;

        lv_obj_set_pos(y_labels[li], 0, y);
    }
}

/* ================= UPDATE GRAPH ================= */
static void update_chart_window(void)
{
    uint16_t max_y = 1;
    bool any_point_changed = false;

    for (int i = 0; i < TOTAL_HOURS; i++) {

        int idx = (data_head + i) % TOTAL_HOURS;

        if (i >= data_count) {
            if (last_run_drawn[i] != 0) {
                lv_chart_set_value_by_id(chart, s_run, i, 0);
                last_run_drawn[i] = 0;
                any_point_changed = true;
            }
            if (last_idle_drawn[i] != 0) {
                lv_chart_set_value_by_id(chart, s_idle, i, 0);
                last_idle_drawn[i] = 0;
                any_point_changed = true;
            }
            if (last_err_drawn[i] != 0) {
                lv_chart_set_value_by_id(chart, s_error, i, 0);
                last_err_drawn[i] = 0;
                any_point_changed = true;
            }
            if (last_setting_drawn[i] != 0) {
                lv_chart_set_value_by_id(chart, s_setting, i, 0);
                last_setting_drawn[i] = 0;
                any_point_changed = true;
            }
            continue;
        }

        /* Apply visibility filtering */
        uint16_t r  = show_run     ? run_buf[idx]     : 0;
        uint16_t id = show_idle    ? idle_buf[idx]    : 0;
        uint16_t er = show_error   ? err_buf[idx]     : 0;
        uint16_t st = show_setting ? setting_buf[idx] : 0;

        if (last_run_drawn[i] != r) {
            lv_chart_set_value_by_id(chart, s_run, i, r);
            last_run_drawn[i] = r;
            any_point_changed = true;
        }
        if (last_idle_drawn[i] != id) {
            lv_chart_set_value_by_id(chart, s_idle, i, id);
            last_idle_drawn[i] = id;
            any_point_changed = true;
        }
        if (last_err_drawn[i] != er) {
            lv_chart_set_value_by_id(chart, s_error, i, er);
            last_err_drawn[i] = er;
            any_point_changed = true;
        }
        if (last_setting_drawn[i] != st) {
            lv_chart_set_value_by_id(chart, s_setting, i, st);
            last_setting_drawn[i] = st;
            any_point_changed = true;
        }

        /* Auto Y-axis scale (only visible series) */
        if (show_run     && r  > max_y) max_y = r;
        if (show_idle    && id > max_y) max_y = id;
        if (show_error   && er > max_y) max_y = er;
        if (show_setting && st > max_y) max_y = st;
    }

    /* Add headroom */
    max_y = (max_y * 12) / 10;
    if (max_y < 10) max_y = 10;

    const bool range_changed = (max_y != last_max_y);
    if (range_changed) {
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_y);
        last_max_y = max_y;
    }

    if (any_point_changed || range_changed) {
        lv_chart_refresh(chart);
    }

    /* X-axis labels are static (00:00 .. 23:00), update only on screen create */
    if (range_changed) {
        update_y_axis_labels(max_y);
    }
}

static void chart_draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part != LV_PART_ITEMS)
        return;

    /* Show popup only when pressed */
    if (!lv_obj_has_state(obj, LV_STATE_PRESSED))
        return;

    const lv_chart_series_t * ser = dsc->sub_part_ptr;
    int id = dsc->id;

    if (id < 0)
        return;

    lv_coord_t value = ser->y_points[id];
    if (value <= 0)
        return;

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", value);

    /* Popup background */
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa   = LV_OPA_80;
    rect_dsc.radius   = 2;

    lv_area_t a;
    a.x1 = dsc->draw_area->x1 - 12;
    a.x2 = dsc->draw_area->x2 + 12;
    a.y1 = dsc->draw_area->y1 - 26;
    a.y2 = dsc->draw_area->y1 - 6;

    lv_draw_rect(dsc->draw_ctx, &rect_dsc, &a);

    /* Popup text */
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();

    lv_draw_label(dsc->draw_ctx, &label_dsc, &a, buf, NULL);
}
