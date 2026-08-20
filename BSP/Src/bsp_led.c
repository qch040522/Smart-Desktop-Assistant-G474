/**
 * @file    bsp_led.c
 */
#include "bsp.h"
#include "bsp_led.h"

#define LED_PIN  GPIO_PIN_4
#define LED_PORT GPIOB

void BspLed_Init(void)  { BspLed_Off(); }

void BspLed_On(void)
{
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

void BspLed_Off(void)
{
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

void BspLed_Toggle(void)
{
  HAL_GPIO_WritePin(LED_PORT, LED_PIN,
      (HAL_GPIO_ReadPin(LED_PORT, LED_PIN) == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}