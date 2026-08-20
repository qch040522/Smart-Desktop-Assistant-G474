/**
 * @file    app_tasks.h
 * @brief   App 任务实现头：初始化 UI 子系统与各任务运行体（供 main.c 调用）。
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_rtos.h"
#include "bsp.h"

/* 任务看门狗槽位分配 */
#define WDG_SLOT_UART     0u
#define WDG_SLOT_SENSOR   1u
#define WDG_SLOT_STATE    2u
#define WDG_SLOT_POSTURE  3u
#define WDG_SLOT_ENV      4u
#define WDG_SLOT_TIMER    5u
#define WDG_SLOT_UI       6u
#define WDG_SLOT_FLASH    7u

/* UI(界面) 子系统初始化（注册 TJC 事件回调, OLED 首页） */
void App_UiInit(void);

/* 界面系统对外命令投递（TJC 事件 -> qCmd）由 task 实现 */
void App_TaskDefault(void *arg);
void App_TaskUart(void *arg);
void App_TaskSensor(void *arg);
void App_TaskState(void *arg);
void App_TaskPosture(void *arg);
void App_TaskEnv(void *arg);
void App_TaskTimer(void *arg);
void App_TaskUi(void *arg);
void App_TaskFlash(void *arg);
void App_TaskInit(void *arg);   /* 调度器启动后初始化 I2C 外设 */

/* 每周期 AI 帧存在标志（state/posture 共享, 供状态机判定） */
extern volatile uint8_t g_frame_any;
extern volatile uint8_t g_frame_valid;

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */