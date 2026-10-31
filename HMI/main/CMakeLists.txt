idf_component_register(
    SRCS
        "main.c"
        "waveshare_rgb_lcd_port.c"
        "lvgl_port.c"

        # UI
        "ui/ui_common.c"
        "ui/ui_welcome.c"
        "ui/ui_graph.c"
        "ui/ui_minute.c"
        "ui/ui_main.c"
        "ui/ui_manual.c"
        "ui/ui_auto.c"
        "ui/ui_settings.c"
        "ui/ui_dynamic.c"
        "ui/ui_dynamic_edit.c"

        # RS-485 + shared state
        "ui/ui_rs485_state.c"
        "rs485_tx.c"
        "rs485_rx.c"
        "rs485_init.c"

    INCLUDE_DIRS
        "."
        "ui"

    REQUIRES
        cJSON
        driver
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        lvgl__lvgl
        nvs_flash
)

set_source_files_properties(
    $ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c
    PROPERTIES COMPILE_FLAGS "-O0"
)
