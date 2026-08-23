/**
 * @file    svc_config.c
 * @brief   配置服务实现（Flash 整扇区读写）。
 */
#include <string.h>
#include <stddef.h>
#include "bsp.h"
#include "bsp_flash.h"
#include "bsp_uart_link.h"   /* 复用 BspUartLink_Crc16 */
#include "svc_config.h"
#include "svc_timer.h"

/* 末扇区偏移 = 总Flash大小(512KB) - 扇区大小(2KB) */
#define CFG_SECT_ADDR    FLASH_USER_SECTOR_ADDR
/* CRC 覆盖 crc 字段之前的全部字节(含对齐保留字节) */
#define CFG_CRC_LEN      (offsetof(app_config_t, crc))

void SvcConfig_SetDefaults(app_config_t *cfg)
{
  if (cfg == NULL) return;

  cfg->magic           = FLASH_CFG_MAGIC;
  cfg->version         = 1u;
  cfg->sys_mode        = SYS_MODE_LEISURE;
  cfg->ctrl_mode       = CTRL_MODE_AUTO;
  cfg->angle_base      = (int16_t)POSTURE_BASE_ANGLE_DEFAULT;
  cfg->angle_threshold = POSTURE_TH_LVL_DEFAULT;
  cfg->alarm_hour      = 0u;                   /* 默认 0:00 */
  cfg->alarm_min       = 0u;
  cfg->alarm_en        = 0u;
  cfg->alarm_repeat    = ALARM_REPEAT_DAILY;   /* 默认每日 */
  cfg->alarm_weekday   = 1u;                   /* 1=周一 */
  cfg->pomodoro_min    = (uint16_t)POMO_DEFAULT_MINUTES;  /* 默认0=关闭 */
  cfg->pomodo_en       = 0u;
  cfg->fan_level       = 4u;
  cfg->lamp_brightness = 90u;
  cfg->reserved        = 0xFFu;   /* 对齐字节固定 */
  cfg->crc             = 0xFFFFu; /* 占位, Save 时重新计算 */
}

/* 字段合法性校验: 拦截断电导致 Flash 半擦写产生的 0xFF 损坏值 */
static int cfg_fields_valid(const app_config_t *cfg)
{
  if (cfg == NULL) return 0;
  if (cfg->sys_mode >= SYS_MODE_NUM)            return 0;
  if (cfg->ctrl_mode > CTRL_MODE_MANUAL)        return 0;
  if (cfg->fan_level >= FAN_LEVELS)             return 0;
  if (cfg->lamp_brightness > LAMP_PWM_MAX)      return 0;
  if (cfg->alarm_repeat > ALARM_REPEAT_WEEKLY)  return 0;
  if (cfg->alarm_weekday > 7u)                  return 0;
  if (cfg->pomodoro_min > POMO_MAX_MINUTES)     return 0;
  return 1;
}

int SvcConfig_Load(app_config_t *cfg)
{
  uint8_t  tmp[sizeof(app_config_t)];
  uint16_t stored_crc;
  uint16_t calc_crc;

  if (cfg == NULL) return -1;

  BspFlash_Read(CFG_SECT_ADDR, tmp, sizeof(app_config_t));
  memcpy(cfg, tmp, sizeof(app_config_t));

  stored_crc = (uint16_t)((uint16_t)tmp[CFG_CRC_LEN] | ((uint16_t)tmp[CFG_CRC_LEN + 1u] << 8u));
  calc_crc   = BspUartLink_Crc16(tmp, CFG_CRC_LEN);

  /* 任一校验不过(签名/CRC/字段越界) -> 用默认值并重写干净配置 */
  if ((cfg->magic != FLASH_CFG_MAGIC) ||
      (stored_crc != calc_crc) ||
      (!cfg_fields_valid(cfg)))
  {
    SvcConfig_SetDefaults(cfg);
    SvcConfig_Save(cfg);
    return -1;
  }
  return 0;
}

int SvcConfig_Save(const app_config_t *cfg)
{
  uint8_t  tmp[sizeof(app_config_t)];
  uint16_t c;
  int      rc;

  if (cfg == NULL) return -1;

  memset(tmp, 0xFFu, sizeof(tmp));
  memcpy(tmp, cfg, sizeof(app_config_t));
  /* 计算并写入 CRC(覆盖 crc 之前的全部字节) */
  c = BspUartLink_Crc16(tmp, CFG_CRC_LEN);
  tmp[CFG_CRC_LEN]      = (uint8_t)(c & 0xFFu);
  tmp[CFG_CRC_LEN + 1u] = (uint8_t)(c >> 8u);

  BspFlash_ErasePage(CFG_SECT_ADDR);
  rc = BspFlash_Program(CFG_SECT_ADDR, tmp, sizeof(app_config_t));
  return rc;
}