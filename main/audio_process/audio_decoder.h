#ifndef __AUDIO_DECODER_H
#define __AUDIO_DECODER_H
#include "Com_Debug.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "bsp_sound.h"

typedef struct
{
    bool is_running;
    RingbufHandle_t in_buffer

} my_decoder_t;

void audio_decoder_init(void);
void audio_decoder_start(RingbufHandle_t in_buffer);
#endif
