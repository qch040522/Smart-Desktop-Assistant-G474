/**
 * @file    bsp_flash.h
 * @brief   STM32G474 内部 Flash 存储（低频配置/快照）。
 *          设计: 用户在保留地址段（末扇区）做"整扇区擦写 + 全量写回"。
 */
#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Flash 页大小（G474 数据扇区 = 2KB） */
#define BSP_FLASH_PAGE_SIZE    0x800u

/** 读：直接按地址读取 */
void BspFlash_Read(uint32_t addr, uint8_t *buf, uint32_t size);

/** 擦除一个扇区（addr 必须扇区对齐） */
int BspFlash_ErasePage(uint32_t addr);

/** 写（内部自动先解锁；调用前建议自行擦出过） */
int BspFlash_Program(uint32_t addr, const uint8_t *buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* BSP_FLASH_H */