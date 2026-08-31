/**
 * @file    svc_posture.c
 * @brief   坐姿检测与校准服务实现。
 */
#include <stddef.h>
#include "svc_posture.h"

static int16_t s_base_x10 = 0;      /* 基准角(0.1°) */
static int16_t s_filt_x10 = 0;      /* 低通后角度 */
static uint8_t s_have_filt = 0u;
static uint8_t s_alarm_active = 0u;
static uint32_t s_hold_start_ms = 0u;
static uint32_t s_last_alarm_ms = 0u;
static calib_state_t s_calib = CALIB_IDLE;

void SvcPosture_Init(int16_t base_angle_x10)
{
  s_base_x10     = base_angle_x10;
  s_filt_x10     = base_angle_x10;
  s_have_filt    = 0u;
  s_alarm_active = 0u;
  s_hold_start_ms= 0u;
  s_last_alarm_ms= 0u;
  s_calib        = CALIB_IDLE;
}

int16_t SvcPosture_Angle(void)          { return s_filt_x10; }
uint8_t SvcPosture_AlarmActive(void)    { return s_alarm_active; }
calib_state_t SvcPosture_CalibState(void){ return s_calib; }
int16_t SvcPosture_GetBaseAngle(void)   { return s_base_x10; }

void SvcPosture_StartCalibration(void)
{
  s_calib = CALIB_WAITING;
}

void SvcPosture_HandleCalibResult(int16_t base_angle_x10)
{
  s_base_x10 = base_angle_x10;
  s_filt_x10 = base_angle_x10;
  s_calib    = CALIB_IDLE;
}

/**
 * AI 帧驱动。告警条件（需求 §五）:
 *  学习模式 & 有人 & 头部可见 & 角度 > base+阈值 & 持续>=1.5s, 且受冷却限制。
 */
uint8_t SvcPosture_OnAiFrame(const ai_frame_t *f, sys_mode_t mode,
                             uint8_t threshold_deg, int16_t base_angle_x10,
                             uint32_t now_ms)
{
  uint8_t trigger = 0u;

  if (f == NULL) return 0u;

  /* 一阶低通 */
  if (s_have_filt)
  {
    s_filt_x10 = (int16_t)(s_filt_x10 +
                 ((int32_t)f->pitch_angle - s_filt_x10) / (int32_t)(1 << POSTURE_FILTER_SHIFT));
  }
  else
  {
    s_filt_x10 = f->pitch_angle;
    s_have_filt = 1u;
  }

  /* 条件满足? keypoint_valid 为 0~3(鼻/左肩/右肩), 对齐 UART_PROTOCOL.md §3.1 */
  uint8_t posture_en = (mode == SYS_MODE_STUDY);
  int32_t thr = (int32_t)base_angle_x10 + (int32_t)threshold_deg * 10;
  uint8_t in_bad = (posture_en && f->has_human && f->head_visible &&
                    (f->keypoint_valid >= 3u) &&
                    ((int32_t)s_filt_x10 > thr)) ? 1u : 0u;

  if (in_bad)
  {
    if (s_hold_start_ms == 0u)
    {
      s_hold_start_ms = now_ms;
    }
    /* 持续 >=1.5s 且距上次告警已过冷却 */
    if ((now_ms - s_hold_start_ms) >= POSTURE_ALARM_HOLD_MS)
    {
      if ((s_last_alarm_ms == 0u) ||
          ((now_ms - s_last_alarm_ms) >= POSTURE_ALARM_COOLDOWN_MS))
      {
        trigger = 1u;
        s_last_alarm_ms = now_ms;
        s_hold_start_ms = now_ms;    /* 重置, 等待下一次持续 */
      }
    }
    s_alarm_active = 1u;
  }
  else
  {
    s_hold_start_ms = 0u;
    s_alarm_active = 0u;             /* 头部丢失/有人消失 -> 停止当前告警 */
  }

  return trigger;
}