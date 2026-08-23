/**
 * @file    svc_env.c
 * @brief   环境控制实现。
 *
 *  手动控制页: 风扇/台灯各按"自动挡/手动挡"独立控制(不看摄像头)。
 *     风扇自动: 温度越高档位越大 (0~100%); 台灯自动: 光照<阈值全亮(开关式)。
 *  自动监测页: 需"有人 + 达到阈值"才开风扇/台灯(结合摄像头判定)。
 *  休眠: 两者关闭。
 */
#include <stddef.h>
#include "bsp.h"
#include "svc_env.h"
#include "bsp_fan.h"
#include "bsp_ws2812.h"

static ctrl_mode_t s_page_mode;      /* 当前页面: 自动监测页(CTRL_MODE_AUTO)/手动控制页(CTRL_MODE_MANUAL) */
static uint8_t     s_fan_auto;       /* 手动页风扇"自动挡"标志 */
static uint8_t     s_lamp_auto;      /* 手动页台灯"自动挡"标志 */
static uint8_t     s_fan_level;      /* 手动档 0..FAN_LEVELS-1 */
static uint16_t    s_lamp_bright;

/* 上次实际下发值（用于变化检测, 避免周期性重复发送） */
static uint8_t  s_last_fan  = 0xFFu;
static uint8_t  s_last_lamp = 0u;
static uint8_t  s_lamp_sent = 0u;   /* 台灯是否已下发过(亮度可达255=0xFF, 不能用0xFF做哨兵) */

static uint8_t map_temp_to_level(int16_t temp_x10)
{
  int t = (int)temp_x10;              /* 0.1°C */
  if (t <= 200)  return 0u;           /* <=20°C 停 */
  if (t <= 220)  return 1u;
  if (t <= 240)  return 2u;
  if (t <= 260)  return 3u;
  if (t <= 280)  return 4u;
  if (t <= 300)  return 5u;
  if (t <= 320)  return 6u;
  return (uint8_t)(FAN_LEVELS - 1u);
}

static uint16_t lux_to_bright(uint16_t lux)
{
  /* 开关式: 光照低于 200lux -> 全亮; 否则灭 (无阶梯) */
  if (lux < 200u)  return LAMP_PWM_MAX;
  return 0u;
}

void SvcEnv_Init(app_config_t *cfg)
{
  s_page_mode   = (ctrl_mode_t)cfg->ctrl_mode;
  s_fan_level   = 0u;   /* 启动时风扇从"关"开始: 避免上电即恢复高速档, 在弱电源下触发复位循环 */
  s_lamp_bright = (uint16_t)(cfg->lamp_brightness > LAMP_PWM_MAX ? LAMP_PWM_MAX : cfg->lamp_brightness);
  s_fan_auto    = 0u;
  s_lamp_auto   = 0u;
}

#define FAN_RAMP_STEP  15u   /* 风扇软启动: 每次(1s)最多上升15%, 避免启动/堵转大电流拉垮电源 */
#define FAN_KICK_PCT   40u   /* 启动"踢一脚"占空比(%): 各档启动统一先给40%, 克服静摩擦后由软启动爬升 */
#define FAN_KICK_MS    400u  /* 踢一脚持续时间(ms) */

static uint32_t s_fan_kick_until = 0u;

static void apply_fan(uint8_t target)
{
  uint32_t now = BSP_GetTick();
  uint8_t  out;

  if (target == 0u)
  {
    s_fan_kick_until = 0u;                 /* 关闭: 清启动状态 */
    out = 0u;
  }
  else if (now < s_fan_kick_until)
  {
    /* 启动kick期间: 统一给 KICK_PCT(40%) 上限, 高档也先压低, 避免启动大电流 */
    out = FAN_KICK_PCT;
  }
  else if (s_last_fan == 0u)
  {
    /* 从停止启动: 触发kick(克服静摩擦), 之后软启动爬升 */
    s_fan_kick_until = now + FAN_KICK_MS;
    out = FAN_KICK_PCT;
  }
  else if (target > s_last_fan)
  {
    out = (uint8_t)(s_last_fan + FAN_RAMP_STEP);  /* 升速限幅软启动 */
    if (out > target) out = target;
  }
  else
  {
    out = target;                            /* 降速/保持: 立即 */
  }

  if (out != s_last_fan)
  {
    BspFan_SetSpeed(out);
    s_last_fan = out;
  }
}

