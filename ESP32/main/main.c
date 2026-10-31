// main.c
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
// =========================
// ESP-IDF / Project Includes
// =========================
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "PCF85063A.h"
#include "timegm.h"
#include "esp_eth.h"
#include "driver/spi_master.h"
#include "lwip/inet.h"
#include "esp_sntp.h"
#include "freertos/semphr.h"

// =========================
// Project Configuration
// =========================
#define MOUNT_POINT "/sdcard"
// Daily CSV file is created per-day using a fixed machine code + RTC date:
// Example: /sdcard/m01_0112.csv (m01=machine code, 0112=MMDD)
#define MACHINE_CODE_STR "M01"
#define TAG "ESP32_LOGGER"
#define RELAY1_GPIO 6  // DI 3 (RUN State)
#define RELAY2_GPIO 7  // DI 4 (ERROR State)
#define INPUT5_GPIO 8  // DI 5 (Setting_time or IDEL State)
#define USE_REAL_INPUT_PINS 1
#define INPUT1_GPIO RELAY1_GPIO
#define INPUT2_GPIO RELAY2_GPIO
// If inputs are active-low (0 means ON), set to 1.
#define INPUT_ACTIVE_LOW 1
// Internal pull configuration (set according to your wiring)

#define IST_OFFSET_SEC (5*3600 + 30*60)
#define SERVER_URL "http://192.168.137.1:8000/upload_csv"
#define MAX_HTTP_RETRIES 3
#define SD_INIT_MAX_RETRIES 5
#define RTC_DRIFT_THRESHOLD_SEC  10
#define STATE_RUN     1
#define STATE_IDLE    2
#define STATE_ERROR   3
#define STATE_SETTING 4
static volatile  int current_machine_state = -1;
static int last_machine_state = -1;
// =========================
// Global State / Counters
// =========================
static volatile bool relay1_state = false;
static volatile bool relay2_state = false;
static volatile bool sdcard_ini = false;
static bool boot_state_sent = false;
static bool eth_connected = false;
static esp_netif_t *eth_netif = NULL;
SemaphoreHandle_t sd_mutex = NULL;
static SemaphoreHandle_t rtc_mutex = NULL;
static portMUX_TYPE counters_mux = portMUX_INITIALIZER_UNLOCKED;
static int32_t last_sec = -1;
static int32_t last_minute = -1;
static int32_t last_hour = -1;
static int32_t last_day = -1;
static int32_t run_sec = 0;
static int32_t idle_sec = 0;
static int32_t error_sec = 0;
static int32_t setting_sec = 0;
static int32_t run = 0;
static int32_t idle = 0;
static int32_t error = 0;
static int32_t setting = 0;
static int32_t di5_continuous_sec = 0;
static volatile int setting_max_duration_sec = 120;

#define PO_MIN_DURATION_SEC 120

#define PIN_INT    12
#define PIN_MOSI   13
#define PIN_MISO   14
#define PIN_SCLK   15
#define PIN_CS     16
#define PIN_RST    39

static sdmmc_card_t *sd_card = NULL;

QueueHandle_t state_queue;

typedef struct {
    char timestamp[32];
    int run_sec;
    int idle_sec;
    int error_sec;
    int setting_sec;
} LastMinuteData;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    bool valid;
} ServerTime;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int run_sec;
    int idle_sec;
    int error_sec;
    int setting_sec;
} MinuteRS485Data;

QueueHandle_t rs485_minute_queue;

static void rs485_send_time_data(const PCF_DateTime *rtc);
static void rs485_send_time(void);

static LastMinuteData last_minute_data = {0};

static time_t to_epoch(int y, int m, int d, int h, int min, int s);

static inline bool gpio_level_to_state(int raw_level)
{
#if INPUT_ACTIVE_LOW
    return raw_level ? false : true;
#else
    return raw_level ? true : false;
#endif
}

static bool rtc_read(PCF_DateTime *out)
{
    if (rtc_mutex) xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    int r = PCF_GetDateTime(out);
    if (rtc_mutex) xSemaphoreGive(rtc_mutex);
    return (r == 0);
}

static void build_daily_csv_path_from_tm(const struct tm *t, char *path, size_t len)
{
    int mm = t->tm_mon + 1;
    int dd = t->tm_mday;
    snprintf(path, len, "%s/%s_%02d%02d.csv", MOUNT_POINT, MACHINE_CODE_STR, mm, dd);
}

static void build_daily_csv_name_from_tm(const struct tm *t, char *name, size_t len)
{
    int mm = t->tm_mon + 1;
    int dd = t->tm_mday;
    snprintf(name, len, "%s_%02d%02d.csv", MACHINE_CODE_STR, mm, dd);
}

static void get_completed_hour_tm(const PCF_DateTime *rtc, struct tm *out)
{
    struct tm t = {
        .tm_year = (int)rtc->year - 1900,
        .tm_mon = (int)rtc->month - 1,
        .tm_mday = (int)rtc->day,
        .tm_hour = (int)rtc->hour,
        .tm_min = (int)rtc->minute,
        .tm_sec = (int)rtc->second,
    };

    t.tm_hour -= 1;
    mktime(&t);
    *out = t;
}

static void format_rtc_timestamp(const PCF_DateTime *rtc, char *buf, size_t len)
{
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
             (int)rtc->year, (int)rtc->month, (int)rtc->day,
             (int)rtc->hour, (int)rtc->minute, (int)rtc->second);
}


//  Initialize and mount SD card
esp_err_t sdcard_init(void)
{
    esp_err_t ret;
    ESP_LOGI("SDCARD", "Initializing SD card");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = 48;
    slot_config.cmd = 47;
    slot_config.d0  = 45;
    slot_config.width = 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        sd_card = NULL;
        return ret;
    }
    // sd_mutex = xSemaphoreCreateMutex();
    sd_mutex = xSemaphoreCreateRecursiveMutex();
    if (sd_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SD card mutex!");
    }
    ESP_LOGI("SDCARD", "SD card mounted successfully");
    sdcard_ini = true;
    sdmmc_card_print_info(stdout, sd_card);
    return ESP_OK;
}

static esp_err_t send_state_change_to_server(int state);

static void delayed_state_send_task(void *arg)
{
    ESP_LOGI(TAG, "Waiting for machine state to be determined...");

    while (current_machine_state < 0) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    int state = current_machine_state;

    ESP_LOGI(TAG, "Boot state determined → sending to server");

    send_state_change_to_server(state);

    last_machine_state = state;
    boot_state_sent = true;

    vTaskDelete(NULL);
}

// Ethernet (W5500) Initialization
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                break;

            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet Link Down");
                eth_connected = false;
                break;

            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;

            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet Stopped");
                eth_connected = false;
                break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        eth_connected = true;

        ESP_LOGI(TAG, "Scheduling delayed state send (2s)");

        xTaskCreate(delayed_state_send_task, "delayed_state_send", 4096, NULL, 5, NULL);
    }
}

static esp_netif_t *ethernet_init(void)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&cfg);
    // SPI bus configuration for W5500
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = PIN_CS,
        .queue_size = 5,
    };
    gpio_reset_pin(PIN_RST);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 0);

    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);

    w5500_config.int_gpio_num = PIN_INT;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    uint8_t mac_addr[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr("192.168.137.50");
    ip_info.netmask.addr = inet_addr("255.255.255.0");
    ip_info.gw.addr = inet_addr("192.168.137.1");

    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    ESP_LOGI(TAG, "Ethernet initialized with static IP: " IPSTR, IP2STR(&ip_info.ip));

    return eth_netif;
}

static void ensure_file_with_header(const char *path, const char *header)
{
    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
    FILE *f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE("CSV", "Failed to create file: %s (errno=%d: %s)", path, errno, strerror(errno));
            xSemaphoreGiveRecursive(sd_mutex);
            return;
        }
        if (header) fprintf(f, "%s\n", header);
        fclose(f);
        xSemaphoreGiveRecursive(sd_mutex);
        ESP_LOGI("CSV", "Created file: %s", path);
        return;
    }

    // Check if existing file header is missing setting_sec and upgrade it
    char first_line[256] = {0};
    if (header && fgets(first_line, sizeof(first_line), f)) {
        char *nl = strchr(first_line, '\r');
        if (nl) *nl = '\0';
        nl = strchr(first_line, '\n');
        if (nl) *nl = '\0';

        if (strstr(header, "setting_sec") && !strstr(first_line, "setting_sec")) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            (void)fgets(first_line, sizeof(first_line), f); // skip first line
            long data_len = fsize - ftell(f);

            char *data_buf = NULL;
            if (data_len > 0) {
                data_buf = (char *)malloc(data_len + 1);
                if (data_buf) {
                    size_t r = fread(data_buf, 1, data_len, f);
                    data_buf[r] = '\0';
                }
            }
            fclose(f);

            f = fopen(path, "w");
            if (f) {
                fprintf(f, "%s\n", header);
                if (data_buf) {
                    fwrite(data_buf, 1, strlen(data_buf), f);
                    free(data_buf);
                }
                fclose(f);
                ESP_LOGI("CSV", "Updated header for %s to '%s'", path, header);
            } else {
                if (data_buf) free(data_buf);
                ESP_LOGE("CSV", "Failed to rewrite %s for header upgrade", path);
            }
            xSemaphoreGiveRecursive(sd_mutex);
            return;
        }
    }

    fclose(f);
    xSemaphoreGiveRecursive(sd_mutex);
    ESP_LOGI("CSV", "File exists: %s", path);
}

// SNTP
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized via SNTP");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

// Wait for SNTP sync
static void obtain_time(void)
{
    initialize_sntp();
    int retry = 0;
    const int retry_count = 1;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for SNTP time... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "SNTP sync failed - skipping RTC update.");
        return;
    }
    time_t now;
    struct tm timeinfo_utc;
    time(&now);
    gmtime_r(&now, &timeinfo_utc);
    now += IST_OFFSET_SEC;
    struct tm timeinfo_ist;
    gmtime_r(&now, &timeinfo_ist);
    PCF_DateTime newRTC = {
        .second = timeinfo_ist.tm_sec,
        .minute = timeinfo_ist.tm_min,
        .hour   = timeinfo_ist.tm_hour,
        .day    = timeinfo_ist.tm_mday,
        .month  = timeinfo_ist.tm_mon + 1,
        .year   = timeinfo_ist.tm_year + 1900
    };
    // ESP_ERROR_CHECK(i2c_master_driver_initialize());
    // ESP_ERROR_CHECK(PCF_Init());
    if (PCF_SetDateTime(&newRTC) == 0)
        ESP_LOGI(TAG, "RTC updated with IST successfully.");
    else
        ESP_LOGE(TAG, "Failed to update RTC with IST.");
}

