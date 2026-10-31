cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# use the custom partition file located in main
# set(PARTITION_TABLE_CUSTOM_CSV "${CMAKE_SOURCE_DIR}/partitions.csv")
project(ESP32)

# workaround for GCC internal compiler error in esp_lcd_panel_rgb.c
# compile that source with no optimization
if(EXISTS "${IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c")
    set_source_files_properties(
        "${IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
        PROPERTIES COMPILE_OPTIONS "-O0"
    )
endif()
