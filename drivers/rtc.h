#pragma once
#include "../kernel/types.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;      // ano completo (ex: 2026)
    uint8_t weekday;    // 1 = domingo ... 7 = sábado
} rtc_time_t;

void rtc_init(void);
void rtc_read_time(rtc_time_t* t);

// Retorna uma string formatada "HH:MM:SS" (buffer deve ter >=9 bytes)
void rtc_format_time(char* buf, const rtc_time_t* t);
// Retorna "DD/MM/AAAA" (buffer >=11 bytes)
void rtc_format_date(char* buf, const rtc_time_t* t);