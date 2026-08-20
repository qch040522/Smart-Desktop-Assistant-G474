/**
 * @file    bsp_led.h
 * @brief   坐姿提醒 LED（PB4, 推挽输出）。
 */
#ifndef BSP_LED_H
#define BSP_LED_H

#ifdef __cplusplus
extern "C" {
#endif

void BspLed_Init(void);
void BspLed_On(void);
void BspLed_Off(void);
void BspLed_Toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */