#ifndef __AUDIO_ENCODER_H
#define __AUDIO_ENCODER_H
#include "Com_Debug.h"

#include "esp_audio_enc_default.h"
#include "esp_audio_enc.h"
#include "esp_opus_enc.h"

#include "freertos/ringbuf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct
{
    bool is_running;

    RingbufHandle_t inBuffer;
    RingbufHandle_t outBuffer;

} my_encoder_t;

extern my_encoder_t my_encoder;

void audio_encoder_init(void);
void audio_encoder_start(RingbufHandle_t in, RingbufHandle_t out);

#endif
