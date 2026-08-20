/**
 * @file    bsp_flash.c
 * @brief   内部 Flash 编程/擦除（STM32G474, FLASH 1 bank 512KB）。
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

  if ((addr < FLASH_BASE) || (addr >= (FLASH_BASE + 512u * 1024u)))
  {
    return -1;
  }
  page = (addr - FLASH_BASE) / BSP_FLASH_PAGE_SIZE;

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return -1;
  }

  er.TypeErase = FLASH_TYPEERASE_PAGES;
  er.Page     = page;                       /* 页索引（G4: 2KB/页） */
  er.Banks     = FLASH_BANK_1;
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