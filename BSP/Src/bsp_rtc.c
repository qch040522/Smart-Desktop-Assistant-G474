/**
 * @file    bsp_rtc.c
 * @brief   RTC 读取/设置实现（注意: 先读时间再读日期以解锁寄存器）。
 */
#include "bsp.h"
#include "bsp_rtc.h"

void BspRtc_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
  RTC_TimeTypeDef t = {0};
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  if (hour) *hour = t.Hours;
  if (min)  *min  = t.Minutes;
  if (sec)  *sec  = t.Seconds;
}

void BspRtc_GetDate(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday)
{
  RTC_DateTypeDef d = {0};
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  if (year)    *year    = d.Year;
  if (month)   *month   = d.Month;
  if (day)     *day     = d.Date;
  if (weekday) *weekday = d.WeekDay;
}

uint32_t BspRtc_GetYmd(void)
{
  uint8_t y, m, d, w;
  BspRtc_GetDate(&y, &m, &d, &w);
  return ((uint32_t)(2000u + y) * 10000u) + ((uint32_t)m * 100u) + (uint32_t)d;
}

int BspRtc_SetDateTime(uint8_t year, uint8_t month, uint8_t day,
                       uint8_t weekday, uint8_t hour, uint8_t min, uint8_t sec)
{
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};

  t.Hours = hour;
  t.Minutes = min;
  t.Seconds = sec;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
  {
    return -1;
  }
  d.Year    = year;
  d.Month   = month;
  d.Date    = day;
  d.WeekDay = weekday;
  if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)
  {
    return -1;
  }
  return 0;
}