/**
 * @file PCF85063A.c
 * @author Victor Alberti Costa (https://github.com/costa-victor)
 * @brief Driver for the RTC PCF85063A:
 * DATASHEET: https://pdf1.alldatasheet.com/datasheet-pdf/view/1255455/NXP/PCF85063A.html
 * @version 0.1
 * @date 2022-03-04
 *
 */

#include "esp_system.h"
#include "PCF85063A.h"
// #include "i2cdev.h"
// #include "driver/i2c.h"
#include "timegm.h"
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h> 
#include "driver/i2c_master.h"   // new driver only
#include <sys/time.h>


#define PCF85063A_ADDR  0x51
#define PCF_I2C_TIMEOUT_MS  100

#define BinToBCD(bin) ((((bin) / 10) << 4) + ((bin) % 10))

// static esp_err_t last_i2c_err = ESP_OK; 


static i2c_master_dev_handle_t s_pcf_dev = NULL;

// Call this once after i2c_bus_init(), passing the same bus handle
esp_err_t PCF_AttachBus(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCF85063A_ADDR,
        .scl_speed_hz    = 100000,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_pcf_dev);
}

esp_err_t PCF_Write(uint8_t addr, uint8_t *data, size_t length)
{
    if (!s_pcf_dev) return ESP_ERR_INVALID_STATE;

    // Transmit: [reg_addr | data bytes] in one buffer
    uint8_t buf[length + 1];
    buf[0] = addr;
    memcpy(&buf[1], data, length);

    return i2c_master_transmit(s_pcf_dev, buf, length + 1, PCF_I2C_TIMEOUT_MS);
}

esp_err_t PCF_Read(uint8_t addr, uint8_t *data, size_t length)
{
    if (!s_pcf_dev) return ESP_ERR_INVALID_STATE;

    // Write register address, then read back data
    return i2c_master_transmit_receive(
        s_pcf_dev,
        &addr, 1,       // write: register address
        data, length,   // read: data bytes
        PCF_I2C_TIMEOUT_MS
    );
}


// #define SDA_PIN  42
// #define SCL_PIN  41
#define I2C_PORT I2C_NUM_0
#define PCF85063A_ADDR 0x51




/**
 * @brief Config and initialize the I2C
 *
 * @return esp_err_t
 */
// esp_err_t i2c_master_driver_initialize(void)
// {
// 	// Check your used i2c port, it may be different according to your project
// 	// i2c_config_t conf = {
// 	// 	.mode = I2C_MODE_MASTER,
// 	// 	.sda_io_num = SDA_PIN,
// 	// 	.sda_pullup_en = GPIO_PULLUP_ENABLE,
// 	// 	.scl_io_num = SCL_PIN,
// 	// 	.scl_pullup_en = GPIO_PULLUP_ENABLE,
// 	// 	.master.clk_speed = I2C_MASTER_CLK};
// 	// return i2c_param_config(I2C_NUM_0, &conf);
// 	return i2c_dev_init(I2C_NUM_0, SDA_PIN, SCL_PIN);
// }


/**
 * @brief Writes an amount of data to the specified register using I2C
 *
 * @param addr Register address
 * @param data Data
 * @param length Data length in bytes
 * @return esp_err_t Error type
 */
// esp_err_t PCF_Write(uint8_t addr, uint8_t *data, size_t length)
// {
//     i2c_dev_t dev = {
//         .port = I2C_PORT,
//         .addr = PCF85063A_ADDR,  // Example: 0x51
//     };
// 	// i2c_master_driver_initialize();

//     // Write to given register
//     return i2c_dev_write(&dev, &addr, 1, data, length);
// }



/**
 * @brief Reads an amount of data to the specified register using I2C
 *
 * @param addr Register address
 * @param data Data
 * @param length Data length in bytes
 * @return esp_err_t Error type
 */
// esp_err_t PCF_Read(uint8_t addr, uint8_t *data, size_t length)
// {
//     i2c_dev_t dev = {
//         .port = I2C_PORT,
//         .addr = PCF85063A_ADDR,  // Example: 0x51
//     };
// 	// i2c_master_driver_initialize();

//     return i2c_dev_read(&dev, &addr, 1, data, length);
// }



/**
 * @brief Initialize the control register to Write/Read on the RTC
 *
 * @return int -1 if write fails, 0 if its okay
 */
int PCF_Init(void)
{
	static bool init = false;
	if (!init)
	{
		uint8_t tmp = 0b00000000;
		esp_err_t ret = PCF_Write(0x00, &tmp, 1); 	// Control register 1
		if (ret != ESP_OK)
		{
			return -1;
		}

		tmp = 0b10110000;
		ret = PCF_Write(0x01, &tmp, 1); 	// Control register 2
		if (ret != ESP_OK)
		{
			return -1;
		}

		tmp = 0b00000000;
		ret = PCF_Write(0x02, &tmp, 1); 	// Offset register
		if (ret != ESP_OK)
		{
			return -1;
		}

		tmp = 0b00000000;
		ret = PCF_Write(0x03, &tmp, 1); 	// RAM register
		if (ret != ESP_OK)
		{
			return -1;
		}
		init = true;
	}
	return 0;
}


/**
 * @brief Updates RTC's timestamp from a PCF_DateTime structure
 * 
 * @param dateTime Points to a PCF_DataTime structure type
 * @return int 0 if success, -1 if fails write in register, -2 if there is an invalid parameter
 */
