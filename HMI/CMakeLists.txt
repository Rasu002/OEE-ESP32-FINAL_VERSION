# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/managed_components"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(lvgl_porting)

idf_component_get_property(ESP_LCD_LIB esp_lcd COMPONENT_LIB)
set_source_files_properties(
    "$ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
    TARGET_DIRECTORY ${ESP_LCD_LIB}
    PROPERTIES COMPILE_OPTIONS "-O0"
)
    