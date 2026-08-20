/**
 * @file    bsp_buzzer.h
 * @brief   无源蜂鸣器 驱动（TIM3_CH1, PA6, PWM 变频 2~4kHz）。
 */
#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void BspBuzzer_Init(void);

/** 开始鸣叫指定频率 (Hz), 1000~8000 内有效 */
void BspBuzzer_BeepOn(uint16_t freq_hz);

/** 停止鸣叫 */
void BspBuzzer_BeepOff(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUZZER_H */