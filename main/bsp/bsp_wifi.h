#ifndef __BSP_WIFI_H
#define __BSP_WIFI_H
#include "Com_Debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

void bsp_wifi_init(void);
void bsp_wifi_start(void (*wifi_succ)(void), void (*wifi_fail)(void));
#endif
