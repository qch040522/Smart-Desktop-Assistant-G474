/**
 * @file    svc_config.c
 * @brief   配置服务实现（Flash 整扇区读写）。
 */
#include <string.h>
#include "bsp.h"
#include "bsp_flash.h"
#include "svc_config.h"
#include "svc_timer.h"

/* 末扇区偏移 = 总Flash大小(512KB) - 扇区大小(2KB) */
#define CFG_SECT_ADDR    FLASH_USER_SECTOR_ADDR

void SvcConfig_SetDefaults(app_config_t *cfg)
{
  if (cfg == NULL) return;

  cfg->magic           = FLASH_CFG_MAGIC;
  cfg->version         = 1u;
  cfg->sys_mode        = SYS_MODE_LEISURE;
  cfg->ctrl_mode       = CTRL_MODE_AUTO;
  cfg->angle_base      = (int16_t)POSTURE_BASE_ANGLE_DEFAULT;
  cfg->angle_threshold = POSTURE_TH_LVL_DEFAULT;
  cfg->alarm_hour      = 7u;
  cfg->alarm_min       = 0u;
  cfg->alarm_en        = 0u;
  cfg->alarm_repeat    = ALARM_REPEAT_DAILY;   /* 默认每日 */
  cfg->alarm_weekday   = 1u;                   /* 1=周一 */
  cfg->pomodoro_min    = (uint16_t)POMO_DEFAULT_MINUTES;
  cfg->pomodo_en       = 0u;
  cfg->fan_level       = 4u;
  cfg->lamp_brightness = 90u;
}

int SvcConfig_Load(app_config_t *cfg)
{
  uint8_t tmp[sizeof(app_config_t)];

  if (cfg == NULL) return -1;

  BspFlash_Read(CFG_SECT_ADDR, tmp, sizeof(app_config_t));
  memcpy(cfg, tmp, sizeof(app_config_t));

  if (cfg->magic != FLASH_CFG_MAGIC)
  {
    SvcConfig_SetDefaults(cfg);
    SvcConfig_Save(cfg);
    return -1;
  }
  return 0;
}

int SvcConfig_Save(const app_config_t *cfg)
{
  uint8_t tmp[sizeof(app_config_t)];
  int     rc;

  if (cfg == NULL) return -1;

  BspFlash_ErasePage(CFG_SECT_ADDR);
  memset(tmp, 0xFFu, sizeof(tmp));
  memcpy(tmp, cfg, sizeof(app_config_t));
  rc = BspFlash_Program(CFG_SECT_ADDR, tmp, sizeof(app_config_t));
  return rc;
}