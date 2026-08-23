/**
 * @file    bsp_flash.c
 * @brief   内部 Flash 编程/擦除（STM32G474, 支持单/双 bank）。
 *          注意: 本机 OPTR.DBANK=1(双 bank), 高半区扇区须按 bank2 + bank内页号擦除。
 */
#include "bsp.h"
#include "bsp_flash.h"

void BspFlash_Read(uint32_t addr, uint8_t *buf, uint32_t size)
{
  uint32_t i;
  for (i = 0u; i < size; i++)
  {
    buf[i] = *((uint8_t *)(addr + i));
  }
}

int BspFlash_ErasePage(uint32_t addr)
{
  FLASH_EraseInitTypeDef  er = {0};
  uint32_t                page_err = 0u;
  uint32_t                page;
  uint32_t                bank;
  uint32_t                bank_size;

  if ((addr < FLASH_BASE) || (addr >= (FLASH_BASE + 512u * 1024u)))
  {
    return -1;
  }

  /* G4 由 OPTR.DBANK 决定单/双 bank:
   *  - 单 bank: 整片 512KB 为 bank1, 页 0~255
   *  - 双 bank: 每 bank 256KB, 页号按 bank 内重新计算(0~127)
   * 旧实现固定 bank1+全片页号, 在双 bank 下对高半区(0x08040000+)擦除会失败。 */
  if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) != 0u)
  {
    bank_size = 256u * 1024u;
    if (addr < (FLASH_BASE + bank_size))
    {
      bank = FLASH_BANK_1;
      page = (addr - FLASH_BASE) / BSP_FLASH_PAGE_SIZE;
    }
    else
    {
      bank = FLASH_BANK_2;
      page = (addr - (FLASH_BASE + bank_size)) / BSP_FLASH_PAGE_SIZE;
    }
  }
  else
  {
    bank = FLASH_BANK_1;
    page = (addr - FLASH_BASE) / BSP_FLASH_PAGE_SIZE;
  }

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return -1;
  }

  er.TypeErase = FLASH_TYPEERASE_PAGES;
  er.Page     = page;                       /* 页索引（G4: 2KB/页, 按 bank 内计） */
  er.Banks     = bank;
  er.NbPages   = 1u;
  if (HAL_FLASHEx_Erase(&er, &page_err) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return -1;
  }
  HAL_FLASH_Lock();
  return (page_err == 0xFFFFFFFFu) ? 0 : -1;
}

int BspFlash_Program(uint32_t addr, const uint8_t *buf, uint32_t size)
{
  uint32_t i;
  uint64_t val;
  uint32_t n, aligned;

  if (((addr - FLASH_BASE) % 8u) != 0u)
  {
    return -1;
  }

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return -1;
  }

  /* G4 只支持 64 位(双字)编程; 末尾不足 8 字节以 0xFF 补齐 */
  aligned = (size + 7u) & ~((uint32_t)7u);
  n = aligned / 8u;

  for (i = 0u; i < n; i++)
  {
    uint8_t tmp[8];
    uint32_t j;
    for (j = 0u; j < 8u; j++)
    {
      uint32_t idx = i * 8u + j;
      tmp[j] = (idx < size) ? buf[idx] : 0xFFu;
    }
    val = 0u;
    for (j = 0u; j < 8u; j++)
    {
      val |= ((uint64_t)tmp[j]) << (j * 8u);
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i * 8u, val) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return -1;
    }
  }
  HAL_FLASH_Lock();
  return 0;
}