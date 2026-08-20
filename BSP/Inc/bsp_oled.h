/**
 * @file    bsp_oled.h
 * @brief   SSD1306 0.96" OLED（I2C2, 0x3C, 128x64）。
 *          128×64 单页信息, 整页刷新。
 */
#ifndef BSP_OLED_H
#define BSP_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define OLED_WIDTH     128u
#define OLED_HEIGHT    64u
#define OLED_PAGES     8u
#define OLED_FB_SIZE   (OLED_WIDTH * OLED_PAGES)   /* 1024B */

#define OLED_CHAR_W    5u      /* 字符宽 */
#define OLED_CHAR_GAP  1u      /* 字符列距 */
#define OLED_LINE_H    8u      /* 行高=1页 */

/** 初始化控制器 + 清屏 */
void BspOled_Init(void);

/** 清除帧缓冲（不立即显示） */
void BspOled_Clear(void);

/** 打开/关闭显示（0=关, 1=开, 不影响缓冲） */
void BspOled_Display(uint8_t on);

/** 在 (col, row) 位置绘制一行文本, row 范围 0~7 */
void BspOled_Puts(uint8_t row, uint8_t col, const char *str);

/** 2 倍放大绘制一行文本(5x8 字体按 2x2 放大 -> 10x16/字符, 行距12列)。
 *  page 必须为偶数(0/2/4/6), 字符占 page 与 page+1 两页(16px 高)。
 *  每行最多 10 个字符(128/12)。 */
void BspOled_Puts2x(uint8_t page, uint8_t col, const char *str);

/** 将帧缓冲整体刷新到屏幕 */
void BspOled_Flush(void);

/** 直接整行填充（进度条等）: row 页, from_col~to_col 置点 */
void BspOled_FillRow(uint8_t row, uint8_t from, uint8_t to, uint8_t on);

#ifdef __cplusplus
}
#endif

#endif /* BSP_OLED_H */