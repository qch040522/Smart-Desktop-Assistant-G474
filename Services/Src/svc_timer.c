/**
 * @file    svc_timer.c
 * @brief   计时服务实现。
 */
#include <stddef.h>
#include "svc_timer.h"
#include "bsp_rtc.h"
#include "bsp_buzzer.h"

typedef enum { POMO_IDLE = 0, POMO_RUN, POMO_PAUSE } pomo_state_e;

static uint32_t s_cur_sec = 0u;      /* 本次学习秒(会话内) */
static uint32_t s_today_sec = 0u;
static uint32_t s_total_sec = 0u;
static uint32_t s_last_ymd = 0u;     /* 上次跨天检查日期 */

static uint16_t s_pomo_total = POMO_DEFAULT_MINUTES; /* 分钟 */
static uint8_t  s_pomo_en = 1u;
static pomo_state_e s_pomo = POMO_IDLE;
static uint32_t s_pomo_remain = 0u;

static uint8_t  s_alarm_hh = 7u, s_alarm_mm = 0u, s_alarm_en = 0u;
static uint8_t  s_alarm_ringing = 0u;
static uint32_t s_ring_start_ms = 0u;

void SvcTimer_Init(const app_timing_snapshot_t *snap)
{
  if (snap != NULL)
  {
    s_total_sec = snap->study_total_sec;
    s_today_sec = snap->study_today_sec;
    s_last_ymd  = snap->last_date_ymd;
  }
  else
  {
    s_total_sec = 0u;
    s_today_sec = 0u;
    s_last_ymd  = 0u;
  }

  if (s_last_ymd != BspRtc_GetYmd())
  {
    /* 跨天: 今日清零 */
    s_today_sec = 0u;
    s_last_ymd  = BspRtc_GetYmd();
  }
}

void SvcTimer_Tick(sys_mode_t mode, uint32_t now_ms)
{
  uint32_t ymd = BspRtc_GetYmd();
  if (ymd != s_last_ymd)
  {
    s_today_sec = 0u;          /* RTC 跨天清零今日时长 */
    s_last_ymd = ymd;
  }

  /* 学习时长累加 */
  if (mode == SYS_MODE_STUDY)
  {
    s_cur_sec++;
    s_today_sec++;
    s_total_sec++;
  }
  else
  {
    s_cur_sec = 0u;            /* 退出学习重计 */
  }

  /* 番茄钟 */
  if ((s_pomo == POMO_RUN) && (s_pomo_remain > 0u))
  {
    s_pomo_remain--;
    if (s_pomo_remain == 0u)
    {
      /* 到点: 蜂鸣提示 3 秒 */
      s_pomo = POMO_IDLE;
      BspBuzzer_BeepOn(4000u);
    }
  }
  else if ((s_pomo == POMO_RUN) && (s_pomo_remain == 0u))
  {
    /* 已结束, 保持空闲 */
    s_pomo = POMO_IDLE;
  }

  /* 闹钟: 检查时分匹配 */
  if (s_alarm_en && !s_alarm_ringing)
  {
    uint8_t h, m, s;
    BspRtc_GetTime(&h, &m, &s);
    if ((h == s_alarm_hh) && (m == s_alarm_mm) && (s < 5u))
    {
      s_alarm_ringing = 1u;
      s_ring_start_ms = now_ms;
      BspBuzzer_BeepOn(3000u);
    }
  }
  if (s_alarm_ringing)
  {
    if ((now_ms - s_ring_start_ms) >= ALARM_RING_MS)
    {
      s_alarm_ringing = 0u;
      BspBuzzer_BeepOff();
    }
  }
}

/* ---- 时长 ---- */
uint32_t SvcTimer_CurSec(void)   { return s_cur_sec; }
uint32_t SvcTimer_TodaySec(void) { return s_today_sec; }
uint32_t SvcTimer_TotalSec(void) { return s_total_sec; }

void SvcTimer_ResetTotal(void)   { s_total_sec = 0u; }
/* ---- 番茄钟 ---- */
void SvcTimer_PomoSetMin(uint16_t min)
{
  if (min == 0u) min = 1u;
  s_pomo_total = min;
}
void SvcTimer_PomoEnable(uint8_t en) { s_pomo_en = en ? 1u : 0u; }

void SvcTimer_PomoStart(void)
{
  if (s_pomo == POMO_IDLE)
  {
    s_pomo = POMO_RUN;
    s_pomo_remain = (uint32_t)s_pomo_total * 60u;
  }
  else if (s_pomo == POMO_PAUSE)
  {
    s_pomo = POMO_RUN;
  }
}

void SvcTimer_PomoPause(void)
{
  if (s_pomo == POMO_RUN) s_pomo = POMO_PAUSE;
}

void SvcTimer_PomoReset(void)
{
  s_pomo = POMO_IDLE;
  s_pomo_remain = 0u;
}

uint8_t  SvcTimer_PomoState(void)      { return (uint8_t)s_pomo; }
uint32_t SvcTimer_PomoRemainSec(void)  { return s_pomo_remain; }

/* ---- 闹钟 ---- */
void SvcTimer_AlarmSet(uint8_t h, uint8_t m)
{
  s_alarm_hh = h;
  s_alarm_mm = m;
}
void SvcTimer_AlarmEnable(uint8_t en)
{
  s_alarm_en = en ? 1u : 0u;
  if (!s_alarm_en)
  {
    s_alarm_ringing = 0u;
    BspBuzzer_BeepOff();
  }
}
void SvcTimer_AlarmAck(void)
{
  s_alarm_ringing = 0u;
  BspBuzzer_BeepOff();
}
uint8_t SvcTimer_AlarmRinging(void) { return s_alarm_ringing; }
uint8_t SvcTimer_AlarmEnabled(void) { return s_alarm_en; }
uint8_t SvcTimer_AlarmHour(void)    { return s_alarm_hh; }
uint8_t SvcTimer_AlarmMin(void)     { return s_alarm_mm; }

void SvcTimer_BuildSnapshot(app_timing_snapshot_t *snap)
{
  if (snap == NULL) return;
  snap->magic           = FLASH_TIM_MAGIC;
  snap->version         = 1u;
  snap->study_total_sec = s_total_sec;
  snap->study_today_sec = s_today_sec;
  snap->last_date_ymd   = BspRtc_GetYmd();
}