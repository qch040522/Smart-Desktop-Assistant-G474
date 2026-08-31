/**
 * @file    svc_state.h
 * @brief   状态机服务：顶层模式(自动/手动)、学习休闲子状态、无人/断链判定、命令应用。
 */
#ifndef SVC_STATE_H
#define SVC_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

/** 初始化状态服务（从配置恢复上次模式） */
void SvcState_Init(app_config_t *cfg);

/** 周期调用(上报周期1次): 依据本周是否有任何帧/是否有有效帧推进无人与断链 */
void SvcState_Tick(uint32_t now_ms, uint8_t any_frame, uint8_t valid_frame);

/** 撤离由外部指令引起的模式变更 */
void SvcState_SetMode(sys_mode_t m);

/** 设置学习/休闲子状态 */
void SvcState_SetStudyMode(study_mode_t m);

/** 外部命令对象（触摸屏/ESP32/MQTT/系统）应用入口 */
void SvcState_ApplyCmd(const app_cmd_t *cmd, app_config_t *cfg);

/** 当前顶层模式 / 学习休闲子状态 / 无人 / 断链查询 */
sys_mode_t    SvcState_Mode(void);
study_mode_t  SvcState_Study(void);
occupy_state_t SvcState_Occupy(void);
link_state_t   SvcState_Link(void);

/** 取当前告警阈值(°) */
uint8_t SvcState_ThresholdDeg(void);

/** 切换阈值档（0~3） */
void SvcState_SetThresholdLvl(uint8_t lvl);

#ifdef __cplusplus
}
#endif

#endif /* SVC_STATE_H */