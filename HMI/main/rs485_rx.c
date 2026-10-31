#include "ui_minute.h"
// rs498_rx
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "cJSON.h"
#include "ui_rs485_state.h"

#define TAG "RS485_RX"

static bool parse_csv_timestamp(const char *timestamp, char *date_out, size_t date_out_size, int *hour_out)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;

    if (!timestamp || !date_out || date_out_size < sizeof("YYYY-MM-DD") || !hour_out) {
        return false;
    }

    if (sscanf(timestamp, "%d-%d-%d %d", &year, &month, &day, &hour) != 4 &&
        sscanf(timestamp, "%d-%d-%dT%d", &year, &month, &day, &hour) != 4) {
        return false;
    }

    if (hour < 0 || hour >= RS485_OEE_HOURS) {
        return false;
    }

    snprintf(date_out, date_out_size, "%04d-%02d-%02d", year, month, day);
    *hour_out = hour;
    return true;
}

static uint16_t clamp_to_u16(float value)
{
    if (value < 0.0f) {
        return 0;
    }
    if (value > 65535.0f) {
        return 65535;
    }
    return (uint16_t)(value + 0.5f);
}

static void update_oee_graph_state(const char *timestamp, float run_value, float idle_value, float error_value, float setting_value)
{
    char date_buf[16];
    int hour = 0;

    if (!parse_csv_timestamp(timestamp, date_buf, sizeof(date_buf), &hour)) {
        ESP_LOGW("RS485_RX", "Failed to parse timestamp: %s", timestamp);
        return;
    }

    // Only wipe if it's a genuinely new date (different day)
    if (g_rs485_oee_graph_state.valid &&
        strncmp(g_rs485_oee_graph_state.date, date_buf,
                sizeof(g_rs485_oee_graph_state.date)) != 0) {
        memset(&g_rs485_oee_graph_state, 0, sizeof(g_rs485_oee_graph_state));
    }

    g_rs485_oee_graph_state.valid = true;
    strncpy(g_rs485_oee_graph_state.date, date_buf,
            sizeof(g_rs485_oee_graph_state.date) - 1);

    g_rs485_oee_graph_state.run[hour]     = clamp_to_u16(run_value);
    g_rs485_oee_graph_state.idle[hour]    = clamp_to_u16(idle_value);
    g_rs485_oee_graph_state.error[hour]   = clamp_to_u16(error_value);
    g_rs485_oee_graph_state.setting[hour] = clamp_to_u16(setting_value);
    g_rs485_oee_graph_state.off[hour]     = 0;
    g_rs485_oee_graph_state.has_data      = true;
}


static void handle_csv_row(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *ts      = cJSON_GetObjectItem(root, "ts");
    cJSON *run     = cJSON_GetObjectItem(root, "run");
    cJSON *idle    = cJSON_GetObjectItem(root, "idle");
    cJSON *error   = cJSON_GetObjectItem(root, "error");
    cJSON *setting = cJSON_GetObjectItem(root, "setting");

    if (cJSON_IsString(ts) &&
        cJSON_IsNumber(run) &&
        cJSON_IsNumber(idle) &&
        cJSON_IsNumber(error))
    {
        float set_val = (setting && cJSON_IsNumber(setting)) ? (float)setting->valuedouble : 0.0f;

        // Update existing row if timestamp matches
        bool updated = false;
        for (int i = 0; i < today_csv_count; i++) {
            if (strcmp(today_csv[i].timestamp, ts->valuestring) == 0) {
                today_csv[i].run     = (float)run->valuedouble;
                today_csv[i].idle    = (float)idle->valuedouble;
                today_csv[i].error   = (float)error->valuedouble;
                today_csv[i].setting = set_val;
                updated = true;
                break;
            }
        }

        // Append new row
        if (!updated && today_csv_count < MAX_CSV_ROWS) {
            strncpy(today_csv[today_csv_count].timestamp,
                    ts->valuestring,
                    sizeof(today_csv[0].timestamp) - 1);
            today_csv[today_csv_count].run     = (float)run->valuedouble;
            today_csv[today_csv_count].idle    = (float)idle->valuedouble;
            today_csv[today_csv_count].error   = (float)error->valuedouble;
            today_csv[today_csv_count].setting = set_val;
            today_csv_count++;
        }

        ESP_LOGI(TAG, "[CSV] %s run=%.1f idle=%.1f error=%.1f setting=%.1f (rows=%d)",
                 ts->valuestring,
                 (float)run->valuedouble,
                 (float)idle->valuedouble,
                 (float)error->valuedouble,
                 set_val,
                 today_csv_count);

        update_oee_graph_state(
            ts->valuestring,
            (float)run->valuedouble,
            (float)idle->valuedouble,
            (float)error->valuedouble,
            set_val
        );

        // Call your LVGL update here if needed:
        // ui_update_csv_table(today_csv, today_csv_count);
    }

    cJSON_Delete(root);
}

