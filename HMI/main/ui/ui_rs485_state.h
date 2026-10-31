// ui_rs485_state.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_CSV_ROWS 24
#define RS485_OEE_HOURS 24

typedef struct {
    char timestamp[32];
    float run;
    float idle;
    float error;
    float setting;
} CsvRow;
const CsvRow *rs485_get_csv_rows(int *count_out);

typedef struct {
    bool relay1;
    bool relay2;
    bool valid;
} rs485_relay_state_t;

typedef struct {
    bool valid;
    bool has_data;
    uint32_t update_seq;
    char date[16];
    uint16_t run[RS485_OEE_HOURS];
    uint16_t idle[RS485_OEE_HOURS];
    uint16_t error[RS485_OEE_HOURS];
    uint16_t setting[RS485_OEE_HOURS];
    uint16_t off[RS485_OEE_HOURS];
} rs485_oee_graph_state_t;

extern CsvRow today_csv[MAX_CSV_ROWS];
extern int today_csv_count;
extern rs485_relay_state_t g_rs485_relay_state;
extern rs485_oee_graph_state_t g_rs485_oee_graph_state;

/* -------- Per-minute data (rolling 60-minute window) -------- */
#define RS485_MINUTE_SLOTS 60

typedef struct {
    uint8_t  hour;
    uint8_t  minute;
    uint16_t run_sec;
    uint16_t idle_sec;
    uint16_t error_sec;
    uint16_t setting_sec;
} rs485_minute_entry_t;

typedef struct {
    rs485_minute_entry_t entries[RS485_MINUTE_SLOTS];
    int      count;       /* valid entries 0-60       */
    int      head;        /* circular write index     */
    uint32_t update_seq;  /* incremented each entry   */
    bool     valid;
} rs485_minute_state_t;

extern rs485_minute_state_t g_rs485_minute_state;

extern volatile int g_rs485_setting_ack_sec;


typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    bool     valid;
    uint32_t update_seq;
} rs485_time_state_t;

extern rs485_time_state_t g_rs485_time_state;