// HTTP Server
static esp_err_t upload_file_http(const char *path, const char *alias_name)
{
    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "File %s not found", path);
        xSemaphoreGiveRecursive(sd_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);

    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "No memory to read %s", path);
        xSemaphoreGiveRecursive(sd_mutex);
        return ESP_ERR_NO_MEM;
    }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    xSemaphoreGiveRecursive(sd_mutex);
    char url_with_name[256];
    snprintf(url_with_name, sizeof(url_with_name), "%s?filename=%s", SERVER_URL, alias_name);
    esp_http_client_config_t config = {
        .url = url_with_name,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(buf); ESP_LOGE(TAG, "Failed to init HTTP client"); return ESP_FAIL; }
    esp_http_client_set_header(client, "Content-Type", "text/csv");
    esp_err_t err = esp_http_client_set_post_field(client, buf, sz);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(buf);
        ESP_LOGE(TAG, "Failed to set POST field for %s", path);
        return err;
    }

    ESP_LOGI(TAG, "Connecting to server: %s", url_with_name);
    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Server responded with status=%d", status);
    }
    
    esp_http_client_cleanup(client);
    free(buf);
    if (err == ESP_OK && (status == 200 || status == 201 || status == 204)) {
        ESP_LOGI(TAG, "Uploaded %s successfully", alias_name);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Upload failed for %s, err=%d, status=%d", alias_name, err, status);
        return ESP_FAIL;
    }
}
static ServerTime fetch_time_from_server(void)
{
    ServerTime result = {0};
    result.valid = false;

    static char rx_buf[512];
    static int rx_len = 0;

    rx_len = 0;
    memset(rx_buf, 0, sizeof(rx_buf));

    esp_http_client_config_t config = {
        .url = "http://192.168.137.1:8000/api/time",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return result;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return result;
    }

    // int content_length = esp_http_client_fetch_headers(client);
    esp_http_client_fetch_headers(client);

    int read_len;
    while ((read_len = esp_http_client_read(client, rx_buf + rx_len,
                                            sizeof(rx_buf) - rx_len - 1)) > 0) {
        rx_len += read_len;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (rx_len <= 0) {
        ESP_LOGE(TAG, "[TIME] No data received");
        return result;
    }

    rx_buf[rx_len] = 0;

    ESP_LOGI(TAG, "[TIME] Raw JSON: %s", rx_buf);

    cJSON *root = cJSON_Parse(rx_buf);
    if (!root) {
        ESP_LOGE(TAG, "[TIME] JSON parse failed");
        return result;
    }

    cJSON *year   = cJSON_GetObjectItem(root, "year");
    cJSON *month  = cJSON_GetObjectItem(root, "month");
    cJSON *day    = cJSON_GetObjectItem(root, "day");
    cJSON *hour   = cJSON_GetObjectItem(root, "hour");
    cJSON *minute = cJSON_GetObjectItem(root, "minute");
    cJSON *second = cJSON_GetObjectItem(root, "second");

    if (cJSON_IsNumber(year) && cJSON_IsNumber(month) &&
        cJSON_IsNumber(day) && cJSON_IsNumber(hour) &&
        cJSON_IsNumber(minute) && cJSON_IsNumber(second)) {

        result.year   = year->valueint;
        result.month  = month->valueint;
        result.day    = day->valueint;
        result.hour   = hour->valueint;
        result.minute = minute->valueint;
        result.second = second->valueint;
        result.valid  = true;
    } else {
        ESP_LOGE(TAG, "[TIME] JSON fields missing or invalid");
    }

    cJSON_Delete(root);
    return result;
}

static time_t to_epoch(int y, int m, int d, int h, int min, int s)
{
    struct tm t = {
        .tm_year = y - 1900,
        .tm_mon  = m - 1,
        .tm_mday = d,
        .tm_hour = h,
        .tm_min  = min,
        .tm_sec  = s
    };

    return mktime(&t);
}

static time_t rtc_to_epoch(const PCF_DateTime *rtc)
{
    return to_epoch(
        rtc->year,
        rtc->month,
        rtc->day,
        rtc->hour,
        rtc->minute,
        rtc->second
    );
}

esp_err_t relay_get_handler(httpd_req_t *req)
{
    char buf[128];
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len > 1) {
        char *query = malloc(len);
        if (httpd_req_get_url_query_str(req, query, len) == ESP_OK) {
#if USE_REAL_INPUT_PINS
            // Read-only mode when using real input pins
            (void)query;
#else
            if (strstr(query, "relay1=ON")) { relay1_state = true; gpio_set_level(RELAY1_GPIO, 1); }
            if (strstr(query, "relay1=OFF")) { relay1_state = false; gpio_set_level(RELAY1_GPIO, 0); }
            if (strstr(query, "relay2=ON")) { relay2_state = true; gpio_set_level(RELAY2_GPIO, 1); }
            if (strstr(query, "relay2=OFF")) { relay2_state = false; gpio_set_level(RELAY2_GPIO, 0); }
#endif
        }
        free(query);
    }

    snprintf(buf, sizeof(buf),
             "States: in1=%s, in2=%s",
             relay1_state ? "ON" : "OFF",
             relay2_state ? "ON" : "OFF");

    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t relay_state_get_handler(httpd_req_t *req)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"relay1\":\"%s\",\"relay2\":\"%s\"}",
             relay1_state?"ON":"OFF",
             relay2_state?"ON":"OFF");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t last_minute_get_handler(httpd_req_t *req)
{
    // Return LIVE current-hour counters (second accuracy).
    // run_sec/idle_sec/error_sec/setting_sec accumulate every second and reset at each hour boundary.
    int32_t r, i, e, s;
    portENTER_CRITICAL(&counters_mux);
    r = run_sec;
    i = idle_sec;
    e = error_sec;
    s = setting_sec;
    portEXIT_CRITICAL(&counters_mux);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"timestamp\":\"%s\",\"run_sec\":%d,\"idle_sec\":%d,\"error_sec\":%d,\"setting_sec\":%d}",
        last_minute_data.timestamp,
        (int)r, (int)i, (int)e, (int)s);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

typedef struct {
    double run_min;
    double idle_min;
    double error_min;
    double setting_min;
    int rows;
    char daily_name[64];
} DailySummary;

static bool parse_date_yyyy_mm_dd(const char *s, struct tm *out)
{
    if (!s || !out) return false;

    int y = 0, m = 0, d = 0;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return false;
    if (y < 1970 || y > 2500) return false;
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > 31) return false;

    struct tm t = {
        .tm_year = y - 1900,
        .tm_mon = m - 1,
        .tm_mday = d,
        .tm_hour = 0,
        .tm_min = 0,
        .tm_sec = 0,
    };
    (void)mktime(&t);
    *out = t;
    return true;
}

static esp_err_t read_daily_summary_from_sd(const struct tm *t, DailySummary *out)
{
    if (!t || !out) return ESP_ERR_INVALID_ARG;
    if (!sdcard_ini) return ESP_FAIL;

    char daily_path[128];
    build_daily_csv_path_from_tm(t, daily_path, sizeof(daily_path));
    build_daily_csv_name_from_tm(t, out->daily_name, sizeof(out->daily_name));

    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
    FILE *f = fopen(daily_path, "r");
    if (!f) {
        xSemaphoreGiveRecursive(sd_mutex);
        ESP_LOGW(TAG, "[SUMMARY] Daily file not found: %s", daily_path);
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    double run_sum = 0.0, idle_sum = 0.0, error_sum = 0.0, setting_sum = 0.0;
    int rows = 0;

    // Skip header
    (void)fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) != NULL) {
        // expected format: timestamp,run_sec,idle_sec,error_sec[,setting_sec]
        char *p = line;
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';

        char *saveptr = NULL;
        char *tok_ts = strtok_r(p, ",", &saveptr);
        char *tok_run = strtok_r(NULL, ",", &saveptr);
        char *tok_idle = strtok_r(NULL, ",", &saveptr);
        char *tok_error = strtok_r(NULL, ",", &saveptr);
        char *tok_setting = strtok_r(NULL, ",", &saveptr);

        if (!tok_ts || !tok_run || !tok_idle || !tok_error) continue;

        run_sum += atof(tok_run);
        idle_sum += atof(tok_idle);
        error_sum += atof(tok_error);
        if (tok_setting) setting_sum += atof(tok_setting);
        rows++;
    }

    fclose(f);
    xSemaphoreGiveRecursive(sd_mutex);

    out->run_min = run_sum;
    out->idle_min = idle_sum;
    out->error_min = error_sum;
    out->setting_min = setting_sum;
    out->rows = rows;

    ESP_LOGI(TAG, "[SUMMARY] file=%s rows=%d run_sum=%.4f idle_sum=%.4f error_sum=%.4f setting_sum=%.4f",
             out->daily_name, out->rows, out->run_min, out->idle_min, out->error_min, out->setting_min);

    return ESP_OK;
}

static esp_err_t daily_summary_get_handler(httpd_req_t *req)
{
    if (!eth_connected) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Ethernet not connected", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (!sdcard_ini) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "SD card not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    struct tm tm_date = {0};
    bool have_date = false;
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len > 1) {
        char *query = malloc(len);
        if (query) {
            if (httpd_req_get_url_query_str(req, query, len) == ESP_OK) {
                char date_str[16] = {0};
                if (httpd_query_key_value(query, "date", date_str, sizeof(date_str)) == ESP_OK) {
                    have_date = parse_date_yyyy_mm_dd(date_str, &tm_date);
                }
            }
            free(query);
        }
    }

    if (!have_date) {
        PCF_DateTime rtc = {0};
        if (!rtc_read(&rtc)) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "RTC read failed", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        tm_date.tm_year = (int)rtc.year - 1900;
        tm_date.tm_mon = (int)rtc.month - 1;
        tm_date.tm_mday = (int)rtc.day;
        tm_date.tm_hour = 0;
        tm_date.tm_min = 0;
        tm_date.tm_sec = 0;
        (void)mktime(&tm_date);
    }

    DailySummary s = {0};
    esp_err_t err = read_daily_summary_from_sd(&tm_date, &s);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Daily CSV not found", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to read daily summary", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
             (int)(tm_date.tm_year + 1900), (int)(tm_date.tm_mon + 1), (int)tm_date.tm_mday);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"machine_id\":1,\"date\":\"%s\",\"file\":\"%s\",\"run_m\":%.2f,\"idle_min\":%.2f,\"error_min\":%.2f,\"setting_min\":%.2f,\"rows\":%d}",
             date_str, s.daily_name, s.run_min, s.idle_min, s.error_min, s.setting_min, s.rows);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t read_poweroff_events_from_sd(const struct tm *t, char *out_buf, size_t max_len)
{
    if (!t || !out_buf) return ESP_ERR_INVALID_ARG;
    if (!sdcard_ini) return ESP_FAIL;

    char path[128];

    snprintf(path, sizeof(path), "%s/%sP%02d%02d.csv",
             MOUNT_POINT,
             MACHINE_CODE_STR,
             t->tm_mon + 1,
             t->tm_mday);

    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
    FILE *f = fopen(path, "r");
    if (!f) {
        xSemaphoreGiveRecursive(sd_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    char line[128];
    size_t used = 0;
    int first = 1;

    used += snprintf(out_buf + used, max_len - used, "[");

    // skip header
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f)) {

        char *p = line;
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';

        char *saveptr;
        char *from = strtok_r(p, ",", &saveptr);
        char *to   = strtok_r(NULL, ",", &saveptr);

        if (!from || !to) continue;

        if (!first) {
            used += snprintf(out_buf + used, max_len - used, ",");
        }

        used += snprintf(out_buf + used, max_len - used,
            "{\"from\":\"%s\",\"to\":\"%s\"}", from, to);

        first = 0;

        if (used >= max_len - 100) break; // safety
    }

    used += snprintf(out_buf + used, max_len - used, "]");

    fclose(f);
    xSemaphoreGiveRecursive(sd_mutex);

    return ESP_OK;
}

