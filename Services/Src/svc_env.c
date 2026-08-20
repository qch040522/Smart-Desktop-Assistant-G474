/**
 * @file    svc_env.c
 * @brief   环境控制实现。
 *
 *  风扇自动: 温度越高档位越大 (0~100%). 以温度为主、湿度微调。
 *  台灯自动: 光照越低蓝光越亮 (0~255)。
 *  休眠: 两者关闭。
 */
#include <stddef.h>
#include "svc_env.h"
#include "bsp_fan.h"
#include "bsp_ws2812.h"

static ctrl_mode_t s_ctrl_mode;
static uint8_t     s_fan_level;      /* 手动档 0..FAN_LEVELS-1 */
static uint16_t    s_lamp_bright;

/* 上次实际下发值（用于变化检测, 避免周期性重复发送） */
static uint8_t  s_last_fan  = 0xFFu;
static uint8_t  s_last_lamp = 0xFFu;

static uint8_t map_temp_to_level(int16_t temp_x10)
{
  int t = (int)temp_x10;              /* 0.1°C */
  if (t <= 230)  return 0u;           /* <=23°C 停 */
  if (t <= 250)  return 1u;
  if (t <= 270)  return 2u;
  if (t <= 290)  return 3u;
  if (t <= 310)  return 4u;
  if (t <= 330)  return 5u;
  if (t <= 350)  return 6u;
  return (uint8_t)(FAN_LEVELS - 1u);
}

static uint16_t lux_to_bright(uint16_t lux)
{
  if (lux <= LUX_DARK)    return LAMP_PWM_MAX;
  if (lux >= LUX_BRIGHT)  return 0u;
  /* LUX_DARK~LUX_BRIGHT lux 线性 */
  return (uint16_t)(LAMP_PWM_MAX -
                    ((uint32_t)(lux - LUX_DARK) * LAMP_PWM_MAX) /
                    (LUX_BRIGHT - LUX_DARK));
}

void SvcEnv_Init(app_config_t *cfg)
{
  s_ctrl_mode   = (ctrl_mode_t)cfg->ctrl_mode;
  s_fan_level   = (uint8_t)(cfg->fan_level >= FAN_LEVELS ? (FAN_LEVELS - 1u) : cfg->fan_level);
  s_lamp_bright = (uint16_t)(cfg->lamp_brightness > LAMP_PWM_MAX ? LAMP_PWM_MAX : cfg->lamp_brightness);
}

static void apply_fan(uint8_t pct)
{
  if (pct != s_last_fan)
  {
    BspFan_SetSpeed(pct);
    s_last_fan = pct;
  }
}

static void apply_lamp(uint8_t bright)
{
  if (bright != s_last_lamp)
  {
    BspWs2812_SetBlue(bright);
    s_last_lamp = bright;
  }
}

void SvcEnv_Update(const sensor_data_t *sen, sys_mode_t mode, app_config_t *cfg)
{
  (void)cfg;

  if (mode == SYS_MODE_SLEEP)
  {
    apply_fan(0u);
    apply_lamp(0u);
    return;
  }

  if (s_ctrl_mode == CTRL_MODE_MANUAL)
  {
    apply_fan((uint8_t)((uint32_t)s_fan_level * 100u / (FAN_LEVELS - 1u)));
    apply_lamp((uint8_t)s_lamp_bright);
  }
  else if (sen != NULL)
  {
    uint8_t lvl = map_temp_to_level(sen->temp_x10);
    apply_fan((uint8_t)((uint32_t)lvl * 100u / (FAN_LEVELS - 1u)));
    apply_lamp((uint8_t)lux_to_bright(sen->lux));
  }
}

uint8_t SvcEnv_FanLevel(void)      { return s_fan_level; }
uint16_t SvcEnv_LampBrightness(void){ return s_lamp_bright; }
ctrl_mode_t SvcEnv_CtrlMode(void)  { return s_ctrl_mode; }

void SvcEnv_SetFanManual(uint8_t level)
{
  if (level < FAN_LEVELS)
  {
    s_fan_level = level;
    s_ctrl_mode = CTRL_MODE_MANUAL;
  }
}

void SvcEnv_SetLampManual(uint8_t bright)
{
  if (bright <= LAMP_PWM_MAX)
  {
    s_lamp_bright = bright;
    s_ctrl_mode   = CTRL_MODE_MANUAL;
  }
}

void SvcEnv_SetCtrlMode(ctrl_mode_t m)
{
  if ((m == CTRL_MODE_AUTO) || (m == CTRL_MODE_MANUAL))
  {
    s_ctrl_mode = m;
  }
}