#include "rtc.h"
#include "../kernel/types.h"

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_WEEKDAY   0x06
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

static int cmos_update_in_progress(void) {
    return cmos_read(RTC_STATUS_A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void rtc_init(void) {

}

void rtc_read_time(rtc_time_t* t) {
    // Espera o fim de uma atualização em andamento
    while (cmos_update_in_progress());

    t->second = cmos_read(RTC_SECONDS);
    t->minute = cmos_read(RTC_MINUTES);
    t->hour   = cmos_read(RTC_HOURS);
    t->day    = cmos_read(RTC_DAY);
    t->month  = cmos_read(RTC_MONTH);
    t->year   = cmos_read(RTC_YEAR);
    t->weekday= cmos_read(RTC_WEEKDAY);

    // Verifica se o formato é BCD (bit 2 do Status B = 0)
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    if (!(status_b & 0x04)) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour   = bcd_to_bin(t->hour);
        t->day    = bcd_to_bin(t->day);
        t->month  = bcd_to_bin(t->month);
        t->year   = bcd_to_bin(t->year);
    }

    // Ajusta o ano: CMOS guarda apenas os dois últimos dígitos.
    // (válido até 2099)
    t->year += 2000;

    if (!(status_b & 0x02) && (t->hour & 0x80)) {
        // Hora em formato 12h, PM
        t->hour = ((t->hour & 0x7F) + 12) % 24;
    }
}

void rtc_format_time(char* buf, const rtc_time_t* t) {
    buf[0] = '0' + (t->hour   / 10);  buf[1] = '0' + (t->hour   % 10);
    buf[2] = ':';
    buf[3] = '0' + (t->minute / 10);  buf[4] = '0' + (t->minute % 10);
    buf[5] = ':';
    buf[6] = '0' + (t->second / 10);  buf[7] = '0' + (t->second % 10);
    buf[8] = 0;
}

void rtc_format_date(char* buf, const rtc_time_t* t) {
    buf[0] = '0' + (t->day / 10);     buf[1] = '0' + (t->day % 10);
    buf[2] = '/';
    buf[3] = '0' + (t->month / 10);   buf[4] = '0' + (t->month % 10);
    buf[5] = '/';
    // Ano com 4 dígitos
    uint16_t y = t->year;
    buf[6] = '0' + (y / 1000);        y %= 1000;
    buf[7] = '0' + (y / 100);         y %= 100;
    buf[8] = '0' + (y / 10);          buf[9] = '0' + (y % 10);
    buf[10] = 0;
}