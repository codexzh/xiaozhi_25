#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "Com_Debug.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"

void display_init(void);
void display_show_useage(void);
void display_show_tts_stt(char *txt);
void display_show_emoji(char *emoji);
void display_show_qr(char *content);
void display_del_qr(void);
void SetBrightness(int brightness);
// void display_show_llm(char *emoji);
#endif
