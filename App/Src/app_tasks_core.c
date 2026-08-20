/**
 * @file    app_tasks_core.c
 * @brief   App 任务层（一）: default/uart/sensor/env/timer 任务实现。
 *          （state/posture/ui 见 app_tasks_flow.c）
 */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "app_tasks.h"
#include "app_rtos.h"
#include "svc_state.h"
#include "svc_posture.h"
#include "svc_env.h"
#include "svc_timer.h"
#include "svc_link.h"
#include "svc_config.h"
#include "bsp_oled.h"
#include "bsp_fan.h"
#include "bsp_ws2812.h"
#include "bsp_buzzer.h"
#include "bsp_led.h"
#include "bsp_dht11.h"
#include "bsp_bh1750.h"
#include "bsp_mpu6050.h"
#include "bsp_rtc.h"
#include "bsp_tjc.h"
#include "bsp_flash.h"
#include "bsp_uart_link.h"

/* ---- 传感器任务挪动检测基准 ---- */
static int32_t s_grav_base = 0;
static uint32_t s_motion_invalid_ms = 0;

/* ================================================================ */
/*  defaultTask: 看门狗监护 + 喂 IWDG                               */
/* ================================================================ */
void App_TaskDefault(void *arg)
{
  (void)arg;
  uint8_t i;
  for (;;)
  {
    for (i = 0u; i < WDG_SLOTS; i++)
    {
      uint32_t last = Wdg_GetLastBeat(i);
      if ((last != 0u) && ((HAL_GetTick() - last) > WDG_TIMEOUT_MS))
      {
        BspBuzzer_BeepOn(3300u);          /* 看门狗超时报警 */
      }
    }
    BSP_IwdgFeed();
    osDelay(1000u);
  }
}

/* ================================================================ */
/*  uartTask: 周期向 ESP32 上报设备状态 0x12                         */
/* ================================================================ */
void App_TaskUart(void *arg)
{
  (void)arg;
  uint32_t last_report = 0u;

  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_UART);
    SvcLink_Service();                     /* 发送挂起的 ACK 回执 */
    if ((BSP_GetTick() - last_report) >= 3000u)
    {
      dev_status_t st;
      last_report = BSP_GetTick();
      osMutexAcquire(mtxSensor, 0u);
      st = g_status;                 /* 拷贝共享状态 */
      osMutexRelease(mtxSensor);
      SvcLink_SendStatus(&st);
    }
    osDelay(250u);
  }
}

/* ================================================================ */
/*  sensorTask: DHT11 / BH1750 / MPU6050 + 挪动检测                 */
/* ================================================================ */
void App_TaskSensor(void *arg)
{
  (void)arg;
  dht11_data_t dh;
  uint16_t lux = 0u;
  int16_t ax, ay, az;
  uint32_t last_imu = 0u;

  /* 后台初始化 I2C 器件(调度器已启动): BH1750/MPU6050 + 总线扫描, 首次执行一次 */
  (void)BspBh1750_Init();
  (void)BspMpu6050_Init();
  BspI2c_ScanBus();

  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_SENSOR);

    dh.ok = 0u;
    if (BspDht11_Read(&dh) == 0)
    {
      /* 合理性过滤: 温度 0~60°C(0.1°C: 0~600), 湿度 0~100%。
       * DHT11 偶发"校验通过但数值离谱"的坏帧, 丢弃并保留上次有效值。 */
      if ((dh.temp_x10 >= 0) && (dh.temp_x10 <= 600) && (dh.humi_pct <= 100u))
      {
        osMutexAcquire(mtxSensor, 0u);
        g_sensor.temp_x10 = dh.temp_x10;
        g_sensor.humi_pct = dh.humi_pct;
        osMutexRelease(mtxSensor);
      }
    }

    if (BspBh1750_ReadLux(&lux) == 0)
    {
      osMutexAcquire(mtxSensor, 0u);
      g_sensor.lux = lux;
      osMutexRelease(mtxSensor);
    }

    if (((HAL_GetTick() - last_imu) >= 500u) &&
        (BspMpu6050_ReadAccel(&ax, &ay, &az) == 0))
    {
      last_imu = HAL_GetTick();
      osMutexAcquire(mtxSensor, 0u);
      g_sensor.acc_raw[0] = ax;
      g_sensor.acc_raw[1] = ay;
      g_sensor.acc_raw[2] = az;
      g_sensor.tick_ms = HAL_GetTick();
      osMutexRelease(mtxSensor);

      int32_t mag = (int32_t)ax * (int32_t)ax + (int32_t)ay * (int32_t)ay +
                    (int32_t)az * (int32_t)az;
      if (s_grav_base == 0) s_grav_base = mag;

      int32_t diff = s_grav_base - mag; if (diff < 0) diff = -diff;
      if (diff > MOTION_DEADBAND_RAW)
      {
        if (s_motion_invalid_ms == 0u)
        {
          s_motion_invalid_ms = HAL_GetTick();
        }
        else if ((HAL_GetTick() - s_motion_invalid_ms) >= MOTION_CONFIRM_MS)
        {
          /* 持续挪动: 提示"需要重新校准"（OLED/触摸屏/MQTT, 需求 §五） */
          if (g_status.alert_flag == 0u)
          {
            g_status.alert_flag = 1u;
            ui_msg_t m = { UE_RECALIB_NEEDED, 0, 0, 0 };
            osMessageQueuePut(qUI, &m, 0u, 0u);
          }
        }
      }
      else
      {
        s_motion_invalid_ms = 0u;
      }
    }

    osDelay(1500u);
  }
}

