#include <string.h>
#include "esp_log.h"
#include "ui_rs485_state.h"

#include "cJSON.h"   // make sure cJSON is in your display project too

CsvRow today_csv[MAX_CSV_ROWS] = {0};
int today_csv_count = 0;

static const char *TAG = "ui_rs485_state";

rs485_relay_state_t g_rs485_relay_state = {
    .relay1 = false,
    .relay2 = false,
    .valid  = false
};

rs485_oee_graph_state_t g_rs485_oee_graph_state = {
    .valid = false,
    .has_data = false,
    .update_seq = 0,
    .date = {0},
    .run = {0},
    .idle = {0},
    .error = {0},
    .setting = {0},
    .off = {0}
};

volatile int g_rs485_setting_ack_sec = 0;

rs485_minute_state_t g_rs485_minute_state = {
    .count      = 0,
    .head       = 0,
    .update_seq = 0,
    .valid      = false
};





rs485_time_state_t g_rs485_time_state = {
    .year       = 2026,
    .month      = 1,
    .day        = 1,
    .hour       = 0,
    .minute     = 0,
    .second     = 0,
    .valid      = false,
    .update_seq = 0
};
