#ifndef __COMMU_WS_H
#define __COMMU_WS_H
#include "Com_Debug.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "iot.h"
#include "bsp_sound.h"
#include "bsp_ws2812.h"
#include "display.h"

typedef enum
{
    TTS_START,
    TTS_SENTENCE_START,
    TTS_SENTENCE_END,
    TTS_STOP
} tts_state_t;

typedef struct
{
    bool is_connected;

    EventGroupHandle_t eg;

    void (*hello_cb)(void);
    void (*recv_audio_cb)(char *, int);
    void (*recv_tts_cb)(tts_state_t, char *);
    void (*recv_stt_cb)(char *);
    void (*recv_llm_cb)(char *, char *);
    void (*recv_iot_cb)(char *, void *);

    void (*on_close_cb)(void);

} ws_t;
extern ws_t my_ws;

void commu_ws_init(void);
void commu_ws_open_audio_channel(void);
void commu_ws_send_audio(char *data, int len);
void commu_ws_send_hello(void);
void commu_ws_send_wake(void);
void commu_ws_send_start_listen(void);
void commu_ws_send_stop_listen(void);
void commu_ws_send_listen(void);
void commu_ws_send_abort(void);
void commu_ws_send_iot_descriptor(void);
void commu_ws_send_iot_state(void);
void commu_ws_register_helo_cb(void (*hello_cb)(void));
void commu_ws_register_recv_audio_cb(void (*recv_audio_cb)(char *, int));
void commu_ws_register_recv_tts_cb(void (*recv_tts_cb)(tts_state_t, char *));
void commu_ws_register_recv_llm_cb(void (*recv_llm_cb)(char *, char *));
void commu_ws_register_recv_stt_cb(void (*recv_stt_cb)(char *));
void commu_ws_register_on_close_cb(void (*on_close_cb)(void));
#endif
