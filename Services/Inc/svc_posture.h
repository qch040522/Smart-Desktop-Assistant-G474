/**
 * @file    svc_posture.h
 * @brief   坐姿服务：头-肩下沉角时序滤波、低头告警(1.5s保持+冷却)、校准会话。
 */
#ifndef SVC_POSTURE_H
#define SVC_POSTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_types.h"

typedef enum {
  CALIB_IDLE = 0,
  CALIB_WAITING,     /* 已下发 0x10, 等待 ESP32 回传基准角 */
  CALIB_PROGRESS     /* 可选: 本地采样阶段(如需要) */
} calib_state_t;

/** 初始化（base 初值, 阈值档整体由 state 服务提供） */
void SvcPosture_Init(int16_t base_angle_x10);

/**
 * 收到一帧 AI 后处理:
 *   - 更新低通滤波 angle
 *   - 依据 current head/pitch/occupy 判定是否触发告警(内部按 now 累积 1.5s)
 *  返回 1=刚触发了一次新告警(供 LED/UI闪烁), 0=未触发。
 */
uint8_t SvcPosture_OnAiFrame(const ai_frame_t *f, sys_mode_t mode,
                             uint8_t threshold_deg, int16_t base_angle_x10,
                             uint32_t now_ms);

/** 当前滤波后角度 (0.1°) 只读 */
int16_t SvcPosture_Angle(void);

/** 告警当前是否处于点燃状态（供 UI 保持显示） */
uint8_t SvcPosture_AlarmActive(void);

/** 校准控制 */
void        SvcPosture_StartCalibration(void);
void        SvcPosture_HandleCalibResult(int16_t base_angle_x10);
calib_state_t SvcPosture_CalibState(void);
int16_t       SvcPosture_GetBaseAngle(void);

/* 归一化了未被利用的参数 -> 无 */

#ifdef __cplusplus
}
#endif

#endif /* SVC_POSTURE_H */