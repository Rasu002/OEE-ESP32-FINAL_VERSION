#ifndef __PCF85063A_H__
#define __PCF85063A_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_system.h"
#include <time.h>
#include <sys/time.h>
#include "driver/i2c_master.h"   // ← ADD THIS

// ... keep all your existing #defines ...

#define SDA_PIN         42
#define SCL_PIN         41
#define I2C_MASTER_CLK  100000

typedef struct {
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
} PCF_Alarm;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint16_t year;
} PCF_DateTime;

// ← REPLACE i2c_master_driver_initialize with this:
esp_err_t PCF_AttachBus(i2c_master_bus_handle_t bus_handle);

int PCF_Init(void);
esp_err_t PCF_Write(uint8_t addr, uint8_t *data, size_t count);
esp_err_t PCF_Read(uint8_t addr, uint8_t *data, size_t count);
int PCF_SetDateTime(PCF_DateTime *dateTime);
int PCF_GetDateTime(PCF_DateTime *dateTime);
int PCF_rtcUpdateSYSTEM(void);
int PCF_systemUpdateRTC(void);
int PCF_updateRTC(struct tm *tm_str);

#endif