// Getter so other UI files can read the CSV data
const CsvRow *rs485_get_csv_rows(int *count_out)
{
    if (count_out) *count_out = today_csv_count;
    return today_csv;
}
void rs485_handle_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");

    if (cJSON_IsString(type)) {

        if (strcmp(type->valuestring, "csv") == 0) {
            cJSON_Delete(root);
            handle_csv_row(json);
            return;
        }

        if (strcmp(type->valuestring, "time") == 0) {
            cJSON *yr = cJSON_GetObjectItem(root, "year");
            cJSON *mo = cJSON_GetObjectItem(root, "month");
            cJSON *dy = cJSON_GetObjectItem(root, "day");
            cJSON *hr = cJSON_GetObjectItem(root, "hour");
            cJSON *mn = cJSON_GetObjectItem(root, "min");
            cJSON *sc = cJSON_GetObjectItem(root, "sec");

            if (cJSON_IsNumber(yr) && cJSON_IsNumber(mo) && cJSON_IsNumber(dy) &&
                cJSON_IsNumber(hr) && cJSON_IsNumber(mn) && cJSON_IsNumber(sc)) {
                g_rs485_time_state.year   = (uint16_t)yr->valueint;
                g_rs485_time_state.month  = (uint8_t)mo->valueint;
                g_rs485_time_state.day    = (uint8_t)dy->valueint;
                g_rs485_time_state.hour   = (uint8_t)hr->valueint;
                g_rs485_time_state.minute = (uint8_t)mn->valueint;
                g_rs485_time_state.second = (uint8_t)sc->valueint;
                g_rs485_time_state.valid  = true;
                g_rs485_time_state.update_seq++;
                ESP_LOGI(TAG, "[TIME] Sync: %02d/%02d/%04d %02d:%02d:%02d",
                         g_rs485_time_state.day, g_rs485_time_state.month, g_rs485_time_state.year,
                         g_rs485_time_state.hour, g_rs485_time_state.minute, g_rs485_time_state.second);
            }
            cJSON_Delete(root);
            return;
        }
        if (strcmp(type->valuestring, "minute") == 0) {
            cJSON *h   = cJSON_GetObjectItem(root, "hour");
            cJSON *mn  = cJSON_GetObjectItem(root, "minute");
            cJSON *run = cJSON_GetObjectItem(root, "run");
            cJSON *idl = cJSON_GetObjectItem(root, "idle");
            cJSON *err = cJSON_GetObjectItem(root, "error");
            cJSON *set = cJSON_GetObjectItem(root, "setting");
            cJSON *set_lim = cJSON_GetObjectItem(root, "set_limit");
            if (set_lim && cJSON_IsNumber(set_lim) && set_lim->valueint > 0) {
                int mins = set_lim->valueint / 60;
                if (mins >= 1 && mins <= 60) {
                    ui_set_active_setting_minutes(mins);
                }
            }

            if (cJSON_IsNumber(h)   && cJSON_IsNumber(mn) &&
                cJSON_IsNumber(run) && cJSON_IsNumber(idl) &&
                cJSON_IsNumber(err))
            {
                int idx = g_rs485_minute_state.head;
                g_rs485_minute_state.entries[idx].hour      = (uint8_t)h->valueint;
                g_rs485_minute_state.entries[idx].minute    = (uint8_t)mn->valueint;
                g_rs485_minute_state.entries[idx].run_sec   = (uint16_t)run->valueint;
                g_rs485_minute_state.entries[idx].idle_sec  = (uint16_t)idl->valueint;
                g_rs485_minute_state.entries[idx].error_sec = (uint16_t)err->valueint;
                g_rs485_minute_state.entries[idx].setting_sec = (set && cJSON_IsNumber(set)) ? (uint16_t)set->valueint : 0;

                g_rs485_minute_state.head =
                    (idx + 1) % RS485_MINUTE_SLOTS;
                if (g_rs485_minute_state.count < RS485_MINUTE_SLOTS)
                    g_rs485_minute_state.count++;
                g_rs485_minute_state.valid = true;
                g_rs485_minute_state.update_seq++;

                ESP_LOGI(TAG, "[MIN] %02d:%02d R=%d I=%d E=%d",
                    h->valueint, mn->valueint,
                    run->valueint, idl->valueint, err->valueint);
            }
            cJSON_Delete(root);
            return;
        }

        // All CSV rows received â€” now trigger one graph update
        if (strcmp(type->valuestring, "csv_done") == 0) {
            if (g_rs485_oee_graph_state.has_data) {
                g_rs485_oee_graph_state.update_seq++;  // â† single trigger
                ESP_LOGI("RS485_RX", "CSV batch complete, seq=%lu",
                         (unsigned long)g_rs485_oee_graph_state.update_seq);
            }
            cJSON_Delete(root);
            return;
        }
        if (strcmp(type->valuestring, "set_setting_ack") == 0) {
            cJSON *sec = cJSON_GetObjectItem(root, "sec");
            if (cJSON_IsNumber(sec)) {
                ESP_LOGI(TAG, ">>> ESP32 CONFIRMED & SAVED TO FLASH: %d sec (%d min) <<<",
                         sec->valueint, sec->valueint / 60);
                g_rs485_setting_ack_sec = sec->valueint;
            }
            cJSON_Delete(root);
            return;
        }
    }

    // relay state handling...
    cJSON *r1 = cJSON_GetObjectItem(root, "relay1");
    cJSON *r2 = cJSON_GetObjectItem(root, "relay2");

    if (cJSON_IsBool(r1) && cJSON_IsBool(r2)) {
        g_rs485_relay_state.relay1 = cJSON_IsTrue(r1);
        g_rs485_relay_state.relay2 = cJSON_IsTrue(r2);
        g_rs485_relay_state.valid  = true;
    }

    cJSON_Delete(root);
}



