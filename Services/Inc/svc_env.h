/**
 * @file    svc_env.h
 * @brief   环境控制服务：风扇(温湿度→PWM)、台灯(光照→白光开关)。
 *          手动控制页: 各设备按自动挡/手动挡独立控制(不看摄像头);
 *          自动监测页: 需有人+阈值才开。
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
 * @param mode  当前业务模式（手动切入休眠时强制关闭）
 * @param cfg   配置(读手动/自动)
 * @param occ   有人/无人状态（自动监测页: 需有人才开; 手动页不看）
 */
void SvcEnv_Update(const sensor_data_t *sen, sys_mode_t mode, app_config_t *cfg,
                   occupy_state_t occ);

/** 查询当前应输出（供 0x12 / UI 汇总） */
uint8_t  SvcEnv_FanLevel(void);
uint16_t SvcEnv_LampBrightness(void);

/** 手动指令注入（管理员为外部命令） */
void SvcEnv_SetFanManual(uint8_t level);
void SvcEnv_SetFanAuto(void);
void SvcEnv_SetLampManual(uint8_t bright);
void SvcEnv_SetLampAuto(void);
void SvcEnv_SetCtrlMode(ctrl_mode_t m);   /* 页面切换(0x02) */
ctrl_mode_t SvcEnv_CtrlMode(void);

#ifdef __cplusplus
}
#endif

#endif /* SVC_ENV_H */