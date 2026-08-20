/**
 * @file    app_types.h
 * @brief   智能学习桌面助手 - 跨层共享数据结构定义
 */
#ifndef APP_TYPES_H
#define APP_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_conf.h"

/* ============================ 枚举类型 ============================ */

/** 业务模式（需求 §四 状态机） */
typedef enum {
  SYS_MODE_STUDY = 0,   /* 学习模式 */
  SYS_MODE_LEISURE,     /* 休闲模式 */
  SYS_MODE_SLEEP,       /* 业务休眠 */
  SYS_MODE_NUM
} sys_mode_t;

/** 外设控制模式（需求 §六 手动优先/手动持久） */
typedef enum {
  CTRL_MODE_AUTO = 0,   /* 自动 */
  CTRL_MODE_MANUAL      /* 手动(持久,直到切回自动) */
} ctrl_mode_t;

/** 无人/断链综合状态 */
typedef enum {
  OCCUPY_UNKNOWN = 0,   /* 初始或帧缺失 */
  OCCUPY_HUMAN,         /* 有人 */
  OCCUPY_NOBODY,        /* 无人(连续>=3周期) */
} occupy_state_t;

/** ESP32 UART 链路状态 */
typedef enum {
  LINK_OK = 0,
  LINK_DOWN             /* 连续>=10周期无 0x01 帧 */
} link_state_t;

/* ============================ 数据结构 ============================ */

/** AI 状态帧（ESP32 0x01, 需求 §10.1） */
typedef struct {
  uint8_t   has_human;      /* 有人(上半身关键点达标) */
  uint8_t   head_visible;   /* 头部关键点可见 */
  int16_t   pitch_angle;    /* 头-肩下沉角, 0.1°, 低头为正签名 */
  uint8_t   keypoint_valid; /* 上半身有效关键点计数 */
  uint32_t  tick_ms;        /* 收帧时刻(系统运行毫秒) */
} ai_frame_t;

/** 传感器汇聚（需求 §十一 传感器任务） */
typedef struct {
  int16_t   temp_x10;       /* 温度, 0.1°C */
  uint16_t  humi_pct;       /* 相对湿度 %RH */
  uint16_t  lux;            /* 光照 lux */
  int16_t   acc_raw[3];     /* MPU6050 三轴原始值 */
  uint32_t  tick_ms;
} sensor_data_t;

/** 命令来源 */
typedef enum {
  CMD_SRC_UNKNOWN = 0,
  CMD_SRC_TJC,      /* 触摸屏 */
  CMD_SRC_ESP32,    /* MQTT/Web (ESP32 0x02) */
  CMD_SRC_SYS       /* 系统内部 */
} cmd_src_t;

/** 统一命令 id（触摸屏 / MQTT 命令在此归一） */
typedef enum {
  CMD_NONE = 0,

  CMD_SET_SYS_MODE,        /* p1=sys_mode_t */
  CMD_SET_CTRL_MODE,       /* p1=ctrl_mode_t */

  CMD_FAN_LEVEL,           /* p1=0..FAN_LEVELS 手动档; -1=自动 */
  CMD_LAMP_BRIGHT,         /* p1=0..255 手动亮度; -1=自动 */

  CMD_START_CALIB,         /* 坐姿校准 */
  CMD_SET_THRESHOLD_LVL,   /* p1=档序号(0~POSTURE_TH_LVL_COUNT-1) */

  CMD_POMO_ENABLE,         /* p1=0/1 */
  CMD_POMO_SET_MIN,        /* p1=分钟(整数) */
  CMD_POMO_START,
  CMD_POMO_PAUSE,
  CMD_POMO_RESET,

  CMD_ALARM_SET,           /* p1=时, p2=分 */
  CMD_ALARM_ENABLE,        /* p1=0/1 */
  CMD_ALARM_RESET,

  CMD_RESET_TOTAL_STUDY,   /* 总时长归零 */

  CMD_WAKEUP,              /* 唤醒(触摸屏/ESP32 任意操作) */

  CMD_NUM
} cmd_id_t;

