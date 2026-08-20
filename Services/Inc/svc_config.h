/**
 * @file    svc_config.h
 * @brief   业务配置层：读取/保存 app_config_t（低频, 存 Flash 末扇区）。
 *          高频时长/状态由 RAM 计时服务维护, 仅在模式退出/跨天/定时快照落盘。
 */
#ifndef SVC_CONFIG_H
#define SVC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

/** 启动时从 Flash 加载配置; 若签名非法则写入默认值并返回 -1 */
int  SvcConfig_Load(app_config_t *cfg);

/** 保存当前配置到 Flash（内部擦除+写整扇区） */
int  SvcConfig_Save(const app_config_t *cfg);

/** 用一个配置对象填充出厂默认值 */
void SvcConfig_SetDefaults(app_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SVC_CONFIG_H */