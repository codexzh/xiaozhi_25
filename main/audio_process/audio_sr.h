#ifndef __AUDIO_SR_H
#define __AUDIO_SR_H
#include "Com_Debug.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_aec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_sound.h"
#include "freertos/ringbuf.h"

typedef struct
{
    bool        is_running;
    bool        is_wake;
    vad_state_t last_vad_state;

    RingbufHandle_t ringBuf;
    void (*wake_cb)(void);
    void (*vad_change_cb)(vad_state_t);
} sr_t;

extern sr_t my_sr;

void audio_sr_init(void);
void audio_sr_start(RingbufHandle_t ringBuf,
                    void (*wake_bc)(void),
                    void (*vad_change_cb)(vad_state_t));

#endif
