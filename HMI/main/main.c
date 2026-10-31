#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "waveshare_rgb_lcd_port.h"
#include "lvgl.h"    // Primary UI Library: LVGL (Light and Versatile Graphics Library)
#include "ui/ui_common.h"
#include "ui_rs485_state.h"
#include "rs485_tx.h"

#define UART_PORT_NUM UART_NUM_2
#define TXD_PIN 16
#define RXD_PIN 15
#define RTS_PIN 14
#define BUF_SIZE 256

static const char *TAG_1 = "RS485_MAIN";

/* Forward declaration */
void rs485_handle_json(const char *json);
void ui_welcome_screen_create(void);

void rs485_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, RTS_PIN, UART_PIN_NO_CHANGE);
    uart_set_mode(UART_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);

    ESP_LOGI(TAG_1, "RS485 initialized on UART2");
}

static void rs485_rx_task(void *arg)
{
    uint8_t rx_buf[BUF_SIZE];
    char json_buf[BUF_SIZE];
    int json_len = 0;

    ESP_LOGI(TAG_1, "RS485 RX task started");
    vTaskDelay(pdMS_TO_TICKS(500));
    rs485_request_time();

    while (1) {
        int len = uart_read_bytes(
            UART_PORT_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));

        for (int i = 0; i < len; i++) {
            char ch = (char)rx_buf[i];

            if (ch == '\n') {
                json_buf[json_len] = '\0';
                json_len = 0;

                ESP_LOGI(TAG_1, "RX RAW: %s", json_buf);
                rs485_handle_json(json_buf);

            } else if (json_len < BUF_SIZE - 1) {
                json_buf[json_len++] = ch;
            } else {
                json_len = 0;
            }
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();

    rs485_init();

    xTaskCreate(
        rs485_rx_task,
        "rs485_rx_task",
        4096,
        NULL,
        10,
        NULL
    );

    if (lvgl_port_lock(-1)) {
        ui_welcome_screen_create();
        lvgl_port_unlock();
    }
}
