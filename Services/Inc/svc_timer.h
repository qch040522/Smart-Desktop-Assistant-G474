/**
 * @file    svc_timer.h
 * @brief   计时服务：本次/今日/总学习时长、番茄钟、闹钟（RTC 跨天清零）。
 */
#ifndef SVC_TIMER_H
#define SVC_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

/** 初始化（从 Flash 快照恢复今日/总时长） */
void SvcTimer_Init(const app_timing_snapshot_t *snap);

/** 每秒周期处理（驱动计时与闹钟判断） */
void SvcTimer_Tick(sys_mode_t mode, uint32_t now_ms);

/* ---- 学习时长 ---- */
uint32_t SvcTimer_CurSec(void);
uint32_t SvcTimer_TodaySec(void);
uint32_t SvcTimer_TotalSec(void);
void     SvcTimer_ResetTotal(void);

/* ---- 番茄钟（仅手动控制） ---- */
void SvcTimer_PomoSetMin(uint16_t min);
void SvcTimer_PomoSetSec(uint32_t sec);   /* 精确到秒设置 */
void SvcTimer_PomoEnable(uint8_t en);
void SvcTimer_PomoStart(void);
void SvcTimer_PomoPause(void);
void SvcTimer_PomoReset(void);
uint8_t  SvcTimer_PomoState(void);      /* 0=idle,1=run,2=pause */
uint32_t SvcTimer_PomoRemainSec(void);

/* ---- 闹钟 ---- */
void SvcTimer_AlarmSet(uint8_t h, uint8_t m);
void SvcTimer_AlarmEnable(uint8_t en);
void SvcTimer_AlarmAck(void);           /* 手动停响 */
uint8_t SvcTimer_AlarmRinging(void);
uint8_t SvcTimer_AlarmEnabled(void);    /* 闹钟使能状态(供 0x12/UI) */
uint8_t SvcTimer_AlarmHour(void);       /* 闹钟时(供 0x12/UI) */
uint8_t SvcTimer_AlarmMin(void);        /* 闹钟分(供 0x12/UI) */

/* 生成当前时长快照(供 flash 任务落盘) */
void SvcTimer_BuildSnapshot(app_timing_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* SVC_TIMER_H */