static esp_err_t poweroff_events_get_handler(httpd_req_t *req)
{
    struct tm tm_date = {0};
    bool have_date = false;

    size_t len = httpd_req_get_url_query_len(req) + 1;

    if (len > 1) {
        char *query = malloc(len);
        if (query) {
            if (httpd_req_get_url_query_str(req, query, len) == ESP_OK) {
                char date_str[16];

                if (httpd_query_key_value(query, "date", date_str, sizeof(date_str)) == ESP_OK) {
                    have_date = parse_date_yyyy_mm_dd(date_str, &tm_date);
                }
            }
            free(query);
        }
    }

    if (!have_date) {
        PCF_DateTime rtc;
        if (!rtc_read(&rtc)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "RTC fail");
            return ESP_OK;
        }

        tm_date.tm_year = rtc.year - 1900;
        tm_date.tm_mon  = rtc.month - 1;
        tm_date.tm_mday = rtc.day;
    }

    static char json_buf[8192];

    esp_err_t err = read_poweroff_events_from_sd(&tm_date, json_buf, sizeof(json_buf));

    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Combined endpoint: daily CSV totals + live current-hour counters in ONE request.
// Backend calls this instead of /daily_summary + /last_minute separately,
// eliminating the two-request race condition that caused alternating data.
static esp_err_t live_summary_get_handler(httpd_req_t *req)
{
    if (!sdcard_ini) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "SD card not initialized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Read live current-hour counters (thread-safe)
    int32_t live_r, live_i, live_e, live_s;
    portENTER_CRITICAL(&counters_mux);
    live_r = run_sec;
    live_i = idle_sec;
    live_e = error_sec;
    live_s = setting_sec;
    portEXIT_CRITICAL(&counters_mux);

    // Get today's date from RTC
    PCF_DateTime rtc = {0};
    if (!rtc_read(&rtc)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "RTC read failed", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    struct tm tm_today = {
        .tm_year = (int)rtc.year - 1900,
        .tm_mon  = (int)rtc.month - 1,
        .tm_mday = (int)rtc.day,
    };
    mktime(&tm_today);

    // Read daily CSV summary (completed hours)
    double daily_run = 0, daily_idle = 0, daily_error = 0, daily_setting = 0;
    int rows = 0;
    DailySummary s = {0};
    esp_err_t err = read_daily_summary_from_sd(&tm_today, &s);
    if (err == ESP_OK) {
        daily_run     = s.run_min;
        daily_idle    = s.idle_min;
        daily_error   = s.error_min;
        daily_setting = s.setting_min;
        rows          = s.rows;
    }
    // err == ESP_ERR_NOT_FOUND is OK (first hour of day) — daily totals stay 0

    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
             (int)rtc.year, (int)rtc.month, (int)rtc.day);

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"date\":\"%s\","
        "\"daily_run\":%.2f,\"daily_idle\":%.2f,\"daily_error\":%.2f,\"daily_setting\":%.2f,\"rows\":%d,"
        "\"live_run\":%d,\"live_idle\":%d,\"live_error\":%d,\"live_setting\":%d}",
        date_str,
        daily_run, daily_idle, daily_error, daily_setting, rows,
        (int)live_r, (int)live_i, (int)live_e, (int)live_s);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_relay_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 8080;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 14;
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_relay = { .uri = "/relay", .method = HTTP_GET, .handler = relay_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_relay);

        httpd_uri_t uri_state = { 
            .uri = "/relay_state", 
            .method = HTTP_GET, 
            .handler = relay_state_get_handler, 
            .user_ctx = NULL 
        };
        httpd_register_uri_handler(server, &uri_state);

        httpd_uri_t uri_last_minute = { 
            .uri = "/last_minute", 
            .method = HTTP_GET, 
            .handler = last_minute_get_handler, 
            .user_ctx = NULL 
        };
        httpd_register_uri_handler(server, &uri_last_minute);

        httpd_uri_t uri_daily_summary = { .uri = "/daily_summary", .method = HTTP_GET, .handler = daily_summary_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_daily_summary);

        httpd_uri_t uri_api_daily_summary = { .uri = "/api/daily_summary", .method = HTTP_GET, .handler = daily_summary_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_api_daily_summary);

        httpd_uri_t uri_po_events = { .uri = "/api/poweroff_events", .method = HTTP_GET, .handler = poweroff_events_get_handler };
        httpd_register_uri_handler(server, &uri_po_events);

        // Combined endpoint — daily CSV + live counters in one call
        httpd_uri_t uri_live_summary = { .uri = "/live_summary", .method = HTTP_GET, .handler = live_summary_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_live_summary);
    }
    return server;
}    


static int last_fault_code = 0;

static esp_err_t send_state_change_to_server(int state)
{
    if (!eth_connected) {
        ESP_LOGW(TAG, "Ethernet not connected, skipping state update");
        return ESP_FAIL;
    }

    const char *state_str = "STOP";
    int fault_code = 0;

    if (state == STATE_RUN) {
        state_str = "RUN";
    }
    else if (state == STATE_IDLE) {
        state_str = "IDLE";
    }
    else if (state == STATE_ERROR) {
        state_str = "FAULT";
        last_fault_code = (esp_random() % 5) + 1;   // random 1–5, new code each transition
        fault_code = last_fault_code;
    }
    else if (state == STATE_SETTING) {
        state_str = "SETTING";
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://192.168.137.1:8000/api/machine_state?machine_id=1&state=%s&fault_code=%d",
        state_str,
        fault_code
    );

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "State change sent: %s fault=%d", state_str, fault_code);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "State send failed");
    return ESP_FAIL;
}

// Retentive data (NVS)
// Requirement:
// - Store run/idle/error + last_minute in NVS (update every minute).
// - Reset run/idle/error in NVS after hourly CSV write success.
typedef struct {
    int32_t last_minute, last_hour, last_day;
    int32_t run_sec, idle_sec, error_sec, setting_sec;
} RetainedData;


static void load_retained(RetainedData *data)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        if (nvs_get_i32(nvs_handle, "last_minute", &data->last_minute) != ESP_OK) data->last_minute = -1;
        if (nvs_get_i32(nvs_handle, "last_hour", &data->last_hour) != ESP_OK) data->last_hour = -1;
        if (nvs_get_i32(nvs_handle, "last_day", &data->last_day) != ESP_OK) data->last_day = -1;
        if (nvs_get_i32(nvs_handle, "run_sec", &data->run_sec) != ESP_OK) data->run_sec = 0;
        if (nvs_get_i32(nvs_handle, "idle_sec", &data->idle_sec) != ESP_OK) data->idle_sec = 0;
        if (nvs_get_i32(nvs_handle, "error_sec", &data->error_sec) != ESP_OK) data->error_sec = 0;
        if (nvs_get_i32(nvs_handle, "setting_sec", &data->setting_sec) != ESP_OK) data->setting_sec = 0;
        nvs_close(nvs_handle);
    } else {
        data->last_minute = data->last_hour = data->last_day = -1;
        data->run_sec = data->idle_sec = data->error_sec = data->setting_sec = 0;
    }
}

static void save_nvs_minute_tags(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {

        PCF_DateTime rtc;
        if (!rtc_read(&rtc)) {
            ESP_LOGE("NVS", "RTC read failed, skipping epoch save");
            nvs_close(nvs_handle);
            return;
        }

        time_t epoch = rtc_to_epoch(&rtc);

        err = nvs_set_i32(nvs_handle, "last_minute", last_minute);
        if (err == ESP_OK) err = nvs_set_i32(nvs_handle, "run_sec", run_sec);
        if (err == ESP_OK) err = nvs_set_i32(nvs_handle, "idle_sec", idle_sec);
        if (err == ESP_OK) err = nvs_set_i32(nvs_handle, "error_sec", error_sec);
        if (err == ESP_OK) err = nvs_set_i32(nvs_handle, "setting_sec", setting_sec);

        if (err == ESP_OK) err = nvs_set_i64(nvs_handle, "last_epoch", (int64_t)epoch);

        if (err == ESP_OK) err = nvs_commit(nvs_handle);

        nvs_close(nvs_handle);

        if (err == ESP_OK) {
            ESP_LOGI("NVS", "Saved minute + epoch=%lld", (long long)epoch);
        } else {
            ESP_LOGE("NVS", "Save failed: %s", esp_err_to_name(err));
        }

    } else {
        ESP_LOGE("NVS", "Open failed: %s", esp_err_to_name(err));
    }
}

static void save_nvs_hour_tag(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs_handle, "last_hour", last_hour);
        if (err == ESP_OK) err = nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        if (err == ESP_OK) {
            ESP_LOGI("NVS", "Hour write OK (hour=%d)", (int)last_hour);
        } else {
            ESP_LOGE("NVS", "Hour write FAILED: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("NVS", "Failed to open NVS for writing (hour tag): %s", esp_err_to_name(err));
    }
}

static void save_nvs_day_tag(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs_handle, "last_day", last_day);
        if (err == ESP_OK) err = nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        if (err == ESP_OK) {
            ESP_LOGI("NVS", "Day write OK (day=%d)", (int)last_day);
        } else {
            ESP_LOGE("NVS", "Day write FAILED: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("NVS", "Failed to open NVS for writing (day tag): %s", esp_err_to_name(err));
    }
}

static void load_nvs_setting_limit(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        int32_t val = 0;
        if (nvs_get_i32(nvs_handle, "set_limit", &val) == ESP_OK && val > 0) {
            setting_max_duration_sec = (int)val;
            ESP_LOGI("NVS", "Loaded setting limit from NVS: %d sec (%d min)",
                     setting_max_duration_sec, setting_max_duration_sec / 60);
        }
        nvs_close(nvs_handle);
    }
}

static void save_nvs_setting_limit(int sec)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oee_counters", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs_handle, "set_limit", (int32_t)sec);
        if (err == ESP_OK) err = nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI("NVS", "Saved setting limit to NVS: %d sec (%d min)", sec, sec / 60);
        }
    }
}

