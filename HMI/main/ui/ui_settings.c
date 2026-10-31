#include "lvgl.h"
#include "ui_common.h"

/* External */
void ui_dynamic_edit_screen_init(uint8_t pr_id);
void ui_menu_screen_create(void);

/* =======================
 * Colors
 * =======================*/
#define COLOR_EDIT_BTN   lv_color_hex(0xF59E0B)
#define COLOR_EDIT_TEXT  lv_color_hex(0x1F2A33)

/* =======================
 * Label handles (editable text)
 * =======================*/
static lv_obj_t * pr_labels[4];
static bool s_settings_nav_in_progress = false;

/* =======================
 * Zoom animation
 * =======================*/
static void anim_zoom_cb(void * obj, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, v, 0);
}

static void btn_touch_anim(lv_obj_t * btn, int zoom)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn);
    lv_anim_set_values(&a, 256, zoom);
    lv_anim_set_time(&a, 120);
    lv_anim_set_exec_cb(&a, anim_zoom_cb);
    lv_anim_start(&a);
}

/* =======================
 * Back -> MAIN
 * =======================*/
static void back_to_main(void)
{
    ui_menu_screen_create();
}

static void open_dynamic_edit_async(void * user_data)
{
    uint8_t pr_id = (uint8_t)(uintptr_t)user_data;
    ui_dynamic_edit_screen_init(pr_id);
    s_settings_nav_in_progress = false;
}

/* =======================
 * Program button events
 * =======================*/
static void event_edit_pr_pressed(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        btn_touch_anim(lv_event_get_target(e), 235);
    }
}

static void event_edit_pr_released(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        btn_touch_anim(lv_event_get_target(e), 256);
    }
}

static void event_edit_pr_clicked(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_settings_nav_in_progress) {
        return;
    }

    lv_obj_t * btn = lv_event_get_target(e);
    uint8_t pr_id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    s_settings_nav_in_progress = true;

    lv_anim_del(btn, anim_zoom_cb);
    lv_obj_set_style_transform_zoom(btn, 256, 0);
    lv_obj_add_state(btn, LV_STATE_DISABLED);

    lv_async_call(open_dynamic_edit_async, (void *)(uintptr_t)pr_id);
}

/* =======================
 * Screen init
 * =======================*/
void ui_edit_program_screen_init(void)
{
    s_settings_nav_in_progress = false;

    lv_obj_t * scr = lv_obj_create(NULL);
    ui_common_load_screen(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_common_create(scr);
    ui_common_set_back_cb(back_to_main);

    /* Header positions */
    lv_obj_align(ui_btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_align(ui_label_date, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_align(ui_label_time, LV_ALIGN_TOP_RIGHT, -20, 30);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_RIGHT, -20, 52);

    /* Page title */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "Edit Program");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    /* Grid */
    lv_obj_t * grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 520, 260);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col[] = {240, 240, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row[] = {90, 90, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col, row);

    /* Program buttons */
    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t * btn = lv_btn_create(grid);
        lv_obj_set_size(btn, 220, 70);
        lv_obj_set_style_bg_color(btn, COLOR_EDIT_BTN, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 14, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_grid_cell(btn,
            LV_GRID_ALIGN_CENTER, i % 2, 1,
            LV_GRID_ALIGN_CENTER, i / 2, 1);

        lv_obj_add_event_cb(btn, event_edit_pr_pressed, LV_EVENT_PRESSED, (void *)(uintptr_t)(i + 1));
        lv_obj_add_event_cb(btn, event_edit_pr_released, LV_EVENT_RELEASED, (void *)(uintptr_t)(i + 1));
        lv_obj_add_event_cb(btn, event_edit_pr_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));

        pr_labels[i] = lv_label_create(btn);
        char txt[8];
        lv_snprintf(txt, sizeof(txt), "PR-%d", i + 1);
        lv_label_set_text(pr_labels[i], txt);
        lv_obj_center(pr_labels[i]);
        lv_obj_set_style_text_font(pr_labels[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pr_labels[i], COLOR_EDIT_TEXT, 0);
    }
}