int PCF_SetDateTime(PCF_DateTime *dateTime)
{
	// Search for invalid parameters
	if (dateTime->second >= 60 || dateTime->minute >= 60 || dateTime->hour >= 24 || dateTime->day > 32 || dateTime->weekday > 6 || dateTime->month > 12 || dateTime->year < TM_YEAR_BASE || dateTime->year >= 2100)
	{
		return -2;
	}

	uint8_t buffer[7];

	buffer[0] = BinToBCD(dateTime->second) & 0x7F;
	buffer[1] = BinToBCD(dateTime->minute) & 0x7F;
	buffer[2] = BinToBCD(dateTime->hour) & 0x3F;
	buffer[3] = BinToBCD(dateTime->day) & 0x3F;
	buffer[4] = BinToBCD(dateTime->weekday) & 0x07;
	buffer[5] = BinToBCD(dateTime->month) & 0x1F;
	buffer[6] = BinToBCD(dateTime->year - TM_YEAR_BASE);

	esp_err_t ret = PCF_Write(0x04, buffer, sizeof(buffer));
	if (ret != ESP_OK)
	{
		return -1;
	}

	return 0;
}

/**
 * @brief Updates a PCF_DateTime structure from the RTC's timestamp
 * 
 * @param dateTime Points to a PCF_DataTime structure type
 * @return int 1 if clock integrity isn't guaranted, 0 if success, -1 if read fails
 */
int PCF_GetDateTime(PCF_DateTime *dateTime)
{
	uint8_t buffer[7];
	esp_err_t ret;

	ret = PCF_Read(0x04, buffer, sizeof(buffer));
	if (ret != ESP_OK)
	{
		return -1;
	}

	dateTime->second = (((buffer[0] >> 4) & 0x07) * 10) + (buffer[0] & 0x0F);
	dateTime->minute = (((buffer[1] >> 4) & 0x07) * 10) + (buffer[1] & 0x0F);
	dateTime->hour = (((buffer[2] >> 4) & 0x03) * 10) + (buffer[2] & 0x0F);
	dateTime->day = (((buffer[3] >> 4) & 0x03) * 10) + (buffer[3] & 0x0F);
	dateTime->weekday = (buffer[4] & 0x07);
	dateTime->month = ((buffer[5] >> 4) & 0x01) * 10 + (buffer[5] & 0x0F);
	dateTime->year = (TM_YEAR_BASE + 0) + ((buffer[6] >> 4) & 0x0F) * 10 + (buffer[6] & 0x0F);

	if (buffer[0] & 0x80) 	// Clock integrity not guaranted
	{
		return 1;
	}

	return 0;
}

/**
 * @brief Updates the system time from the RTC date
 * 
 * @return int 1 if clock integrity isn't guaranted, 0 if success, -1 if read fails
 */
int PCF_rtcUpdateSYSTEM(void)
{
	int ret = 0;
	PCF_DateTime date = {0};
	struct tm tm = {0};
	struct timeval tv = {0};

	ret = PCF_Init();
	if (ret != 0)
	{
		goto fail;
	}
	ret = PCF_GetDateTime(&date);
	if (ret != 0)
	{
		goto fail;
	}
	tm.tm_sec = date.second;
	tm.tm_min = date.minute;
	tm.tm_hour = date.hour;
	tm.tm_mday = date.day;
	tm.tm_mon = date.month - 1;
	tm.tm_year = date.year - TM_YEAR_BASE;

	tv.tv_sec = timegm(&tm);
	tv.tv_usec = 0;
	ret = settimeofday(&tv, NULL);
fail:
	return ret;
}

/**
 * @brief Updates the RTC date from the system time
 * 
 * @return int 1 if clock integrity isn't guaranted, 0 if success, -1 if read fails
 */
int PCF_systemUpdateRTC(void)
{
	int ret;
	PCF_DateTime date = {0};
	struct tm tm = {0};

	ret = PCF_Init();
	if (ret != 0)
	{
		goto fail;
	}

	time_t now = time(NULL);
	gmtime_r(&now, &tm);
	date.second = tm.tm_sec;
	date.minute = tm.tm_min;
	date.hour = tm.tm_hour;
	date.day = tm.tm_mday;
	date.month = tm.tm_mon + 1;
	date.year = tm.tm_year + TM_YEAR_BASE;
	date.weekday = tm.tm_wday;

	ret = PCF_SetDateTime(&date);

fail:
	return ret;
}

/**
 * @brief Updates only the RTC time from a struct time passed as reference
 * 
 * @param tm_str Struct timer pointer
 * @return int -1 if write fails, 0 if its okay
 */
int PCF_updateRTC(struct tm *tm_str)
{
	PCF_DateTime date = {0};
	int ret;

	ret = PCF_Init();
	if (ret != 0)
	{
		return -1;
	}

	date.second = tm_str->tm_sec;
	date.minute = tm_str->tm_min;
	date.hour = tm_str->tm_hour;
	date.day = tm_str->tm_mday;
	date.month = tm_str->tm_mon + 1;
	date.year = tm_str->tm_year + TM_YEAR_BASE;
	date.weekday = tm_str->tm_wday;

	return PCF_SetDateTime(&date);
}