// =========================
// Offline upload helpers (timeline-based, no rename)
// =========================


typedef struct {
    int month;
    int day;
    bool valid;
} ServerCSVDate;

static ServerCSVDate fetch_last_uploaded_csv(void)
{
    ServerCSVDate result = {0};
    result.valid = false;

    esp_http_client_config_t config = {
        .url = "http://192.168.137.1:8000/api/last_uploaded_csv",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return result;

    char buffer[256];
    int len = 0;

    esp_http_client_open(client, 0);
    esp_http_client_fetch_headers(client);

    int r;
    while ((r = esp_http_client_read(client, buffer + len, sizeof(buffer) - len - 1)) > 0)
        len += r;

    buffer[len] = 0;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(buffer);
    if (!root) return result;

    cJSON *m = cJSON_GetObjectItem(root, "month");
    cJSON *d = cJSON_GetObjectItem(root, "day");

    if (cJSON_IsNumber(m) && cJSON_IsNumber(d)) {
        result.month = m->valueint;
        result.day = d->valueint;
        result.valid = true;
    }

    cJSON_Delete(root);

    return result;
}

static void offline_upload_task(void *arg)
{
    ESP_LOGI(TAG, "Offline upload task started");

    while (1)
    {
        if (eth_connected && sdcard_ini)
        {
            ServerCSVDate srv = fetch_last_uploaded_csv();

            if (!srv.valid) {
                ESP_LOGW(TAG, "[OFFLINE] Could not fetch last uploaded CSV date, retrying in 30s");
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }

            // Read today's date from RTC
            PCF_DateTime rtc;
            rtc_read(&rtc);

            int today_y = rtc.year;
            int today_m = rtc.month;
            int today_d = rtc.day;

            // Convert server's last uploaded date to time_t
            // If server month > today month, it must be from last year
            int srv_year = (srv.month > today_m) ? (today_y - 1) : today_y;
            // If server returned 0,0 → no uploads ever → use epoch start
            time_t srv_time = 0;
            if (srv.month != 0 || srv.day != 0) {
                struct tm srv_tm = {
                    .tm_year = srv_year - 1900,
                    .tm_mon  = srv.month - 1,
                    .tm_mday = srv.day,
                    .tm_hour = 23,
                    .tm_min  = 59,
                    .tm_sec  = 59,
                };
                srv_time = mktime(&srv_tm);
                ESP_LOGI(TAG, "[OFFLINE] Server last uploaded: %04d-%02d-%02d",
                         srv_year, srv.month, srv.day);
            } else {
                ESP_LOGI(TAG, "[OFFLINE] No prior uploads, will upload all past CSVs");
            }

            // Convert today to time_t (start of day — to exclude today's file)
            struct tm today_tm = {
                .tm_year = today_y - 1900,
                .tm_mon  = today_m - 1,
                .tm_mday = today_d,
                .tm_hour = 0,
                .tm_min  = 0,
                .tm_sec  = 0,
            };
            time_t today_time = mktime(&today_tm);

            DIR *dir = opendir(MOUNT_POINT);
            if (!dir) {
                ESP_LOGE(TAG, "[OFFLINE] Failed to open SD directory");
                vTaskDelay(pdMS_TO_TICKS(60000));
                continue;
            }

            struct dirent *entry;
            int uploaded_count = 0;
            int skipped_count  = 0;

            while ((entry = readdir(dir)) != NULL)
            {
                // Only process .csv files
                if (!strcasestr(entry->d_name, ".csv"))
                    continue;

                int mm = 0, dd = 0;

                bool is_poweroff = false;

                // Try normal file
                if (sscanf(entry->d_name, "M01_%2d%2d.CSV", &mm, &dd) == 2) {
                    is_poweroff = false;
                }
                // Try power-off file
                else if (sscanf(entry->d_name, "M01P%2d%2d.CSV", &mm, &dd) == 2) {
                    is_poweroff = true;
                }
                else {
                    ESP_LOGW(TAG, "[OFFLINE] Skipping unknown file: %s", entry->d_name);
                    continue;
                }

                // Determine file's year
                // If file month > today month → it's from last year
                int file_year = (mm > today_m) ? (today_y - 1) : today_y;

                // Convert file date to time_t (start of that day)
                struct tm file_tm = {
                    .tm_year = file_year - 1900,
                    .tm_mon  = mm - 1,
                    .tm_mday = dd,
                    .tm_hour = 0,
                    .tm_min  = 0,
                    .tm_sec  = 0,
                };
                time_t file_time = mktime(&file_tm);

                // Skip today's file (still being written)
                if (file_time >= today_time) {
                    ESP_LOGI(TAG, "[OFFLINE] Skipping today's file: %s", entry->d_name);
                    skipped_count++;
                    continue;
                }

                // Skip only production files based on timeline
                if (!is_poweroff && file_time <= srv_time) {
                    ESP_LOGI(TAG, "[OFFLINE] Already uploaded (production), skipping: %s", entry->d_name);
                    skipped_count++;
                    continue;
                }

                // Power-off files → NEVER skip by timeline
                if (is_poweroff) {
                    ESP_LOGI(TAG, "[OFFLINE] Power-off file → force upload: %s", entry->d_name);
                }

                // This file is a past file not yet uploaded → upload it
                char path[300];
                snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, entry->d_name);

                ESP_LOGI(TAG, "[OFFLINE] Uploading missing CSV: %04d-%02d-%02d → %s",
                         file_year, mm, dd, entry->d_name);

                esp_err_t result = upload_file_http(path, entry->d_name);

                if (result == ESP_OK) {
                    ESP_LOGI(TAG, "[OFFLINE] Upload OK: %s", entry->d_name);
                    uploaded_count++;
                } else {
                    ESP_LOGW(TAG, "[OFFLINE] Upload FAILED: %s", entry->d_name);
                }

                vTaskDelay(pdMS_TO_TICKS(500));
            }

            closedir(dir);

            ESP_LOGI(TAG, "[OFFLINE] Done. Uploaded=%d Skipped=%d",
                     uploaded_count, skipped_count);
        }
        else
        {
            if (!eth_connected) ESP_LOGW(TAG, "[OFFLINE] Ethernet not connected, waiting...");
            if (!sdcard_ini)    ESP_LOGW(TAG, "[OFFLINE] SD card not ready, waiting...");
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

static void update_last_minute_snapshot(const char *timestamp, int run_s, int idle_s, int error_s, int setting_s)
{
    strncpy(last_minute_data.timestamp, timestamp, sizeof(last_minute_data.timestamp) - 1);
    last_minute_data.run_sec = run_s;
    last_minute_data.idle_sec = idle_s;
    last_minute_data.error_sec = error_s;
    last_minute_data.setting_sec = setting_s;

    ESP_LOGI(TAG, "[SNAPSHOT] Updated last minute: %s (Run=%d, Idle=%d, Error=%d, Setting=%d)",
             last_minute_data.timestamp, run_s, idle_s, error_s, setting_s);
}


static void sync_rtc_with_pc_time(void)
{
    if (!eth_connected) {
        ESP_LOGW(TAG, "[TIME] Ethernet not ready, skip sync");
        return;
    }

    ServerTime srv = fetch_time_from_server();
    if (!srv.valid) {
        ESP_LOGW(TAG, "[TIME] Failed to fetch PC time");
        return;
    }

    PCF_DateTime rtc;
    if (!rtc_read(&rtc)) {
        ESP_LOGE(TAG, "[TIME] RTC read failed");
        return;
    }

    time_t pc_time = to_epoch(
        srv.year, srv.month, srv.day,
        srv.hour, srv.minute, srv.second
    );

    time_t rtc_time = to_epoch(
        rtc.year, rtc.month, rtc.day,
        rtc.hour, rtc.minute, rtc.second
    );

    int diff = llabs((int64_t)(pc_time - rtc_time));

    ESP_LOGI(TAG, "[TIME] RTC diff = %d sec", diff);

    ESP_LOGI(TAG, "PC:  %04d-%02d-%02d %02d:%02d:%02d",
         srv.year, srv.month, srv.day,
         srv.hour, srv.minute, srv.second);

    ESP_LOGI(TAG, "RTC: %04d-%02d-%02d %02d:%02d:%02d",
            rtc.year, rtc.month, rtc.day,
            rtc.hour, rtc.minute, rtc.second);
            
    if (diff >= RTC_DRIFT_THRESHOLD_SEC ) {
        ESP_LOGW(TAG, "[TIME] RTC drift → updating from PC");

        PCF_DateTime newRTC = {
            .year   = srv.year,
            .month  = srv.month,
            .day    = srv.day,
            .hour   = srv.hour,
            .minute = srv.minute,
            .second = srv.second
        };

        if (PCF_SetDateTime(&newRTC) == 0)
            ESP_LOGI(TAG, "[TIME] RTC updated successfully");
        else
            ESP_LOGE(TAG, "[TIME] RTC update failed");
    } else {
        ESP_LOGI(TAG, "[TIME] RTC already in sync");
    }
}

static void rtc_boot_sync_task(void *arg)
{
    // wait up to 10 seconds for Ethernet
    for (int i = 0; i < 3; i++) {
        if (eth_connected) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (!eth_connected) {
        ESP_LOGW(TAG, "[TIME] Ethernet never came up — skip RTC sync");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "[TIME] Ethernet ready — syncing RTC");
    sync_rtc_with_pc_time();

    vTaskDelete(NULL);   // 🔥 run once and die
}

static void state_http_task(void *arg)
{
    int state;

    while (1) {
        if (xQueueReceive(state_queue, &state, portMAX_DELAY)) {

            ESP_LOGI(TAG, "Sending state to server: %d", state);

            esp_err_t err = send_state_change_to_server(state);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "State send failed, retrying...");

            }
        }
    }
}

static void logger_task_counter(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Logger counter task started");

    RetainedData retained;
    load_retained(&retained);

    // ── Boot stale check: flush counters if hour or day changed while powered off ──
    PCF_DateTime bootRTC = {0};
    if (rtc_read(&bootRTC)) {
        bool day_changed  = (retained.last_day  >= 0 && retained.last_day  != (int)bootRTC.day);
        bool hour_changed = (retained.last_hour >= 0 && retained.last_hour != (int)bootRTC.hour);

        if (day_changed) {
            // Data is from a previous day — flush it to yesterday's CSV
            ESP_LOGW(TAG, "[BOOT] Day changed while off (%d→%d), flushing stale counters to prev-day CSV",
                     (int)retained.last_day, (int)bootRTC.day);

            if (sdcard_ini && sd_mutex) {
                struct tm prev_tm = {
                    .tm_year = (int)bootRTC.year - 1900,
                    .tm_mon  = (int)bootRTC.month - 1,
                    .tm_mday = retained.last_day,
                    .tm_hour = retained.last_hour >= 0 ? retained.last_hour : 23,
                    .tm_min  = 0,
                    .tm_sec  = 0,
                };
                mktime(&prev_tm);

                char prev_path[64], prev_name[32], prev_ts[64];
                build_daily_csv_path_from_tm(&prev_tm, prev_path, sizeof(prev_path));
                build_daily_csv_name_from_tm(&prev_tm, prev_name, sizeof(prev_name));
                snprintf(prev_ts, sizeof(prev_ts), "%04d-%02d-%02d %02d:00:00",
                         prev_tm.tm_year + 1900, prev_tm.tm_mon + 1, prev_tm.tm_mday,
                         prev_tm.tm_hour);

                ensure_file_with_header(prev_path, "timestamp,run_sec,idle_sec,error_sec,setting_sec");

                xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
                FILE *f = fopen(prev_path, "a");
                if (f) {
                    fprintf(f, "%s,%.4f,%.4f,%.4f,%.4f\n",
                            prev_ts,
                            (double)retained.run_sec,
                            (double)retained.idle_sec,
                            (double)retained.error_sec,
                            (double)retained.setting_sec);
                    fclose(f);
                    ESP_LOGI(TAG, "[BOOT] Flushed stale data to %s (row: %s)", prev_path, prev_ts);
                }
                xSemaphoreGiveRecursive(sd_mutex);
            }

            retained.run_sec     = 0;
            retained.idle_sec    = 0;
            retained.error_sec   = 0;
            retained.setting_sec = 0;
            retained.last_hour   = (int)bootRTC.hour;
            retained.last_day    = (int)bootRTC.day;

        } else if (hour_changed) {
            // Same day, different hour — flush partial hour to today's CSV
            ESP_LOGW(TAG, "[BOOT] Hour changed while off (%d→%d), flushing stale counters to same-day CSV",
                     (int)retained.last_hour, (int)bootRTC.hour);

            if (sdcard_ini && sd_mutex) {
                struct tm prev_tm = {
                    .tm_year = (int)bootRTC.year - 1900,
                    .tm_mon  = (int)bootRTC.month - 1,
                    .tm_mday = (int)bootRTC.day,
                    .tm_hour = retained.last_hour,
                    .tm_min  = 0,
                    .tm_sec  = 0,
                };
                mktime(&prev_tm);

                char prev_path[64], prev_name[32], prev_ts[64];
                build_daily_csv_path_from_tm(&prev_tm, prev_path, sizeof(prev_path));
                build_daily_csv_name_from_tm(&prev_tm, prev_name, sizeof(prev_name));
                snprintf(prev_ts, sizeof(prev_ts), "%04d-%02d-%02d %02d:00:00",
                         prev_tm.tm_year + 1900, prev_tm.tm_mon + 1, prev_tm.tm_mday,
                         prev_tm.tm_hour);

                ensure_file_with_header(prev_path, "timestamp,run_sec,idle_sec,error_sec,setting_sec");

                xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
                FILE *f = fopen(prev_path, "a");
                if (f) {
                    fprintf(f, "%s,%.4f,%.4f,%.4f,%.4f\n",
                            prev_ts,
                            (double)retained.run_sec,
                            (double)retained.idle_sec,
                            (double)retained.error_sec,
                            (double)retained.setting_sec);
                    fclose(f);
                    ESP_LOGI(TAG, "[BOOT] Flushed stale hour data to %s (row: %s)", prev_path, prev_ts);
                }
                xSemaphoreGiveRecursive(sd_mutex);
            }

            retained.run_sec     = 0;
            retained.idle_sec    = 0;
            retained.error_sec   = 0;
            retained.setting_sec = 0;
            retained.last_hour   = (int)bootRTC.hour;
        }
    }
    // ────────────────────────────────────────────────────────────────────────────────

    portENTER_CRITICAL(&counters_mux);
    last_minute = retained.last_minute;
    last_hour   = retained.last_hour;
    last_day    = retained.last_day;
    run_sec     = retained.run_sec;
    idle_sec    = retained.idle_sec;
    error_sec   = retained.error_sec;
    setting_sec = retained.setting_sec;
    portEXIT_CRITICAL(&counters_mux);

    while (1) {
        PCF_DateTime currentRTC = {0};
        static bool rtc_ok = false;
        static uint32_t dbg_counter = 0;

        if (!rtc_read(&currentRTC)) {
            if (rtc_ok) {
                ESP_LOGE(TAG, "Failed to read time from RTC");
                rtc_ok = false;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!rtc_ok) {
            ESP_LOGI(TAG, "RTC time restored - counter task active");
            rtc_ok = true;
        }

        int current_sec    = currentRTC.second;
        int current_minute = currentRTC.minute;

        if (last_sec < 0) {
            portENTER_CRITICAL(&counters_mux);
            if (last_sec    < 0) last_sec    = current_sec;
            if (last_minute < 0) last_minute = current_minute;
            portEXIT_CRITICAL(&counters_mux);
        }

        run     = 0;
        idle    = 0;
        error   = 0;
        setting = 0;

        if (current_sec != last_sec) {
#if USE_REAL_INPUT_PINS
            int in1_raw = gpio_get_level(INPUT1_GPIO);
            int in2_raw = gpio_get_level(INPUT2_GPIO);
            int in5_raw = gpio_get_level(INPUT5_GPIO);
            bool in1 = gpio_level_to_state(in1_raw);
            bool in2 = gpio_level_to_state(in2_raw);
            bool in5 = gpio_level_to_state(in5_raw);

            relay1_state = in1;
            relay2_state = in2;

            int current_state;

            // 1st Priority: Error (DI4)
            if (in2) {
                error = 1;
                current_state = STATE_ERROR;
                di5_continuous_sec = 0; // reset DI5 timer if error occurs
            }
            // 2nd Priority: DI5 (Setting time for first setting_max_duration_sec, then Idle)
            else if (in5) {
                di5_continuous_sec++;
                if (di5_continuous_sec <= setting_max_duration_sec) {
                    setting = 1;
                    current_state = STATE_SETTING;
                } else {
                    idle = 1;
                    current_state = STATE_IDLE;
                }
            }
            // 3rd Priority: Run (DI3)
            else if (in1) {
                run = 1;
                current_state = STATE_RUN;
                di5_continuous_sec = 0; // DI5 is off
            }
            // Default: All signals off -> Idle
            else {
                idle = 1;
                current_state = STATE_IDLE;
                di5_continuous_sec = 0; // DI5 is off
            }

            current_machine_state = current_state;

            if (current_state != last_machine_state) {
                xQueueSend(state_queue, &current_state, 0);  // non-blocking
                last_machine_state = current_state;
            }
#else
            if (relay1_state && relay2_state)        run   = 1;
            else if (!relay1_state && !relay2_state) idle  = 1;
            else                                     error = 1;
#endif

            portENTER_CRITICAL(&counters_mux);
            last_sec     = current_sec;
            run_sec     += run;
            idle_sec    += idle;
            error_sec   += error;
            setting_sec += setting;
            portEXIT_CRITICAL(&counters_mux);

            ESP_LOGI("CNT", "run=%d idle=%d error=%d setting=%d (di5_time=%ds)",
                     (int)run_sec, (int)idle_sec, (int)error_sec, (int)setting_sec, (int)di5_continuous_sec);

            if ((current_sec % 5) == 0) {
                rs485_send_time_data(&currentRTC);
            }
        }

        if (current_minute != last_minute) {
            char ts[32];
            format_rtc_timestamp(&currentRTC, ts, sizeof(ts));

            int32_t r, i, e, s;
            portENTER_CRITICAL(&counters_mux);
            last_minute = current_minute;
            r = run_sec;
            i = idle_sec;
            e = error_sec;
            s = setting_sec;
            portEXIT_CRITICAL(&counters_mux);

            update_last_minute_snapshot(ts, r, i, e, s);
            save_nvs_minute_tags();

            // send run_sec, idle_sec, error_sec, setting_sec to RS485 queue
            MinuteRS485Data data = {
                .year = (int)currentRTC.year,
                .month = (int)currentRTC.month,
                .day = (int)currentRTC.day,
                .hour = currentRTC.hour,
                .minute = currentRTC.minute,
                .run_sec = r,
                .idle_sec = i,
                .error_sec = e,
                .setting_sec = s
            };

            if (xQueueSend(rs485_minute_queue, &data, 0) != pdPASS) {
                ESP_LOGW(TAG, "[RS485_MIN] Queue full, dropping minute data");
            }
        }

        if ((++dbg_counter % 25) == 0) {
#if USE_REAL_INPUT_PINS
            int in1_raw = gpio_get_level(INPUT1_GPIO);
            int in2_raw = gpio_get_level(INPUT2_GPIO);
            int in5_raw = gpio_get_level(INPUT5_GPIO);
            bool in1 = gpio_level_to_state(in1_raw);
            bool in2 = gpio_level_to_state(in2_raw);
            bool in5_active = (in5_raw == 0); // 0 = signal present, 1 = no signal
            ESP_LOGI("DBG", "in1_raw=%d in2_raw=%d -> in1=%d in2=%d (RUN=10 IDLE=00 ERROR=01)",
                      in1_raw, in2_raw, (int)in1, (int)in2);
            ESP_LOGI("DI5_CHECK", "DI5 Raw: %d -> Signal is: %s", 
                     in5_raw, in5_active ? ">>> SIGNAL DETECTED (ON) <<<" : "OFF (NO SIGNAL)");
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void logger_task_csv(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Logger CSV task started");

    while (!sdcard_ini || sd_mutex == NULL) {
        ESP_LOGW(TAG, "SD card not ready yet, waiting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1) {
        PCF_DateTime currentRTC = {0};
        static bool rtc_ok = false;

        if (!rtc_read(&currentRTC)) {
            if (rtc_ok) {
                ESP_LOGE(TAG, "Failed to read time from RTC");
                rtc_ok = false;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!rtc_ok) {
            ESP_LOGI(TAG, "RTC time restored - CSV task active");
            rtc_ok = true;
        }

        int current_hour = currentRTC.hour;
        int current_day  = currentRTC.day;

        if (last_hour < 0) {
            portENTER_CRITICAL(&counters_mux);
            if (last_hour < 0) last_hour = current_hour;
            portEXIT_CRITICAL(&counters_mux);
            save_nvs_hour_tag();
        }

        if (current_hour != last_hour) {
            struct tm completed_tm;
            get_completed_hour_tm(&currentRTC, &completed_tm);

            char hour_ts[64];
            snprintf(hour_ts, sizeof(hour_ts), "%04d-%02d-%02d %02d:00:00",
                     (int)(completed_tm.tm_year + 1900),
                     (int)(completed_tm.tm_mon + 1),
                     (int)completed_tm.tm_mday,
                     (int)completed_tm.tm_hour);

            char daily_path[64];
            char daily_name[32];
            build_daily_csv_path_from_tm(&completed_tm, daily_path, sizeof(daily_path));
            build_daily_csv_name_from_tm(&completed_tm, daily_name, sizeof(daily_name));

            ensure_file_with_header(daily_path, "timestamp,run_sec,idle_sec,error_sec,setting_sec");
            ESP_LOGI("CSV", "Hourly log file: %s", daily_path);

            // ── FIX: read live globals directly, not stale NVS ──
            int32_t snap_run, snap_idle, snap_error, snap_setting;
            portENTER_CRITICAL(&counters_mux);
            snap_run     = run_sec;
            snap_idle    = idle_sec;
            snap_error   = error_sec;
            snap_setting = setting_sec;
            portEXIT_CRITICAL(&counters_mux);

            double hour_run_m     = (double)snap_run;
            double hour_idle_m    = (double)snap_idle;
            double hour_error_m   = (double)snap_error;
            double hour_setting_m = (double)snap_setting;
            // ────────────────────────────────────────────────────

            xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
            bool csv_ok = false;
            FILE* f_daily = fopen(daily_path, "a");
            if (f_daily) {
                int wr = fprintf(f_daily, "%s,%.4f,%.4f,%.4f,%.4f\n",
                                 hour_ts, hour_run_m, hour_idle_m, hour_error_m, hour_setting_m);
                if (wr >= 0) {
                    csv_ok = true;
                    ESP_LOGI("CSV", "Append OK: %s,%.4f,%.4f,%.4f,%.4f",
                             hour_ts, hour_run_m, hour_idle_m, hour_error_m, hour_setting_m);
                } else {
                    ESP_LOGE("CSV", "Append FAILED (errno=%d: %s)", errno, strerror(errno));
                }
                fclose(f_daily);
            } else {
                ESP_LOGE("CSV", "Failed to open %s for append (errno=%d: %s)",
                         daily_path, errno, strerror(errno));
            }
            xSemaphoreGiveRecursive(sd_mutex);

            portENTER_CRITICAL(&counters_mux);
            last_hour = current_hour;
            portEXIT_CRITICAL(&counters_mux);
            save_nvs_hour_tag();

            if (csv_ok) {
                portENTER_CRITICAL(&counters_mux);
                run_sec     = 0;
                idle_sec    = 0;
                error_sec   = 0;
                setting_sec = 0;
                last_minute = currentRTC.minute;
                portEXIT_CRITICAL(&counters_mux);

                save_nvs_minute_tags();
                update_last_minute_snapshot(hour_ts, 0, 0, 0, 0);
            }
        }

        if (last_day < 0) {
            portENTER_CRITICAL(&counters_mux);
            if (last_day < 0) last_day = current_day;
            portEXIT_CRITICAL(&counters_mux);
            save_nvs_day_tag();
        }

        if (current_day != last_day) {
            portENTER_CRITICAL(&counters_mux);
            last_day = current_day;
            portEXIT_CRITICAL(&counters_mux);
            save_nvs_day_tag();
            ESP_LOGI(TAG, "Day changed, updated NVS last_day=%d", (int)last_day);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void list_sdcard_csv_files(void)
{
    if (!sdcard_ini) {
        ESP_LOGW("SD", "SD card not initialized");
        return;
    }

    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        ESP_LOGE("SD", "Failed to open directory %s (errno=%d: %s)",
                 MOUNT_POINT, errno, strerror(errno));
        return;
    }

    ESP_LOGI("SD", "Listing CSV files in %s:", MOUNT_POINT);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcasestr(entry->d_name, ".csv")) {

            char fullpath[sizeof(MOUNT_POINT) + 1 + 256];
            snprintf(fullpath, sizeof(fullpath), "%s/%s",
                     MOUNT_POINT, entry->d_name);

            struct stat st;
            if (stat(fullpath, &st) == 0) {
                ESP_LOGI("SD",
                         "File: %s | Size: %ld bytes",
                         entry->d_name,
                         (long)st.st_size);
            } else {
                ESP_LOGW("SD",
                         "File: %s | stat failed (errno=%d: %s)",
                         entry->d_name,
                         errno, strerror(errno));
            }
        }
    }

    closedir(dir);
}

/*
 * TCA9554 — Set custom HIGH/LOW states on each DO
 * Board: Waveshare ESP32-S3-POE-ETH
 */

#include <stdint.h>
#include "driver/i2c_master.h"
#include"esp_io_expander_tca9554.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include <unistd.h>    // fsync(), close(), unlink()
#include <fcntl.h>     // open(), O_DIRECTORY

/* relay index mapping */
#define RELAY1 0
#define RELAY2 1

/* RS-485 UART */ 
#define RS485_UART      UART_NUM_1
#define RS485_TXD       17
#define RS485_RXD       18
#define RS485_RTS       21
#define RS485_BUF_SIZE  256

// static const char *TAG = "tca9554_demo";

static esp_io_expander_handle_t io_expander = NULL;
static i2c_master_bus_handle_t i2c_handle = NULL;

#define RELAY_FILE      "/sdcard/relay.txt"
#define RELAY_FILE_TMP  "/sdcard/relay.tmp"



#define WIFI_SSID "1527"
#define WIFI_PASS "123456789"

static bool relay_state[8] = { false };
static bool system_ready = false;



/* ===== Relay control source ===== */
typedef enum {
    SRC_DI,
    SRC_RS485,
    SRC_HTTP
} relay_source_t;

// typedef struct {
//     bool r1;
//     bool r2;
// } relay_persist_t;

/* Forward declaration */
void relay_set(uint8_t relay, bool on, relay_source_t src);

/* Track last control source */
static relay_source_t last_source[8] = {0};

/* ===== DI → DO mapping ===== */
typedef struct {
    uint8_t di_gpio;
    uint8_t relay;
    bool    active_high;
} di_do_map_t;

static const di_do_map_t di_do_map[] = {
    { RELAY1_GPIO, RELAY1, false },  // DI1 → Relay1
    { RELAY2_GPIO, RELAY2, false },  // DI2 → Relay2
};

#define DI_DO_COUNT (sizeof(di_do_map)/sizeof(di_do_map[0]))


static void di_gpio_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask =
            (1ULL << RELAY1_GPIO) |
            (1ULL << RELAY2_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static void di_monitor_task(void *arg)
{
    static int last_state[DI_DO_COUNT];
    static int stable_count[DI_DO_COUNT];

    /* Initialize states */
    for (int i = 0; i < DI_DO_COUNT; i++) {
        last_state[i]   = gpio_get_level(di_do_map[i].di_gpio);
        stable_count[i] = 0;
    }

    ESP_LOGI(TAG, "DI monitor task started");

    while (1) {

        for (int i = 0; i < DI_DO_COUNT; i++) {

            int level = gpio_get_level(di_do_map[i].di_gpio);

            /* If level is same as last stable → reset debounce counter */
            if (level == last_state[i]) {
                stable_count[i] = 0;
            }
            else {
                /* Level changed → count stability */
                stable_count[i]++;

                /* Debounce threshold reached */
                if (stable_count[i] >= 3) {   // 3 × 50 ms = ~150 ms

                    stable_count[i] = 0;
                    last_state[i] = level;

                    bool on = di_do_map[i].active_high ? level : !level;

                    ESP_LOGI(TAG,
                        "DI GPIO %d stable change → Relay %d = %s",
                        di_do_map[i].di_gpio,
                        di_do_map[i].relay + 1,
                        on ? "ON" : "OFF"
                    );

                    /* DI-driven relay control */
                    relay_set(di_do_map[i].relay, on, SRC_DI);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));   // polling interval
    }
}

static void rs485_send_status(void)
{
    char msg[128];

    snprintf(msg, sizeof(msg),
        "{\"relay1\":%s,\"relay2\":%s}\n",
        relay_state[0] ? "true" : "false",
        relay_state[1] ? "true" : "false"
    );

    uart_write_bytes(RS485_UART, msg, strlen(msg));

    ESP_LOGI(TAG, "RS485 sent: %s", msg);
}

static bool relay_save_atomic(void)
{
    FILE *f = fopen(RELAY_FILE_TMP, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open temp relay file");
        return false;
    }

    fprintf(f,
        "R1=%d,R2=%d\n",
        relay_state[RELAY1] ? 1 : 0,
        relay_state[RELAY2] ? 1 : 0
    );

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    unlink(RELAY_FILE);  // ignore errors

    if (rename(RELAY_FILE_TMP, RELAY_FILE) != 0) {
        ESP_LOGE(TAG, "Relay file rename failed");
        return false;
    }

    /* Sync directory (FAT safety) */
    int dfd = open("/sdcard", O_DIRECTORY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }

    ESP_LOGI(TAG, "Relay state saved (atomic)");
    return true;
}

void relay_set(uint8_t relay, bool on, relay_source_t src)
{
    if (relay >= 8) return;

    if (relay_state[relay] == on) {
        rs485_send_status();
        return;
    }

    relay_state[relay] = on;
    last_source[relay] = src;

    esp_io_expander_set_level(
        io_expander,
        (1 << relay),
        on ? 0 : 1
    );

    ESP_LOGI(TAG,
        "Relay %d -> %s (src=%d)",
        relay + 1,
        on ? "ON" : "OFF",
        src
    );

    rs485_send_status();
    if (system_ready) {
        relay_save_atomic();
    }

}


/* ---------------- I2C INIT FOR ESP32 to HMI Display Connect ---------------- */
#define I2C_MASTER_SDA_IO   42
#define I2C_MASTER_SCL_IO   41
#define I2C_MASTER_NUM      I2C_NUM_0

static esp_err_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,   // sda = serail data
        .scl_io_num = I2C_MASTER_SCL_IO,   // scl = serial clock
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
        .glitch_ignore_cnt = 7
    };

    return i2c_new_master_bus(&cfg, &i2c_handle);
}

/* ---------------- Creates the TCA9554 driver and configures all eight expander pins as outputs. ---------------- */
/*  
        A driver is software that helps your application communicate with a particular hardware device.
        Your board contains a TCA9554 I/O expander chip. 
        Your C code needs a way to communicate with that chip to control the digital outputs.. 
 */

/* TCA9554 address (A0=A1=A2=GND) */
#define I2C_ADDRESS ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000

/* Mask for all 8 outputs */
#define DO_MASK_8BITS ( \
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | \
        IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3 | \
        IO_EXPANDER_PIN_NUM_4 | IO_EXPANDER_PIN_NUM_5 | \
        IO_EXPANDER_PIN_NUM_6 | IO_EXPANDER_PIN_NUM_7 )

static esp_err_t tca_init(void)
{
    esp_err_t ret = esp_io_expander_new_i2c_tca9554(
        i2c_handle, I2C_ADDRESS, &io_expander
    );
    if (ret != ESP_OK) return ret;

    return esp_io_expander_set_dir(io_expander, DO_MASK_8BITS, IO_EXPANDER_OUTPUT);
}



/* ---------------------- This Is WIFI Configration Function Started  -------------------------- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &got_ip);

    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS } };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

/* ------- WIFI Configration Function Ended-------- */

static void rs485_send_today_csv(void)
{
    if (!sdcard_ini || sd_mutex == NULL) {
        ESP_LOGW(TAG, "[RS485_CSV] SD not ready, skip");
        return;
    }

    PCF_DateTime rtc = {0};
    if (!rtc_read(&rtc)) {
        ESP_LOGW(TAG, "[RS485_CSV] RTC read failed, skip");
        return; 
    }

    struct tm today = {
        .tm_year = (int)rtc.year - 1900,
        .tm_mon  = (int)rtc.month - 1,
        .tm_mday = (int)rtc.day,
    };

    char daily_path[64];
    build_daily_csv_path_from_tm(&today, daily_path, sizeof(daily_path));

    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);
    FILE *f = fopen(daily_path, "r");
    if (!f) {
        xSemaphoreGiveRecursive(sd_mutex);
        ESP_LOGW(TAG, "[RS485_CSV] Today's CSV not found: %s", daily_path);
        return;
    }

    char line[256];

    // Skip header
    (void)fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) != NULL) {
        // Strip newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Parse: timestamp,run_sec,idle_sec,error_sec[,setting_sec]
        char *saveptr = NULL;
        char *tok_ts      = strtok_r(line, ",", &saveptr);
        char *tok_run     = strtok_r(NULL, ",", &saveptr);
        char *tok_idle    = strtok_r(NULL, ",", &saveptr);
        char *tok_error   = strtok_r(NULL, ",", &saveptr);
        char *tok_setting = strtok_r(NULL, ",", &saveptr);

        if (!tok_ts || !tok_run || !tok_idle || !tok_error) continue;

        char msg[256];
        snprintf(msg, sizeof(msg),
            "{\"type\":\"csv\",\"ts\":\"%s\",\"run\":%.4f,\"idle\":%.4f,\"error\":%.4f,\"setting\":%.4f}\n",
            tok_ts,
            atof(tok_run),
            atof(tok_idle),
            atof(tok_error),
            tok_setting ? atof(tok_setting) : 0.0
        );

        uart_write_bytes(RS485_UART, msg, strlen(msg));
        ESP_LOGI(TAG, "[RS485_CSV] Sent: %s", msg);

        // Small gap between rows so the receiver can process
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    fclose(f);
    xSemaphoreGiveRecursive(sd_mutex);

    ESP_LOGI(TAG, "[RS485_CSV] Today's CSV sent over RS485");
    const char *done_msg = "{\"type\":\"csv_done\"}\n";
    uart_write_bytes(RS485_UART, done_msg, strlen(done_msg));
    ESP_LOGI(TAG, "[RS485_CSV] csv_done signal sent");
    ESP_LOGI(TAG, "[RS485_CSV] Today's CSV sent over RS485"); 
}


static void rs485_csv_sync_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "[RS485_CSV] Boot: sending today's CSV");
    rs485_send_today_csv();

    int last_sent_hour = -1;

    while (1)
    {
        PCF_DateTime rtc;

        if (rtc_read(&rtc)) {

            int current_hour = rtc.hour;

            if (last_sent_hour < 0) {
                last_sent_hour = current_hour;
            }

            if (current_hour != last_sent_hour) {

                ESP_LOGI(TAG, "[RS485_CSV] Hour change → sending today's CSV");

                rs485_send_today_csv();

                last_sent_hour = current_hour;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ---------- forward declarations ---------- */

static void rs485_send_time_data(const PCF_DateTime *rtc)
{
    if (!rtc) return;
    char msg[96];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"time\",\"year\":%04d,\"month\":%02d,\"day\":%02d,\"hour\":%02d,\"min\":%02d,\"sec\":%02d}\n",
        rtc->year, rtc->month, rtc->day, rtc->hour, rtc->minute, rtc->second
    );
    uart_write_bytes(RS485_UART, msg, strlen(msg));
}

static void rs485_send_time(void)
{
    PCF_DateTime rtc = {0};
    if (rtc_read(&rtc)) {
        rs485_send_time_data(&rtc);
    }
}

static void rs485_rx_task(void *arg)
{
    uint8_t rx_buf[RS485_BUF_SIZE];
    char json_buf[RS485_BUF_SIZE];
    int json_len = 0;

    ESP_LOGI(TAG, "RS485 RX task started");

    while (1) {
        int len = uart_read_bytes(
            RS485_UART, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));

        for (int i = 0; i < len; i++) {
            char ch = (char)rx_buf[i];

            if (ch == '\r') {
                continue; // Ignore carriage return
            }

            if (ch == '\n') {
                if (json_len == 0) continue;
                json_buf[json_len] = '\0';
                json_len = 0;

                ESP_LOGI(TAG, "RS485 RX raw: %s", json_buf);

                cJSON *root = cJSON_Parse(json_buf);
                if (!root) {
                    ESP_LOGW(TAG, "Invalid JSON");
                    continue;
                }

                cJSON *cmd   = cJSON_GetObjectItem(root, "cmd");
                cJSON *id    = cJSON_GetObjectItem(root, "id");
                cJSON *state = cJSON_GetObjectItem(root, "state");

                if (cJSON_IsString(cmd) &&
                    strcmp(cmd->valuestring, "relay") == 0 &&
                    cJSON_IsNumber(id) &&
                    cJSON_IsBool(state)) {

                    ESP_LOGI(TAG,
                        "RS485 CMD: relay=%d state=%d",
                        id->valueint,
                        cJSON_IsTrue(state));

                    relay_set(id->valueint - 1, cJSON_IsTrue(state), SRC_RS485);
                }

                if (cJSON_IsString(cmd) &&
                    strcmp(cmd->valuestring, "set_setting_time") == 0) {
                    cJSON *sec = cJSON_GetObjectItem(root, "sec");
                    if (cJSON_IsNumber(sec) && sec->valueint > 0) {
                        setting_max_duration_sec = sec->valueint;
                        save_nvs_setting_limit(setting_max_duration_sec);
                        ESP_LOGI(TAG, "RS485 CMD: setting_max_duration_sec updated to %d sec (%d min)",
                                 setting_max_duration_sec, setting_max_duration_sec / 60);

                        // Send ACK confirmation back to HMI
                        char ack_msg[64];
                        snprintf(ack_msg, sizeof(ack_msg),
                            "{\"type\":\"set_setting_ack\",\"sec\":%d}\n",
                            setting_max_duration_sec
                        );
                        uart_write_bytes(RS485_UART, ack_msg, strlen(ack_msg));
                        ESP_LOGI(TAG, "RS485 ACK sent to HMI: %s", ack_msg);
                    }
                }

                if (cJSON_IsString(cmd) &&
                    strcmp(cmd->valuestring, "get_time") == 0) {
                    ESP_LOGI(TAG, "RS485 CMD: get_time received -> sending current time to HMI");
                    rs485_send_time();
                }

                if (cJSON_IsString(cmd) &&
                    strcmp(cmd->valuestring, "set_time") == 0) {
                    cJSON *yr = cJSON_GetObjectItem(root, "year");
                    cJSON *mo = cJSON_GetObjectItem(root, "month");
                    cJSON *dy = cJSON_GetObjectItem(root, "day");
                    cJSON *hr = cJSON_GetObjectItem(root, "hour");
                    cJSON *mn = cJSON_GetObjectItem(root, "min");
                    cJSON *sc = cJSON_GetObjectItem(root, "sec");
                    if (cJSON_IsNumber(yr) && cJSON_IsNumber(mo) && cJSON_IsNumber(dy) &&
                        cJSON_IsNumber(hr) && cJSON_IsNumber(mn) && cJSON_IsNumber(sc)) {
                        PCF_DateTime newRTC = {
                            .year   = (uint16_t)yr->valueint,
                            .month  = (uint8_t)mo->valueint,
                            .day    = (uint8_t)dy->valueint,
                            .hour   = (uint8_t)hr->valueint,
                            .minute = (uint8_t)mn->valueint,
                            .second = (uint8_t)sc->valueint,
                            .weekday = 0
                        };
                        if (PCF_SetDateTime(&newRTC) == 0) {
                            ESP_LOGI(TAG, "RTC updated via RS485 set_time: %04d-%02d-%02d %02d:%02d:%02d",
                                     newRTC.year, newRTC.month, newRTC.day, newRTC.hour, newRTC.minute, newRTC.second);
                        } else {
                            ESP_LOGE(TAG, "Failed to update RTC via set_time");
                        }
                        rs485_send_time();
                    }
                }

                cJSON_Delete(root);

            } else if (json_len < sizeof(json_buf) - 1) {
                json_buf[json_len++] = ch;
            } else {
                json_len = 0;
            }
        }
    }
}

esp_err_t status_http_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"relay1\":%s,\"relay2\":%s}",
        relay_state[0] ? "true" : "false",
        relay_state[1] ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}


/* ================= HTTP HANDLER ================= */
esp_err_t relay_http_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];

        if (httpd_query_key_value(query, "relay1", val, sizeof(val)) == ESP_OK) {
            relay_set(RELAY1, strcasecmp(val, "ON") == 0, SRC_HTTP);
        }
        if (httpd_query_key_value(query, "relay2", val, sizeof(val)) == ESP_OK) {
            relay_set(RELAY2, strcasecmp(val, "ON") == 0, SRC_HTTP);
        }

        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No query params");
    return ESP_FAIL;
}

static httpd_handle_t start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.ctrl_port        = 32767;   // ← unique ctrl port for server 1
    cfg.max_uri_handlers = 8;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) == ESP_OK) {

        httpd_uri_t relay_uri = {
            .uri = "/relay",
            .method = HTTP_GET,
            .handler = relay_http_handler
        };

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_http_handler
        };

        httpd_register_uri_handler(server, &relay_uri);
        httpd_register_uri_handler(server, &status_uri);
    }

    return server;
}

