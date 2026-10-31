#pragma once
#include <stdbool.h>

void rs485_send_cmd(int relay, bool on);
void rs485_request_oee_date(const char *date_iso);
void rs485_send_setting_limit(int seconds);

#include <stdint.h>
void rs485_request_time(void);
void rs485_send_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);
