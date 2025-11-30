#ifndef __BSP_SOUND_H
#define __BSP_SOUND_H
#include "Com_Debug.h"
#include "Com_Config.h"
// i2c相关
#include "driver/i2c_master.h"

// i2s相关
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"

// 编解码驱动相关
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

void bsp_sound_init(void);
void bsp_sound_open(void);
void bsp_sound_close(void);
db_state_t bsp_sound_write(uint8_t *data, int size);
db_state_t bsp_sound_read(uint8_t data[], int size);
void       SetVolume(int volume);
void       SetMute(bool mute);
#endif