/* 
    RS485 is a communication standard that allows devices to exchange data over wires, 
    including over relatively long distances in industrial environments.
    
    UART is a hardware communication system that allows two electronic 
    devices to send and receive data serially, one bit at a time.
*/
static void rs485_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(RS485_UART, RS485_BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(RS485_UART, &uart_config);
    uart_set_pin(RS485_UART, RS485_TXD, RS485_RXD, RS485_RTS, UART_PIN_NO_CHANGE);
    uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX);

    ESP_LOGI(TAG, "RS485 initialized");
}


static bool relay_load_from_sd(void)
{
    FILE *f = fopen(RELAY_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG, "No relay retention file");
        return false;
    }

    char line[64];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        ESP_LOGW(TAG, "Relay file empty");
        return false;
    }
    fclose(f);

    int r1, r2;
    if (sscanf(line, "R1=%d,R2=%d", &r1, &r2) != 2) {
        ESP_LOGW(TAG, "Relay file format invalid");
        return false;
    }

    if ((r1 < 0 || r1 > 1) || (r2 < 0 || r2 > 1)) {
        ESP_LOGW(TAG, "Relay file value out of range");
        return false;
    }

    relay_state[RELAY1] = r1;
    relay_state[RELAY2] = r2;

    ESP_LOGI(TAG, "Relay state loaded: R1=%d R2=%d", r1, r2);
    return true;
}

