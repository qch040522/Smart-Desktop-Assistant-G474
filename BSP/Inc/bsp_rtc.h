/**
 * @file    bsp_rtc.h
 * @brief   STM32G474 内建 RTC（VBAT 备份）驱动。
 */
#ifndef BSP_RTC_H
#define BSP_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 读取时间（24小时制） */
void BspRtc_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec);

/** 读取日期（年=后两位, 月, 日, 星期1~7） */
void BspRtc_GetDate(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday);

/** 转为 yyyymmdd 整数（用于"跨天清零"判断） */
uint32_t BspRtc_GetYmd(void);

/** 设置日期与时间（调用者希望强制校时） */
int BspRtc_SetDateTime(uint8_t year, uint8_t month, uint8_t day,
                       uint8_t weekday, uint8_t hour, uint8_t min, uint8_t sec);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */