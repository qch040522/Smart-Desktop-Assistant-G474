/**
 * @file    bsp_rtc.c
 * @brief   RTC 读取/设置实现（BYPSHAD 旁路影子寄存器, 避免锁存导致的读取滞后）。
 */
#include "bsp.h"
#include "bsp_rtc.h"

/* 开启 BYPSHAD: TR/DR 直接反映实际值, 无影子锁存(解决时间读取滞后) */
static void rtc_enable_bypshad(void)
{
  /* 每次检查是否已生效, 失败则重试(复位后 DBP/写保护需重新使能) */
  if ((hrtc.Instance->CR & RTC_CR_BYPSHAD) != 0u) return;
  HAL_PWR_EnableBkUpAccess();                    /* 使能备份域访问(DBP) */
  __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);      /* RTC 写保护键序列解锁 */
  SET_BIT(hrtc.Instance->CR, RTC_CR_BYPSHAD);
  __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
}

void BspRtc_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};
  rtc_enable_bypshad();
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  if (hour) *hour = t.Hours;
  if (min)  *min  = t.Minutes;
  if (sec)  *sec  = t.Seconds;
}

void BspRtc_GetDate(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday)
{
  RTC_DateTypeDef d = {0};
  RTC_TimeTypeDef t = {0};
  rtc_enable_bypshad();
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
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