static void relay_apply_loaded_state(void)
{
    for (int i = 0; i < 2; i++) {
        esp_io_expander_set_level(
            io_expander,
            (1 << i),
            relay_state[i] ? 0 : 1   // active-low relay
        );
    }

    rs485_send_status();   // sync display
}

static void rs485_boot_sync_task(void *arg)
{
    /* Wait for display to boot */
    vTaskDelay(pdMS_TO_TICKS(1500));   // 1.5–2s is typical for HMI

    ESP_LOGI(TAG, "Boot sync: sending retained relay state to display");
    rs485_send_status();
    rs485_send_time();

    vTaskDelete(NULL);
}

static void rs485_minute_sender_task(void *arg)
{
    MinuteRS485Data data;

    while (1) {

        if (xQueueReceive(rs485_minute_queue, &data, portMAX_DELAY)) {

            char msg[256];

            snprintf(msg, sizeof(msg),
                "{\"type\":\"minute\",\"year\":%04d,\"month\":%02d,\"day\":%02d,"
                "\"hour\":%02d,\"minute\":%02d,\"run\":%d,\"idle\":%d,\"error\":%d,\"setting\":%d,\"set_limit\":%d}\n",
                data.year,
                data.month,
                data.day,
                data.hour,
                data.minute,
                data.run_sec,
                data.idle_sec,
                data.error_sec,
                data.setting_sec,
                setting_max_duration_sec
            );

            uart_write_bytes(RS485_UART, msg, strlen(msg));

            ESP_LOGI(TAG,
                "[RS485_MIN] %02d:%02d Run=%d Idle=%d Error=%d Setting=%d",
                data.hour,
                data.minute,
                data.run_sec,
                data.idle_sec,
                data.error_sec,
                data.setting_sec
            );
        }
    }
}

