/**
 * @file    bsp_uart_cb.c
 * @brief   UART 中断接收回调分发（USART3->ESP32, UART5->TJC）。
 *          HAL_UART_RxCpltCallback 全局唯一，置于此文件。
 */
#include "bsp.h"
#include "bsp_uart_link.h"
#include "bsp_tjc.h"

/* 共享接收变量（BSP/App 可见，见 bsp.h） */
volatile uint8_t g_u3_rx_byte;
volatile uint8_t g_u5_rx_byte;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart3)
  {
    BspUartLink_RxByte(g_u3_rx_byte);
    /* 重新挂起单字节接收 */
    HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_u3_rx_byte, 1u);
  }
  else if (huart == &huart5)
  {
    BspTjc_RxByte(g_u5_rx_byte);
    HAL_UART_Receive_IT(&huart5, (uint8_t *)&g_u5_rx_byte, 1u);
  }
}

/* UART5(TJC) 发送完成: 非阻塞队列续发下一条 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart5)
  {
    BspTjc_TxComplete();
  }
}