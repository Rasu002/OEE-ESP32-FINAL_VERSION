#include "lvgl.h"
#include "ui_common.h"

/* Go back to MAIN menu */
void ui_menu_screen_create(void);

/* Dynamic screen */
void ui_dynamic_screen_init(uint8_t program_id);

/* =======================
 * Colors
 * =======================*/
#define COLOR_PR_BTN   lv_color_hex(0x2ED14F)
#define COLOR_PR_TEXT  lv_color_hex(0x0F2A18)

static bool s_auto_nav_in_progress = false;

/* =======================
 * Back button handler
 * =======================*/
static void back_to_main(void)
{
    ui_menu_screen_create();
}

static void open_dynamic_async(void * user_data)
{
    uint8_t pr_id = (uint8_t)(uintptr_t)user_data;
    ui_dynamic_screen_init(pr_id);
    s_auto_nav_in_progress = false;
}

/* =======================
 * PR button event
 * =======================*/
static void event_pr_btn(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_auto_nav_in_progress) {
        return;
    }

    s_auto_nav_in_progress = true;
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);

    uint8_t pr_id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_async_call(open_dynamic_async, (void *)(uintptr_t)pr_id);
}

/* =======================
 * AUTO / Select Model screen
 * =======================*/
void ui_auto_screen_init(void)
{
    s_auto_nav_in_progress = false;

    lv_obj_t * screen = lv_obj_create(NULL);
    ui_common_load_screen(screen);

    /* Disable screen scrolling */
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Common header */
    ui_common_create(screen);
    ui_common_set_back_cb(back_to_main);

    /* Header positions */
    lv_obj_align(ui_btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_align(ui_label_date,  LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_align(ui_label_time,  LV_ALIGN_TOP_RIGHT, -20, 30);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_RIGHT, -20, 52);

    /* Page title */
    lv_obj_t * title = lv_label_create(screen);
    lv_label_set_text(title, "Select Model");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x083B4C), 0);

    /* Grid container */
    lv_obj_t * grid = lv_obj_create(screen);
    lv_obj_set_size(grid, 520, 260);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {240, 240, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {90, 90, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    /* PR buttons */
    for (uint8_t i = 1; i <= 4; i++) {
        lv_obj_t * btn = lv_btn_create(grid);
        lv_obj_set_size(btn, 220, 70);
        lv_obj_set_style_bg_color(btn, COLOR_PR_BTN, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 16, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_grid_cell(btn,
            LV_GRID_ALIGN_CENTER, (i - 1) % 2, 1,
            LV_GRID_ALIGN_CENTER, (i - 1) / 2, 1);

        lv_obj_add_event_cb(btn, event_pr_btn, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t * lbl = lv_label_create(btn);
        char txt[8];
        lv_snprintf(txt, sizeof(txt), "PR-%d", i);
        lv_label_set_text(lbl, txt);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, COLOR_PR_TEXT, 0);
    }
}

