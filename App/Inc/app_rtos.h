/**
 * @file    app_rtos.h
 * @brief   RTOS 对象与所有任务入口的统一声明。
 *
 *  CubeMX 已在 main.c 中创建了 9 个任务并调用 StartXxxTask，
 *  本文件提供各任务真正的运行函数与应用初始化接口。
 */
#ifndef APP_RTOS_H
#define APP_RTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os2.h"
#include "app_types.h"

/* ==================== RTOS 对象（所有任务共享） ==================== */
extern osMessageQueueId_t qCmd;     /* 统一命令: 多生产者 -> state 任务 */
extern osMessageQueueId_t qAI;      /* AI 状态帧: uart -> posture/state */
extern osMessageQueueId_t qUI;      /* UI 事件: 各服务 -> ui 任务 */
extern osMessageQueueId_t qFlash;   /* Flash 快照请求 */

extern osMutexId_t mtxSensor;       /* 保护共享传感器数据 */
extern osMutexId_t mtxConfig;      /* 保护配置对象 */
extern osMutexId_t mtxI2c;         /* 保护 I2C2 总线(OLED/BH1750/MPU6050 共用) */

/* 传感器共享数据（sensor 任务产, env/timing/ui 消费, 读前后须加 mtxSensor） */
extern volatile sensor_data_t g_sensor;
/* 设备状态（汇总, uart/state 更新, ui/uart 读） */
extern volatile dev_status_t g_status;

/* 全局配置（App 持有, 同步 Flash） */
extern app_config_t g_cfg;

/* ==================== 应用初始化 ==================== */
void App_Init(void);                /* 创建队列/互斥, 各任务初始化 */

/* ==================== 各任务运行体（由 main.c 的 StartXxx 调用） ==================== */
void App_TaskDefault(void *arg);    /* 看门狗监控 */
void App_TaskUart(void *arg);       /* USART3 <-> ESP32 协议 */
void App_TaskSensor(void *arg);     /* DHT11/BH1750/MPU6050 */
void App_TaskState(void *arg);      /* 状态机/无人断链/命令路由 */
void App_TaskPosture(void *arg);    /* 坐姿判定/校准会话 */
void App_TaskEnv(void *arg);        /* 风扇/台灯 环境控制 */
void App_TaskTimer(void *arg);      /* 时长/番茄钟/闹钟 */
void App_TaskUi(void *arg);         /* OLED + TJC 串口屏 */
void App_TaskFlash(void *arg);      /* 快照/配置落盘 */

/* ==================== 看门狗心跳接口 ==================== */
#define WDG_SLOTS          8u      /* 任务看门狗槽数 */
#define WDG_TIMEOUT_MS     5000u
void     Wdg_Init(void);
void     Wdg_Heartbeat(uint8_t slot);  /* 任务周期调用 */
uint32_t Wdg_GetLastBeat(uint8_t slot);
uint8_t  Wdg_PeekTimeout(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RTOS_H */