static int64_t load_last_saved_epoch(void)
{
    nvs_handle_t nvs_handle;
    int64_t epoch = 0;

    esp_err_t err = nvs_open("oee_counters", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        if (nvs_get_i64(nvs_handle, "last_epoch", &epoch) != ESP_OK) {
            epoch = 0;
        }
        nvs_close(nvs_handle);
    }

    return epoch;
}

static void write_poweroff_csv(time_t from_epoch, time_t to_epoch)
{
    if (!sdcard_ini || sd_mutex == NULL) {
        ESP_LOGW(TAG, "[PO] SD not ready, skip write");
        return;
    }

    struct tm from_tm;
    struct tm to_tm;

    localtime_r(&from_epoch, &from_tm);
    localtime_r(&to_epoch, &to_tm);

    char path[128];

    // 🔥 IMPORTANT: file based on FROM time
    snprintf(path, sizeof(path), "%s/%sP%02d%02d.csv",
             MOUNT_POINT,
             MACHINE_CODE_STR,
             from_tm.tm_mon + 1,
             from_tm.tm_mday);

    ensure_file_with_header(path, "from_time,to_time");

    char from_str[32];
    char to_str[32];

    strftime(from_str, sizeof(from_str), "%Y-%m-%d %H:%M:%S", &from_tm);
    strftime(to_str, sizeof(to_str), "%Y-%m-%d %H:%M:%S", &to_tm);

    xSemaphoreTakeRecursive(sd_mutex, portMAX_DELAY);

    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s,%s\n", from_str, to_str);
        fclose(f);

        ESP_LOGI(TAG, "[PO] Logged: %s → %s in %s",
                 from_str, to_str, path);
    } else {
        ESP_LOGE(TAG, "[PO] File open failed: %s (errno=%d: %s)",
                 path, errno, strerror(errno));
    }

    xSemaphoreGiveRecursive(sd_mutex);
}