/** 统一命令消息 */
typedef struct {
  uint8_t   id;       /* cmd_id_t */
  cmd_src_t src;
  int32_t   p1, p2, p3;
} app_cmd_t;

/** UI 事件（各状态->界面任务） */
typedef enum {
  UE_NONE = 0,
  UE_MODE_CHANGED,       /* a=sys_mode_t */
  UE_CTRL_CHANGED,       /* a=ctrl_mode_t */
  UE_SENSOR_UPD,         /* 传感器新数据 */
  UE_AI_UPD,             /* AI 帧(用于校准进度显示) */
  UE_POSTURE_ALARM,      /* 坐姿告警发生个 */
  UE_RECALIB_NEEDED,     /* 需要重新校准(MPU 挪动) */
  UE_CALIB_START,        /* 校开始 */
  UE_CALIB_DONE,         /* 校准完成 a=基准角(0.1°) */
  UE_POMO_TICK,          /* 番茄钟秒变化 */
  UE_POMO_DONE,          /* 番茄钟结束 */
  UE_ALARM_RING,         /* 闹钟响 */
  UE_ALARM_RELEASE,      /* 闹钟停止 */
  UE_LINK_DOWN,          /* ESP32 断链 */
  UE_LINK_UP,            /* 链路恢复 */
  UE_OCCUPY_CHG,         /* 有人/无人变化 a=occupy_state */
} ui_evt_t;

typedef struct {
  uint8_t   id;      /* ui_evt_id_t */
  int16_t   a, b, c;
} ui_msg_t;

/** 配置（低频, 快照到 Flash，需求 §七/§9） */
typedef struct {
  uint32_t magic;             /* FLASH_CFG_MAGIC */
  uint32_t version;
  uint8_t  sys_mode;          /* 上次模式(回调) */
  uint8_t  ctrl_mode;         /* 自动/手动持久 */
  int16_t  angle_base;        /* 基准角(0.1°) */
  uint8_t  angle_threshold;   /* 当前告警阈值(°) */
  uint8_t  alarm_hour;
  uint8_t  alarm_min;
  uint8_t  alarm_en;
  uint16_t pomodoro_min;
  uint8_t  pomodo_en;
  uint8_t  fan_level;         /* 手动档记忆 */
  uint8_t  lamp_brightness;   /* 手动亮度记忆 */
} app_config_t;

/** 时长快照（退出学习模式 / 跨天 时写 Flash 102） */
typedef struct {
  uint32_t magic;              /* FLASH_TIM_MAGIC */
  uint32_t version;
  uint32_t study_total_sec;    /* 总学习时长(s) */
  uint32_t study_today_sec;    /* 今日学习时长(s) */
  uint32_t last_date_ymd;      /* 最后保存时 RTC 日期 yyyymmdd */
} app_timing_snapshot_t;

/** 设备当前运行状态（汇总, 供 0x12 上报 / UI 显示） */
typedef struct {
  uint8_t  sys_mode;
  uint8_t  ctrl_mode;
  uint8_t  fan_level;          /* 0~N */
  uint16_t lamp_brightness;    /* 0~255 */
  uint32_t study_cur_sec;
  uint32_t study_today_sec;
  uint32_t study_total_sec;
  uint8_t  pomofen;            /* 番茄钟使能 */
  uint8_t  pomo_state;         /* 0=idle 1=run 2=pause */
  uint32_t pomo_remain_sec;
  uint8_t  alarm_en;
  uint8_t  alarm_hour, alarm_min;
  uint8_t  occupy;             /* occupy_state_t */
  uint8_t  link_state;         /* link_state_t */
  uint8_t  alert_flag;         /* 需要重新校准标志 */
} dev_status_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_TYPES_H */