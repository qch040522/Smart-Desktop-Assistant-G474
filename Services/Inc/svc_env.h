/**
 * @file    svc_env.h
 * @brief   环境控制服务：风扇(温湿度→PWM)、台灯(光照→蓝色亮度)。
 *          手动优先 = 手动持久，直到用户切回自动。
 */
#ifndef SVC_ENV_H
#define SVC_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

/** 初始化（沿用配置中的手动档/亮度记忆） */
void SvcEnv_Init(app_config_t *cfg);

/**
 * 周期性计算并应用风扇/台灯目标（由 env 任务每 ~1s 调用）。
 *
 * @param sen   传感器(温湿度/光照)
 * @param mode  当前业务模式（休眠时关闭）
 * @param cfg   配置(读手动/自动)
 */
void SvcEnv_Update(const sensor_data_t *sen, sys_mode_t mode, app_config_t *cfg);

/** 查询当前应输出（供 0x12 / UI 汇总） */
uint8_t  SvcEnv_FanLevel(void);
uint16_t SvcEnv_LampBrightness(void);

/** 手动指令注入（管理员为外部命令） */
void SvcEnv_SetFanManual(uint8_t level);
void SvcEnv_SetLampManual(uint8_t bright);
void SvcEnv_SetCtrlMode(ctrl_mode_t m);
ctrl_mode_t SvcEnv_CtrlMode(void);

#ifdef __cplusplus
}
#endif

#endif /* SVC_ENV_H */