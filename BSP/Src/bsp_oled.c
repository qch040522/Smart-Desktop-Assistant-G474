/**
 * @file    bsp_oled.c
 * @brief   SSD1306 0.96" OLED 驱动实现（I2C2, 0x3C, 128x64）。
 *
 *  页寻址模式: 8 页 × 128 字节 = 1024B 帧缓冲。
 *  文本绘制使用 5x8 点阵字体（含 1 像素列距）。
 */
#include "bsp.h"
#include "bsp_oled.h"
#include "app_rtos.h"
#include <string.h>

/* ==================== SSD1306 常量 ==================== */
#define SSD1306_I2C_ADDR    ((uint16_t)(0x3Cu << 1u))  /* 7位地址 0x3C 左移1位 */
#define SSD1306_CTRL_CMD    0x00u                       /* I2C 控制字节: 命令 */
#define SSD1306_CTRL_DATA   0x40u                       /* I2C 控制字节: 数据 */
#define SSD1306_TIMEOUT_MS  100u

/* 页寻址页地址命令 */
#define SSD1306_PAGE_BASE   0xB0u

/* ==================== 初始化序列 ==================== */
/* 标准 SSD1306 128x64 初始化 (内部电荷泵, 页寻址模式) */
static const uint8_t s_init_seq[] = {
    0xAE,                    /* 显示关闭 */
    0xD5, 0x80,              /* 设置显示时钟: 分频比=1, 震荡器=8 */
    0xA8, 0x3F,              /* 设置复用比 (64-1=0x3F) */
    0xD3, 0x00,              /* 设置显示偏移 (无偏移) */
    0x40,                    /* 设置显示起始行 = 0 */
    0x8D, 0x14,              /* 开启电荷泵 (内部 VCC) */
    0x20, 0x02,              /* 设置内存寻址模式: 页寻址 */
    0xA1,                    /* 设置段重映射 (column 127 -> 0) */
    0xC8,                    /* 设置 COM 输出扫描方向反转 */
    0xDA, 0x12,              /* 设置 COM 硬件引脚 (顺序, 左右重映射) */
    0x81, 0xCF,              /* 设置对比度 (高) */
    0xA4,                    /* 输出跟随 RAM 内容 */
    0xA6,                    /* 正常显示 (不反白) */
    0xD9, 0x1F,              /* 设置预充电周期 */
    0xDB, 0x20,              /* 设置 VCOMH 脱离电平 (0.77*Vcc) */
    0xAF                     /* 显示打开 */
};

