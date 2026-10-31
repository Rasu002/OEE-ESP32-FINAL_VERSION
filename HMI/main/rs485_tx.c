#include "driver/uart.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define UART_PORT_NUM UART_NUM_2

static const char *TAG = "RS485_TX";

void rs485_send_cmd(int relay, bool on)
{
    ESP_LOGI(TAG, "send_cmd called: relay=%d state=%d", relay, on);

    char msg[64];
    snprintf(msg, sizeof(msg),
        "{\"cmd\":\"relay\",\"id\":%d,\"state\":%s}\n",
        relay,
        on ? "true" : "false"
    );

    int ret = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    if (ret < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
    } else {
        ESP_LOGI(TAG, "TX -> %s", msg);
    }
}

void rs485_request_oee_date(const char *date_iso)
{
    char msg[96];

    if (date_iso && date_iso[0] != '\0') {
        snprintf(msg, sizeof(msg),
            "{\"cmd\":\"oee_hourly\",\"date\":\"%s\"}\n",
            date_iso
        );
    } else {
        snprintf(msg, sizeof(msg),
            "{\"cmd\":\"oee_hourly\"}\n"
        );
    }

    int ret = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    if (ret < 0) {
        ESP_LOGE(TAG, "OEE request TX failed");
    } else {
        ESP_LOGI(TAG, "TX -> %s", msg);
    }
}

void rs485_send_setting_limit(int seconds)
{
    char msg[64];
    snprintf(msg, sizeof(msg),
        "{\"cmd\":\"set_setting_time\",\"sec\":%d}\n",
        seconds
    );

    int ret = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    if (ret < 0) {
        ESP_LOGE(TAG, "send_setting_limit failed");
    } else {
        ESP_LOGI(TAG, "TX -> %s", msg);
    }
}


void rs485_request_time(void)
{
    const char *msg = "{\"cmd\":\"get_time\"}\n";
    int ret = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    if (ret >= 0) {
        ESP_LOGI(TAG, "TX -> %s", msg);
    }
}

void rs485_send_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec)
{
    char msg[96];
    snprintf(msg, sizeof(msg),
        "{\"cmd\":\"set_time\",\"year\":%04d,\"month\":%02d,\"day\":%02d,\"hour\":%02d,\"min\":%02d,\"sec\":%02d}\n",
        year, month, day, hour, min, sec
    );
    int ret = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    if (ret >= 0) {
        ESP_LOGI(TAG, "TX -> %s", msg);
    }
}