static void handle_poweroff_on_boot(void)
{
    int64_t last_epoch = load_last_saved_epoch();

    if (last_epoch == 0) {
        ESP_LOGW(TAG, "[PO] No last_epoch found → skip");
        return;
    }

    PCF_DateTime rtc;
    if (!rtc_read(&rtc)) {
        ESP_LOGE(TAG, "[PO] RTC read failed → skip");
        return;
    }

    time_t boot_epoch = rtc_to_epoch(&rtc);

    if (boot_epoch <= last_epoch) {
        ESP_LOGW(TAG, "[PO] Invalid time (boot <= last) → skip");
        return;
    }

    int64_t diff = boot_epoch - last_epoch;

    if (diff < PO_MIN_DURATION_SEC) {
        ESP_LOGI(TAG, "[PO] Short gap (%lld sec < %d sec) → ignore",
                diff, PO_MIN_DURATION_SEC);
        return;
    }

    ESP_LOGI(TAG, "[PO] Power OFF detected: %lld sec", diff);

    // ✅ write CSV
    write_poweroff_csv((time_t)last_epoch, boot_epoch);
}

// MAIN
void app_main(void)
{
    // NVS initialization
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    load_nvs_setting_limit();

    // Configures the software timezone to Indian Standard Time 
    // This code sets the timezone of your ESP32-S3 to Indian Standard Time (IST)
    setenv("TZ", "IST-5:30", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to IST");

    // SD card initialization
    int retry = 0;
    while (!sdcard_ini && retry < SD_INIT_MAX_RETRIES) {
        ESP_LOGI(TAG, "Initializing SD card (attempt %d/%d)...", retry + 1, SD_INIT_MAX_RETRIES);
        esp_err_t ret = sdcard_init();
        if (ret == ESP_OK) break;
        retry++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (!sdcard_ini) {
        ESP_LOGE(TAG, "SD card initialization failed after %d attempts", SD_INIT_MAX_RETRIES);
    }

    // WIFI Initialization Function Calls
    wifi_init(); 

    /* I2C Initialization for ESP32 to HMI Display Connect */
    ESP_ERROR_CHECK(i2c_bus_init());
    
    /* Creates the TCA9554 driver and configures all eight expander pins as outputs */
    ESP_ERROR_CHECK(tca_init());
    
    
    /* 
        Calls "PCF_AttachBus(i2c_handle)"" to register the PCF85063A 
        external hardware Real-Time Clock (RTC) chip on the same I2C bus 
    */
    ESP_ERROR_CHECK(PCF_AttachBus(i2c_handle)); 


    /* This code configures ESP32 GPIO pins for two possible purposes:
            Mode 1 — Read real inputs: GPIO 6 and GPIO 7 work as digital inputs. The ESP32 reads their signals to determine the machine state.
            Mode 2 — Control outputs: GPIO 6 and GPIO 7 work as digital outputs. The ESP32 sets their levels HIGH or LOW.
        your firmware uses Mode 1 and reads GPIO 6 and GPIO 7 as inputs. It does not execute the output configuration in the #else section.
    */

#define INPUT_PULLUP_EN 1
#define INPUT_PULLDOWN_EN 0

#if USE_REAL_INPUT_PINS
    gpio_config_t in_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = ((1ULL << INPUT1_GPIO) | (1ULL << INPUT2_GPIO) | (1ULL << INPUT5_GPIO)),
        .pull_up_en = INPUT_PULLUP_EN ? 1 : 0,
        .pull_down_en = INPUT_PULLDOWN_EN ? 1 : 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&in_conf);
    ESP_LOGI("GPIO", "Reading real inputs: INPUT1=%d INPUT2=%d INPUT5=%d active_low=%d pu=%d pd=%d",
             (int)INPUT1_GPIO, (int)INPUT2_GPIO, (int)INPUT5_GPIO, (int)INPUT_ACTIVE_LOW, (int)INPUT_PULLUP_EN, (int)INPUT_PULLDOWN_EN);
#else
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << RELAY1_GPIO) | (1ULL << RELAY2_GPIO)),
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY1_GPIO, 0);
    gpio_set_level(RELAY2_GPIO, 0);
#endif

         /* 3. the ESP32 only reads the relay state from a file eg: R1 = 0, R2 = 1  */
    bool have_retention = relay_load_from_sd();

    rs485_init();

    /* 4. Apply retained state OR default OFF */
    if (have_retention) {
        relay_apply_loaded_state();
    } else {
        for (int i = 0; i < 2; i++) {
            relay_state[i] = false;
            esp_io_expander_set_level(io_expander, (1 << i), 1);
        }
    }

    /* 5. Start communication */

    start_http();
    

    /* 6. Init DI and tasks */
    di_gpio_init();

    // Ethernet init
    ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    ethernet_init();

    // Initialize I2C for RTC
    rtc_mutex = xSemaphoreCreateMutex();
    if (rtc_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create RTC mutex!");
    }
    obtain_time();

    vTaskDelay(pdMS_TO_TICKS(2000));

    // 🔥 Power-off detection
    handle_poweroff_on_boot();

    // Start relay server 
    start_relay_server();
    ESP_LOGI(TAG, "HTTP relay server started");
    vTaskDelay(pdMS_TO_TICKS(2000)); // let Ethernet and RTC stabilize
    state_queue = xQueueCreate(10, sizeof(int));
    rs485_minute_queue = xQueueCreate(10, sizeof(MinuteRS485Data));

    // Start logger task 
    ESP_LOGI(TAG, "Starting logger task...");
    xTaskCreatePinnedToCore(logger_task_counter, "logger_task_counter", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(logger_task_csv, "logger_task_csv", 8192, NULL, 4, NULL, 0);

    if (sdcard_ini) {
    list_sdcard_csv_files();
}
    // Start offline upload task
    xTaskCreatePinnedToCore(offline_upload_task, "offline_upload_task", 8192, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(rtc_boot_sync_task, "rtc_boot_sync", 4096, NULL, 6, NULL,0);
    
    xTaskCreate(
        di_monitor_task,
        "di_monitor_task",
        4096,
        NULL,
        9,
        NULL
    );

    xTaskCreate(
        rs485_rx_task,
        "rs485_rx_task",
        4096,
        NULL,
        10,
        NULL
    );

    ESP_LOGI(TAG, "System ready with retentive relay state");
    system_ready = true;
    xTaskCreate(
        rs485_boot_sync_task,
        "rs485_boot_sync_task",
        2048,
        NULL,
        5,
        NULL
    );

    xTaskCreate(
        rs485_csv_sync_task,
        "rs485_csv_sync",
        4096,
        NULL,
        4,  
        NULL
    );

    xTaskCreate(state_http_task, "state_http_task", 4096, NULL, 6, NULL);
    xTaskCreate(
        rs485_minute_sender_task,
        "rs485_minute_sender",
        4096,
        NULL,
        4,
        NULL
    );

}