static uint32_t s_lamp_last_force = 0u;

static void apply_lamp(uint8_t bright)
{
  uint32_t now = BSP_GetTick();
  /* 首帧必发 + 变化检测 + 每5s强制刷新(恢复电源抖动导致的WS2812花屏/全亮) */
  if ((s_lamp_sent == 0u) || (bright != s_last_lamp) ||
      ((now - s_lamp_last_force) >= 5000u))
  {
    BspWs2812_SetBright(bright);
    s_last_lamp = bright;
    s_lamp_sent = 1u;
    s_lamp_last_force = now;
  }
}

void SvcEnv_Update(const sensor_data_t *sen, sys_mode_t mode, app_config_t *cfg,
                   occupy_state_t occ)
{
  (void)cfg;

  if (mode == SYS_MODE_SLEEP)      /* 手动切入休眠: 强制关闭 */
  {
    apply_fan(0u);
    apply_lamp(0u);
    return;
  }

  if (s_page_mode == CTRL_MODE_AUTO)  /* 自动监测页: 需有人且达到阈值才开 */
  {
    uint8_t  f = 0u;
    uint16_t l = 0u;
    if ((occ == OCCUPY_HUMAN) && (sen != NULL))
    {
      uint8_t lvl = map_temp_to_level(sen->temp_x10);
      f = (uint8_t)((uint32_t)lvl * 100u / (FAN_LEVELS - 1u));
      l = lux_to_bright(sen->lux);
    }
    apply_fan(f);
    apply_lamp((uint8_t)l);
    return;
  }

  /* 手动控制页: 各设备按"自动挡/手动挡"独立控制, 不看摄像头 */
  if ((s_fan_auto != 0u) && (sen != NULL))
  {
    uint8_t lvl = map_temp_to_level(sen->temp_x10);
    apply_fan((uint8_t)((uint32_t)lvl * 100u / (FAN_LEVELS - 1u)));
  }
  else if (s_fan_auto == 0u)
  {
    apply_fan((uint8_t)((uint32_t)s_fan_level * 100u / (FAN_LEVELS - 1u)));
  }

  if ((s_lamp_auto != 0u) && (sen != NULL))
  {
    apply_lamp((uint8_t)lux_to_bright(sen->lux));
  }
  else if (s_lamp_auto == 0u)
  {
    apply_lamp((uint8_t)s_lamp_bright);
  }
}

uint8_t SvcEnv_FanLevel(void)      { return s_fan_level; }
uint16_t SvcEnv_LampBrightness(void){ return s_lamp_bright; }
ctrl_mode_t SvcEnv_CtrlMode(void)  { return s_page_mode; }

void SvcEnv_SetFanManual(uint8_t level)
{
  if (level < FAN_LEVELS)
  {
    s_fan_level = level;
    s_fan_auto  = 0u;
  }
}

void SvcEnv_SetFanAuto(void)
{
  s_fan_auto = 1u;                 /* 手动页风扇自动挡 */
}

void SvcEnv_SetLampManual(uint8_t bright)
{
  if (bright <= LAMP_PWM_MAX)
  {
    s_lamp_bright = bright;
    s_lamp_auto   = 0u;
  }
}

void SvcEnv_SetLampAuto(void)
{
  s_lamp_auto = 1u;                /* 手动页台灯自动挡 */
}

void SvcEnv_SetCtrlMode(ctrl_mode_t m)
{
  /* 0x02 页面切换: 只改页面模式, 不影响手动页各设备自动挡 */
  if ((m == CTRL_MODE_AUTO) || (m == CTRL_MODE_MANUAL))
  {
    s_page_mode = m;
  }
}