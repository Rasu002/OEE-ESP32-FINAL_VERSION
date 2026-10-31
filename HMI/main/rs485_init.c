#include "driver/uart.h"
#include "esp_log.h"

#define TAG "RS485_INIT"

#define UART_PORT_NUM UART_NUM_2
#define TXD_PIN 16
#define RXD_PIN 15
#define RTS_PIN 14
#define BUF_SIZE 512

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

    ESP_ERROR_CHECK(uart_driver_install(
        UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));

    ESP_ERROR_CHECK(uart_set_pin(
        UART_PORT_NUM,
        TXD_PIN,
        RXD_PIN,
        RTS_PIN,
        UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(
        uart_set_mode(UART_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX));

    ESP_LOGI(TAG, "RS485 initialized on UART%d", UART_PORT_NUM);
}
