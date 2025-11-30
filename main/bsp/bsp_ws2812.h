#ifndef __BSP_WS2812_H__
#define __BSP_WS2812_H__

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

#define LED_STRIP_USE_DMA 0

#define LED_STRIP_LED_COUNT 2
#define LED_STRIP_MEMORY_BLOCK_WORDS 0 
// GPIO引脚
#define LED_STRIP_GPIO_PIN 46

#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

void bsp_ws2812_init(void);

void bsp_ws2812_ledOn(void);

void bsp_ws2812_ledOff(void);

void SetLed(bool led);

#endif /* __INT_WS2812_H__ */
