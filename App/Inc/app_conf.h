/**
 * @file    app_conf.h
 * @brief   智能学习桌面助手 - STM32G474 应用层全局配置
 *
 * 说明：
 *  本文件为"App / Services / BSP"三层共享的系统常量定义，
 *  对应《需求规格说明书 v5.1》与《引脚规划 v3.3》。
 *  CubeMX 工程目录不变（Core / Drivers / Middlewares）。
 */
#ifndef APP_CONF_H
#define APP_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 基础常量 ============================ */
#define APP_TICK_MS                 1000u   /* 各任务基础心跳周期(ms) */
#define SYSTEM_CORE_HZ              170000000uL

/* ============================ 业务参数 ============================ */
/* --- 无人 / 断链判定 (需求 §四 / §11.1) --- */
#define AI_REPORT_PERIOD_MS         1000u   /* ESP32 0x01 上报周期目标(默认1s) */
#define AI_REPORT_PERIOD_SLEEP_MS   2500u   /* 业务休眠期上报周期 */
/* 无人判定: 距上次有效帧超过该时间判无人。
 * 注意: ESP32 AI 推理耗时约 4~5s/帧(非 1s), 若按帧数(如3帧)判定会在帧间隙误判无人
 * 导致风扇/台灯周期性开关, 故改为时间窗口。 */
#define UNHUMAN_TIMEOUT_MS          15000u  /* 距上次有效帧 15s 无更新 -> 无人 */
#define LINK_DOWN_FRAME_TH            10u   /* 连续>=10个周期无任何0x01帧 -> 断链 */

/* --- 坐姿告警 (需求 §五) --- */
#define POSTURE_ALARM_HOLD_MS        1500u  /* 告警需连续保持 1.5s */
#define POSTURE_ALARM_COOLDOWN_MS    60000u /* 告警冷却时间 60s */
#define POSTURE_ALARM_FLASH_TIMES    5u     /* 一次告警 LED 闪烁次数 */
#define POSTURE_FILTER_SHIFT         3u     /* 角度一阶低通: alpha=1/8 */
/* 告警阈值档位(0.1° 单位) */
#define POSTURE_TH_LVL_COUNT         4u
#define POSTURE_TH_LVL_DEFAULT       2u      /* 默认第2档 */
#define POSTURE_BASE_ANGLE_DEFAULT   0       /* 默认基准角 0 */

/* --- MPU6050 挪动 / 基准失效检测 (需求 §五 / §12.8) --- */
#define MOTION_DEADBAND_XY           200     /* X/Y轴死区(线性域LSB, 约0.012g, 水平方向更灵敏) */
#define MOTION_DEADBAND_Z            400     /* Z轴死区(线性域LSB, 约0.024g) */
#define MOTION_CONFIRM_MS            200u    /* 偏差持续确认窗口 0.2s(两次采样即触发) */

/* ============================ 设备参数 ============================ */
#define FAN_PWM_PERIOD               8500u   /* TIM4 计数周期, 0% ~ 100% = 0~8500 */
#define FAN_LEVELS                   8u      /* 风扇手动档 (0=关) */

#define LAMP_PWM_MAX                 255u    /* WS2812 白光亮度 0~255 */
#define LAMP_ADJUST_STEP             16u     /* 自动亮度步进，防闪烁 */

/* 台灯自动亮度策略 */
#define LUX_DARK                    80u     /* <= 此值亮度 255 */
#define LUX_BRIGHT                  400u    /* >= 此值亮度 0 */

/* =========================== 计时参数 =========================== */
#define POMO_DEFAULT_MINUTES        0u   /* 番茄钟默认时长: 0=关闭 */
#define POMO_MAX_MINUTES            1000u  /* 番茄钟时长上限(1000分钟), 超限按0处理(关闭) */
#define POMO_RING_MS                10000u  /* 番茄钟到点蜂鸣提示 10s(自动停) */
#define ALARM_RING_MS               30000u  /* 闹钟到场响铃最长 30s */

/* =========================== 存储参数 =========================== */
#define FLASH_USER_SECTOR_ADDR      0x0807F800u /* 配置扇区(末扇区, 2KB/扇) */
#define FLASH_TIM_ADDR              0x0807F000u /* 时长快照扇区              */
#define FLASH_CFG_MAGIC              0x4B4D31u  /* 配置签名 "KM1" */
#define FLASH_TIM_MAGIC              0x4B4D32u  /* 时长签名 "KM2" */

/* ========================== 队列容量 ========================== */
#define QUEUE_CMD_DEPTH      16u
#define QUEUE_AI_DEPTH       8u
#define QUEUE_UI_DEPTH       16u
#define QUEUE_FLASH_DEPTH    4u

#ifdef __cplusplus
}
#endif

#endif /* APP_CONF_H */