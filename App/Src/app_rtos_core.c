/**
 * @file    app_rtos_core.c
 * @brief   RTOS 对象与系统初始化：创建队列/互斥, 加载配置, 初始化 BSP 与各服务。
 */
#include <string.h>
#include "cmsis_os2.h"
#include "app_rtos.h"
#include "app_tasks.h"
#include "svc_config.h"
#include "svc_state.h"
#include "svc_posture.h"
#include "svc_env.h"
#include "svc_timer.h"
#include "svc_link.h"
#include "bsp_oled.h"
#include "bsp_fan.h"
#include "bsp_ws2812.h"
#include "bsp_buzzer.h"
#include "bsp_led.h"
#include "bsp_dht11.h"
#include "bsp_bh1750.h"
#include "bsp_mpu6050.h"
#include "bsp_rtc.h"

/* ---- RTOS 对象定义 ---- */
osMessageQueueId_t qCmd;
osMessageQueueId_t qAI;
osMessageQueueId_t qUI;
osMessageQueueId_t qFlash;
osMutexId_t        mtxSensor;
osMutexId_t        mtxConfig;
osMutexId_t        mtxI2c;

volatile sensor_data_t g_sensor;
volatile dev_status_t  g_status;

/* ---- 全局配置（App 持有, 同步 Flash） ---- */
app_config_t         g_cfg;

/* ==================== 任务看门狗 ==================== */
static volatile uint8_t  s_wdg_beat[WDG_SLOTS];
static volatile uint32_t s_wdg_last[WDG_SLOTS];

void Wdg_Init(void)
{
  uint8_t i;
  for (i = 0u; i < WDG_SLOTS; i++)
  {
    s_wdg_beat[i] = 0u;
    s_wdg_last[i] = HAL_GetTick();
  }
}

void Wdg_Heartbeat(uint8_t slot)
{
  if (slot >= WDG_SLOTS) return;
  s_wdg_beat[slot] = 1u;
  s_wdg_last[slot] = HAL_GetTick();
}

uint32_t Wdg_GetLastBeat(uint8_t slot)
{
  if (slot >= WDG_SLOTS) return 0u;
  return s_wdg_last[slot];
}

uint8_t Wdg_PeekTimeout(void)
{
  return 1u;   /* 默认安全; 实际监控由 default 任务轮询 */
}

/* ==================== 应用初始化 ==================== */
void App_Init(void)
{
  /* 1. 创建消息队列 */
  qCmd   = osMessageQueueNew(QUEUE_CMD_DEPTH,    sizeof(app_cmd_t), NULL);
  qAI    = osMessageQueueNew(QUEUE_AI_DEPTH,     sizeof(ai_frame_t), NULL);
  qUI    = osMessageQueueNew(QUEUE_UI_DEPTH,     sizeof(ui_msg_t), NULL);
  qFlash = osMessageQueueNew(QUEUE_FLASH_DEPTH,  sizeof(uint8_t), NULL);

  /* 2. 创建互斥 */
  mtxSensor = osMutexNew(NULL);
  mtxConfig = osMutexNew(NULL);
  mtxI2c    = osMutexNew(NULL);   /* I2C2 总线互斥 */

  /* 3. 任务看门狗 */
  Wdg_Init();

  /* 4. 加载配置 / 初始化服务 */
  (void)SvcConfig_Load(&g_cfg);
  SvcState_Init(&g_cfg);
  SvcPosture_Init(g_cfg.angle_base);
  SvcEnv_Init(&g_cfg);
  SvcTimer_Init(NULL);

  /* 5. 初始化 BSP 外设 */
  BspOled_Init();
  BspFan_Init();
  BspWs2812_Init();
  BspBuzzer_Init();
  BspLed_Init();
  BspDht11_Init();
  (void)BspBh1750_Init();
  (void)BspMpu6050_Init();
  (void)BspRtc_GetYmd();

  /* 6. 通讯服务 */
  SvcLink_Init();
  App_UiInit();            /* 负责 TJC/OLED 等界面任务初始化 */

  /* 7. I2C 总线扫描(调试): 判断 OLED/BH1750/MPU6050 是否都在总线上 */
  BspI2c_ScanBus();

  /* 7. 传感器初始 */
  g_sensor.temp_x10 = 0;
  g_sensor.humi_pct = 0;
  g_sensor.lux      = 0;
  g_sensor.acc_raw[0] = g_sensor.acc_raw[1] = g_sensor.acc_raw[2] = 0;
  g_sensor.tick_ms  = 0;
}