/* ================================================================ */
/*  envTask: 风扇/台灯 环境控制                                      */
/* ================================================================ */
void App_TaskEnv(void *arg)
{
  (void)arg;
  for (;;)
  {
    sensor_data_t sen;
    Wdg_Heartbeat(WDG_SLOT_ENV);
    osMutexAcquire(mtxSensor, 0u);
    sen = g_sensor;                  /* 拷贝共享传感器数据 */
    osMutexRelease(mtxSensor);
    SvcEnv_Update(&sen, SvcState_Mode(), &g_cfg);
    g_status.fan_level       = SvcEnv_FanLevel();
    g_status.lamp_brightness = (uint16_t)SvcEnv_LampBrightness();
    g_status.ctrl_mode       = SvcEnv_CtrlMode();
    osDelay(1000u);
  }
}

/* ================================================================ */
/*  timerTask: 时长/番茄钟/闹钟                                      */
/* ================================================================ */
void App_TaskTimer(void *arg)
{
  (void)arg;
  for (;;)
  {
    Wdg_Heartbeat(WDG_SLOT_TIMER);
    SvcTimer_Tick(SvcState_Mode(), HAL_GetTick());
    g_status.study_cur_sec   = SvcTimer_CurSec();
    g_status.study_today_sec = SvcTimer_TodaySec();
    g_status.study_total_sec = SvcTimer_TotalSec();
    g_status.pomo_remain_sec = SvcTimer_PomoRemainSec();
    g_status.pomo_state      = SvcTimer_PomoState();
    g_status.pomofen         = g_cfg.pomodo_en;
    g_status.alarm_en        = SvcTimer_AlarmEnabled();
    g_status.alarm_hour      = SvcTimer_AlarmHour();
    g_status.alarm_min       = SvcTimer_AlarmMin();

    if (SvcTimer_AlarmRinging())
    {
      ui_msg_t m = { UE_ALARM_RING, 0, 0, 0 };
      osMessageQueuePut(qUI, &m, 0u, 0u);
    }
    osDelay(1000u);
  }
}

/* ================================================================ */
/*  flashTask: 写配置 / 时长快照                                     */
/* ================================================================ */
void App_TaskFlash(void *arg)
{
  (void)arg;
  uint8_t req;
  for (;;)
  {
    if (osMessageQueueGet(qFlash, &req, NULL, 0u) == osOK)
    {
      if (req == 0u)
      {
        (void)SvcConfig_Save(&g_cfg);
      }
      else
      {
        app_timing_snapshot_t snap;
        SvcTimer_BuildSnapshot(&snap);
        uint8_t tmp[sizeof(app_timing_snapshot_t)];
        BspFlash_ErasePage(FLASH_TIM_ADDR);
        memset(tmp, 0xFFu, sizeof(tmp));
        memcpy(tmp, &snap, sizeof(app_timing_snapshot_t));
        (void)BspFlash_Program(FLASH_TIM_ADDR, tmp, sizeof(app_timing_snapshot_t));
      }
    }
    osDelay(100u);
  }
}