/* ==================== 5x8 ASCII 字体表 ==================== */
/* 每字符 5 字节, 每字节为一列 (bit7=上, bit0=下), 覆盖 0x20~0x7F */
/* 特殊: 字符 '~'(0x7E) 在 BspOled_Puts 中映射为"度"符号 ° */
/* 小号 3x3 圆环, 仅用前 3 列, 后 2 列留空 */
static const uint8_t s_deg5x8[5] = { 0x02u, 0x05u, 0x02u, 0x00u, 0x00u };
static const uint8_t s_font5x8[96][5] = {
    /* 0x20 space */  {0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x21 ! */      {0x00, 0x00, 0x5F, 0x00, 0x00},
    /* 0x22 " */      {0x00, 0x07, 0x00, 0x07, 0x00},
    /* 0x23 # */      {0x14, 0x7F, 0x14, 0x7F, 0x14},
    /* 0x24 $ */      {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    /* 0x25 % */      {0x23, 0x13, 0x08, 0x64, 0x62},
    /* 0x26 & */      {0x36, 0x49, 0x55, 0x22, 0x50},
    /* 0x27 ' */      {0x00, 0x05, 0x03, 0x00, 0x00},
    /* 0x28 ( */      {0x00, 0x1C, 0x22, 0x41, 0x00},
    /* 0x29 ) */      {0x00, 0x41, 0x22, 0x1C, 0x00},
    /* 0x2A * */      {0x08, 0x04, 0x7F, 0x04, 0x08},
    /* 0x2B + */      {0x08, 0x08, 0x3E, 0x08, 0x08},
    /* 0x2C , */      {0x00, 0x50, 0x30, 0x00, 0x00},
    /* 0x2D - */      {0x08, 0x08, 0x08, 0x08, 0x08},
    /* 0x2E . */      {0x00, 0x60, 0x60, 0x00, 0x00},
    /* 0x2F / */      {0x20, 0x10, 0x08, 0x04, 0x02},
    /* 0x30 0 */      {0x3E, 0x51, 0x49, 0x45, 0x3E},
    /* 0x31 1 */      {0x00, 0x42, 0x7F, 0x40, 0x00},
    /* 0x32 2 */      {0x42, 0x61, 0x51, 0x49, 0x46},
    /* 0x33 3 */      {0x21, 0x41, 0x45, 0x4B, 0x31},
    /* 0x34 4 */      {0x18, 0x14, 0x12, 0x7F, 0x10},
    /* 0x35 5 */      {0x27, 0x45, 0x45, 0x45, 0x39},
    /* 0x36 6 */      {0x3C, 0x4A, 0x49, 0x49, 0x30},
    /* 0x37 7 */      {0x01, 0x71, 0x09, 0x05, 0x03},
    /* 0x38 8 */      {0x36, 0x49, 0x49, 0x49, 0x36},
    /* 0x39 9 */      {0x06, 0x49, 0x49, 0x29, 0x1E},
    /* 0x3A : */      {0x00, 0x36, 0x36, 0x00, 0x00},
    /* 0x3B ; */      {0x00, 0x56, 0x36, 0x00, 0x00},
    /* 0x3C < */      {0x08, 0x14, 0x22, 0x41, 0x00},
    /* 0x3D = */      {0x14, 0x14, 0x14, 0x14, 0x14},
    /* 0x3E > */      {0x00, 0x41, 0x22, 0x14, 0x08},
    /* 0x3F ? */      {0x02, 0x01, 0x51, 0x09, 0x06},
    /* 0x40 @ */      {0x32, 0x49, 0x79, 0x41, 0x3E},
    /* 0x41 A */      {0x7E, 0x11, 0x11, 0x11, 0x7E},
    /* 0x42 B */      {0x7F, 0x49, 0x49, 0x49, 0x36},
    /* 0x43 C */      {0x3E, 0x41, 0x41, 0x41, 0x22},
    /* 0x44 D */      {0x7F, 0x41, 0x41, 0x41, 0x3E},
    /* 0x45 E */      {0x7F, 0x49, 0x49, 0x49, 0x41},
    /* 0x46 F */      {0x7F, 0x09, 0x09, 0x01, 0x01},
    /* 0x47 G */      {0x3E, 0x41, 0x49, 0x49, 0x7A},
    /* 0x48 H */      {0x7F, 0x08, 0x08, 0x08, 0x7F},
    /* 0x49 I */      {0x00, 0x41, 0x7F, 0x41, 0x00},
    /* 0x4A J */      {0x20, 0x40, 0x41, 0x3F, 0x01},
    /* 0x4B K */      {0x7F, 0x08, 0x14, 0x22, 0x41},
    /* 0x4C L */      {0x7F, 0x40, 0x40, 0x40, 0x40},
    /* 0x4D M */      {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    /* 0x4E N */      {0x7F, 0x04, 0x08, 0x10, 0x7F},
    /* 0x4F O */      {0x3E, 0x41, 0x41, 0x41, 0x3E},
    /* 0x50 P */      {0x7F, 0x09, 0x09, 0x09, 0x06},
    /* 0x51 Q */      {0x3E, 0x41, 0x51, 0x21, 0x5E},
    /* 0x52 R */      {0x7F, 0x09, 0x19, 0x29, 0x46},
    /* 0x53 S */      {0x46, 0x49, 0x49, 0x49, 0x31},
    /* 0x54 T */      {0x01, 0x01, 0x7F, 0x01, 0x01},
    /* 0x55 U */      {0x3F, 0x40, 0x40, 0x40, 0x3F},
    /* 0x56 V */      {0x1F, 0x20, 0x40, 0x20, 0x1F},
    /* 0x57 W */      {0x7F, 0x20, 0x18, 0x20, 0x7F},
    /* 0x58 X */      {0x63, 0x14, 0x08, 0x14, 0x63},
    /* 0x59 Y */      {0x07, 0x08, 0x70, 0x08, 0x07},
    /* 0x5A Z */      {0x61, 0x51, 0x49, 0x45, 0x43},
    /* 0x5B [ */      {0x00, 0x7F, 0x41, 0x41, 0x00},
    /* 0x5C \ */      {0x02, 0x04, 0x08, 0x10, 0x20},
    /* 0x5D ] */      {0x00, 0x41, 0x41, 0x7F, 0x00},
    /* 0x5E ^ */      {0x04, 0x02, 0x01, 0x02, 0x04},
    /* 0x5F _ */      {0x40, 0x40, 0x40, 0x40, 0x40},
    /* 0x60 ` */      {0x00, 0x01, 0x02, 0x04, 0x00},
    /* 0x61 a */      {0x20, 0x54, 0x54, 0x54, 0x78},
    /* 0x62 b */      {0x7F, 0x48, 0x44, 0x44, 0x38},
    /* 0x63 c */      {0x38, 0x44, 0x44, 0x44, 0x20},
    /* 0x64 d */      {0x38, 0x44, 0x44, 0x48, 0x7F},
    /* 0x65 e */      {0x38, 0x54, 0x54, 0x54, 0x18},
    /* 0x66 f */      {0x08, 0x7E, 0x09, 0x01, 0x02},
    /* 0x67 g */      {0x08, 0x19, 0x12, 0x12, 0x14},
    /* 0x68 h */      {0x7F, 0x08, 0x04, 0x04, 0x78},
    /* 0x69 i */      {0x00, 0x44, 0x7D, 0x40, 0x00},
    /* 0x6A j */      {0x20, 0x40, 0x44, 0x3D, 0x00},
    /* 0x6B k */      {0x00, 0x7F, 0x08, 0x14, 0x63},
    /* 0x6C l */      {0x00, 0x41, 0x7F, 0x40, 0x00},
    /* 0x6D m */      {0x7C, 0x04, 0x18, 0x04, 0x78},
    /* 0x6E n */      {0x7C, 0x08, 0x04, 0x04, 0x78},
    /* 0x6F o */      {0x38, 0x44, 0x44, 0x44, 0x38},
    /* 0x70 p */      {0x7C, 0x14, 0x14, 0x14, 0x08},
    /* 0x71 q */      {0x08, 0x14, 0x14, 0x14, 0x7C},
    /* 0x72 r */      {0x00, 0x7C, 0x08, 0x04, 0x04},
    /* 0x73 s */      {0x48, 0x54, 0x54, 0x54, 0x20},
    /* 0x74 t */      {0x04, 0x3F, 0x44, 0x40, 0x20},
    /* 0x75 u */      {0x3C, 0x40, 0x40, 0x20, 0x7C},
    /* 0x76 v */      {0x1C, 0x20, 0x40, 0x20, 0x1C},
    /* 0x77 w */      {0x3C, 0x40, 0x30, 0x40, 0x3C},
    /* 0x78 x */      {0x44, 0x28, 0x10, 0x28, 0x44},
    /* 0x79 y */      {0x0C, 0x50, 0x50, 0x50, 0x3C},
    /* 0x7A z */      {0x44, 0x64, 0x54, 0x4C, 0x44},
    /* 0x7B { */      {0x00, 0x08, 0x36, 0x41, 0x00},
    /* 0x7C | */      {0x00, 0x00, 0x7F, 0x00, 0x00},
    /* 0x7D } */      {0x00, 0x41, 0x36, 0x08, 0x00},
    /* 0x7E ~ */      {0x08, 0x04, 0x08, 0x10, 0x08},
};

/* ==================== 帧缓冲 ==================== */
static uint8_t s_fb[OLED_FB_SIZE];

/* ==================== 内部函数 ==================== */

/**
 * @brief  发送 SSD1306 命令字节流 (通过 I2C2)
 * @param  cmd: 命令数据指针
 * @param  len: 命令字节数 (含命令码 + 参数)
 */
static void oled_send_cmd(const uint8_t *cmd, uint16_t len)
{
    uint8_t buf[33u];  /* 1 字节控制 + 最大 32 字节命令 */
    uint16_t i;

    if (len > 32u || cmd == NULL) return;
    buf[0] = SSD1306_CTRL_CMD;
    for (i = 0u; i < len; i++)
    {
        buf[i + 1u] = cmd[i];
    }
    if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c2, SSD1306_I2C_ADDR, buf, len + 1u, SSD1306_TIMEOUT_MS);
    if (mtxI2c != NULL) osMutexRelease(mtxI2c);
}

/**
 * @brief  发送 SSD1306 数据字节流 (通过 I2C2)
 * @param  data: 数据指针
 * @param  len:  数据字节数 (最大 128)
 */
static void oled_send_data(const uint8_t *data, uint16_t len)
{
    uint8_t buf[OLED_WIDTH + 1u];  /* 1 字节控制 + 最大 128 字节数据 */
    uint16_t i;

    if (len > OLED_WIDTH || data == NULL) return;
    buf[0] = SSD1306_CTRL_DATA;
    for (i = 0u; i < len; i++)
    {
        buf[i + 1u] = data[i];
    }
    if (mtxI2c != NULL) osMutexAcquire(mtxI2c, osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c2, SSD1306_I2C_ADDR, buf, len + 1u, SSD1306_TIMEOUT_MS);
    if (mtxI2c != NULL) osMutexRelease(mtxI2c);
}

/**
 * @brief  设置页地址和列地址 (页寻址模式下)
 * @param  page: 页号 (0~7)
 * @param  col:  列地址 (0~127)
 */
static void oled_set_pos(uint8_t page, uint8_t col)
{
    uint8_t cmd[3];

    page &= 0x07u;
    col  &= 0x7Fu;

    /* 页地址 + 列地址低4位 + 列地址高4位 */
    cmd[0] = (uint8_t)(SSD1306_PAGE_BASE | page);
    cmd[1] = (uint8_t)(0x00u | (col & 0x0Fu));
    cmd[2] = (uint8_t)(0x10u | ((col >> 4u) & 0x0Fu));
    oled_send_cmd(cmd, 3u);
}

/* ==================== 公共 API ==================== */

/**
 * @brief  初始化 SSD1306 控制器 + 清屏刷新
 */
void BspOled_Init(void)
{
    /* 发送初始化序列 */
    oled_send_cmd(s_init_seq, sizeof(s_init_seq));

    /* 清屏 + 刷新到屏幕 */
    BspOled_Clear();
    BspOled_Flush();
}

/**
 * @brief  清除帧缓冲 (不立即显示到屏幕)
 */
void BspOled_Clear(void)
{
    memset(s_fb, 0, OLED_FB_SIZE);
}

/**
 * @brief  打开/关闭显示 (0=关, 1=开, 不影响帧缓冲内容)
 * @param  on: 1=显示, 0=关闭显示
 */
void BspOled_Display(uint8_t on)
{
    uint8_t cmd = on ? 0xAFu : 0xAEu;
    oled_send_cmd(&cmd, 1u);
}

/**
 * @brief  在 (row, col) 位置绘制字符串
 * @param  row: 页号 (0~7), 每页高度 8 像素
 * @param  col: 字符列 (0 起), 每字符占 6 像素 (5 字体 + 1 间距)
 * @param  str: 以 '\0' 结尾的字符串
 */
void BspOled_Puts(uint8_t row, uint8_t col, const char *str)
{
    uint8_t  base_col;
    uint8_t  base_col_start;
    uint16_t fb_offset;
    uint8_t  fcol;
    const uint8_t *font;

    if (str == NULL) return;
    if (row >= OLED_PAGES) return;

    fb_offset     = (uint16_t)row * OLED_WIDTH;
    base_col_start = (uint8_t)(col * (OLED_CHAR_W + OLED_CHAR_GAP));
    base_col       = base_col_start;

    while (*str)
    {
        /* 特殊: '~' 画"度"符号 ° (用于显示 ℃) */
        if (*str == '~')
        {
            for (fcol = 0u; fcol < OLED_CHAR_W; fcol++)
            {
                if ((base_col + fcol) < OLED_WIDTH)
                {
                    s_fb[fb_offset + base_col + fcol] |= s_deg5x8[fcol];
                }
            }
        }
        /* 仅绘制可打印字符 (0x20~0x7F) */
        else if (*str >= 0x20u && *str <= 0x7Fu)
        {
            font = s_font5x8[(uint8_t)(*str - 0x20u)];
            for (fcol = 0u; fcol < OLED_CHAR_W; fcol++)
            {
                if ((base_col + fcol) < OLED_WIDTH)
                {
                    s_fb[fb_offset + base_col + fcol] |= font[fcol];
                }
            }
        }
        /* 跳过一个字符 + 间距 = 6 像素 */
        base_col = (uint8_t)(base_col + OLED_CHAR_W + OLED_CHAR_GAP);
        str++;
    }
}

/**
 * @brief  2 倍放大绘制字符串（5x8 字体 -> 10x16/字符, 每字符占 12 像素列）
 * @param  page: 起始页号, 需为偶数(0/2/4/6), 字符占 page 与 page+1 两页
 * @param  col:  字符列(0 起), 每行最多 10 字符
 */
void BspOled_Puts2x(uint8_t page, uint8_t col, const char *str)
{
  uint16_t base_col;
  const uint8_t *font;
  uint8_t fcol, bit;

  if (str == NULL) return;
  if (page > (OLED_PAGES - 2u)) return;

  base_col = (uint16_t)col * 12u;

  while (*str)
  {
    if ((*str >= 0x20u) && (*str <= 0x7Fu))
    {
      font = s_font5x8[(uint8_t)(*str - 0x20u)];
      for (fcol = 0u; fcol < 5u; fcol++)
      {
        uint8_t v = font[fcol];
        if (v == 0u) continue;
        uint16_t cL = base_col + (uint16_t)fcol * 2u;   /* 目标左列 */
        if ((cL + 1u) >= OLED_WIDTH) break;
        for (bit = 0u; bit < 8u; bit++)
        {
          if (((v >> bit) & 0x01u) != 0u)
          {
            /* 2x 放大: 源行 bit -> 目标行 2*bit, 2*bit+1 */
            uint8_t dp = (uint8_t)(page + (bit >> 2u));          /* bit0-3->本页, bit4-7->下一页 */
            uint8_t db = (uint8_t)((2u * bit) % 8u);             /* 页内起始位 0/2/4/6 */
            uint8_t m  = (uint8_t)((1u << db) | (1u << (db + 1u)));
            s_fb[(uint16_t)dp * OLED_WIDTH + cL]      |= m;
            s_fb[(uint16_t)dp * OLED_WIDTH + cL + 1u] |= m;
          }
        }
      }
    }
    base_col += 12u;
    str++;
  }
}

/**
 * @brief  将整个帧缓冲刷新到 OLED 屏幕
 */
void BspOled_Flush(void)
{
    uint8_t page;

    for (page = 0u; page < OLED_PAGES; page++)
    {
        /* 设置页地址 + 列地址 0 */
        oled_set_pos(page, 0u);
        /* 发送该页 128 字节数据 */
        oled_send_data(&s_fb[(uint16_t)page * OLED_WIDTH], OLED_WIDTH);
    }
}

/**
 * @brief  在指定页填充列范围 (用于进度条等)
 * @param  row:  页号 (0~7)
 * @param  from: 起始像素列 (0~127)
 * @param  to:   结束像素列 (0~127, 包含)
 * @param  on:   1=填充像素, 0=清除像素
 */
void BspOled_FillRow(uint8_t row, uint8_t from, uint8_t to, uint8_t on)
{
    uint16_t base;
    uint8_t  col;
    uint8_t  fill = on ? 0xFFu : 0x00u;

    if (row >= OLED_PAGES) return;
    if (from > to) return;

    base = (uint16_t)row * OLED_WIDTH;

    for (col = from; col <= to && col < OLED_WIDTH; col++)
    {
        s_fb[base + col] = fill;
    }
}