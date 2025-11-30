#include <stdio.h>
#include "bsp_wifi.h"
#include "commu_ota.h"
#include "audio_sr.h"
#include "bsp_sound.h"
#include "audio_encoder.h"
#include "audio_decoder.h"
#include "commu_ws.h"
#include "display.h"
#include "bsp_ws2812.h"

#define APP_HELLO_BIT (1 << 0)
typedef struct
{
    RingbufHandle_t sr_2_encoder;
    RingbufHandle_t encoder_2_ws;
    RingbufHandle_t ws_2_decoder;

    EventGroupHandle_t eg;
} app_t;

typedef enum
{
    APP_STATE_IDLE = 0,
    APP_STATE_SPEAKING,
    APP_STATE_LISTENING
} app_state_t;

static app_state_t state = APP_STATE_IDLE;

static app_t my_app;

void wifi_succ_cb(void);
void wifi_fail_cb(void);

void wake_cb(void);
void vad_change_cb(vad_state_t);
void recv_audio_cb(char *, int);
void recv_tts_cb(tts_state_t, char *);
void recv_stt_cb(char *);
void recv_llm_cb(char *, char *);
void hello_cb(void);
void ws_close_cb(void);

void audioUplod(void *);
void app_main(void)
{
    my_app.eg = xEventGroupCreate();

    // 0 显示屏幕
    display_init();
    bsp_ws2812_init();

    // 如果激活成功,才向下执行, 否则,就应该等待
    // 1. 初始化es8311
    bsp_sound_init();
    bsp_sound_open();

    // 2. 初始化wifi和启动wifi
    bsp_wifi_init();
    bsp_wifi_start(wifi_succ_cb, wifi_fail_cb);
}

void create_buffers(void)
{
    // 创建缓冲区:
    // sr_2_encoder: 类型必须是RINGBUF_TYPE_BYTEBUF
    // my_app.sr_2_encoder = xRingbufferCreate(1024 * 16, RINGBUF_TYPE_BYTEBUF);
    my_app.sr_2_encoder = xRingbufferCreateWithCaps(1024 * 16, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    my_app.encoder_2_ws = xRingbufferCreateWithCaps(1024 * 16, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
    my_app.ws_2_decoder = xRingbufferCreateWithCaps(1024 * 16, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
}

void test(void *arg)
{
    while(1)
    {
        // 1. 读取数据
        size_t   size = 0;
        uint8_t *data = (uint8_t *)xRingbufferReceive(my_app.encoder_2_ws, &size, portMAX_DELAY);
        if(data && size > 0)
        {
            xRingbufferSend(my_app.ws_2_decoder, data, size, portMAX_DELAY);
            vRingbufferReturnItem(my_app.encoder_2_ws, data);
        }
    }
}

void wifi_succ_cb(void)
{
    MY_LOGI("wifi连接成功");
    // 1. ota
    commu_ota_version_check();

    // 2. 创建需要的缓冲区
    create_buffers();

    // 3. 初始化和启动sr
    audio_sr_init();
    audio_sr_start(my_app.sr_2_encoder, wake_cb, vad_change_cb);

    // 4. 初始化编码器和启动编码器
    audio_encoder_init();
    audio_encoder_start(my_app.sr_2_encoder, my_app.encoder_2_ws);

    // 5. 初始化解码器和启动解码器
    audio_decoder_init();
    audio_decoder_start(my_app.ws_2_decoder);

    // 启动测试任务
    // xTaskCreate(test, "test", 4096, NULL, 5, NULL);

    // 6. 初始化websocket
    commu_ws_init();
    commu_ws_register_helo_cb(hello_cb);
    commu_ws_register_recv_audio_cb(recv_audio_cb);
    commu_ws_register_recv_stt_cb(recv_stt_cb);
    commu_ws_register_recv_tts_cb(recv_tts_cb);
    commu_ws_register_recv_llm_cb(recv_llm_cb);
    commu_ws_register_on_close_cb(ws_close_cb);

    // 7. 创建一个任务, 专门负责上传音频数据
    xTaskCreateWithCaps(audioUplod, "audioUplod", 4096, NULL, 5, NULL, MALLOC_CAP_SPIRAM);

    display_show_useage();
}
void wifi_fail_cb(void)
{
    MY_LOGI("wifi连接失败");
}

void wake_cb(void)
{
    MY_LOGI("唤醒");
    switch(state)
    {
        case APP_STATE_IDLE:
            if(!my_ws.is_connected)
            {
                // 建立ws连接
                commu_ws_open_audio_channel();
                // 发送hello
                commu_ws_send_hello();
                // 等待hello响应
                xEventGroupWaitBits(my_app.eg, APP_HELLO_BIT, true, true, portMAX_DELAY);
                MY_LOGI("hello成功");
            }
            break;

        case APP_STATE_SPEAKING:
            // 放弃当前对话
            commu_ws_send_abort();
            break;

        default:
            break;
    }
    // 发送唤醒词之前, 是空闲
    state = APP_STATE_IDLE;
    // 发送唤醒词
    commu_ws_send_wake();
}
void vad_change_cb(vad_state_t vad_state)
{
    MY_LOGI("%s", vad_state == VAD_SPEECH ? "说话" : "静音");
    if(vad_state == VAD_SPEECH && state != APP_STATE_SPEAKING)
    {
        // 开始监听
        state = APP_STATE_LISTENING;
        // 发送监听信息
        commu_ws_send_start_listen();
    }
    else if(vad_state == VAD_SILENCE && state == APP_STATE_LISTENING)
    {
        state = APP_STATE_IDLE;
        // 发送停止监听信息
        commu_ws_send_stop_listen();
    }
}

void hello_cb(void)
{
    xEventGroupSetBits(my_app.eg, APP_HELLO_BIT);
    commu_ws_send_iot_descriptor();
    commu_ws_send_iot_state();
}

void recv_audio_cb(char *data, int len)
{
    xRingbufferSend(my_app.ws_2_decoder, data, len, portMAX_DELAY);
}

void recv_tts_cb(tts_state_t tts_state, char *ttsText)
{
    if(tts_state == TTS_START)
    {
        // MY_LOGI("start");
    }
    else if(tts_state == TTS_STOP)
    {
        // MY_LOGI("stop");
    }
    else if(tts_state == TTS_SENTENCE_START)
    {
        state = APP_STATE_SPEAKING;
        // MY_LOGI("sentce start: %s", ttsText);
        //  在屏幕显示文本内容
        display_show_tts_stt(ttsText);
    }
    else if(tts_state == TTS_SENTENCE_END)
    {
        // MY_LOGI("sentce stop: %s", ttsText);
        state = APP_STATE_IDLE;
    }
}
void recv_stt_cb(char *sttText)
{
    // MY_LOGI("stt: %s", sttText);
    //  TODO 在屏幕显示文本内容
    display_show_tts_stt(sttText);
}
void recv_llm_cb(char *llmText, char *llmEmotion)
{
    // MY_LOGI("llm: %s, %s", llmText, llmEmotion);
    //  在屏幕显示表情包内容
    display_show_emoji(llmEmotion);
}

void ws_close_cb(void)
{
    MY_LOGI("ws关闭");
    state         = APP_STATE_IDLE;
    my_sr.is_wake = false;
}

void audioUplod(void *args)
{
    while(1)
    {
        size_t size = 0;
        void  *data = xRingbufferReceive(my_app.encoder_2_ws, &size, portMAX_DELAY);
        if(state == APP_STATE_LISTENING && data && size > 0)
        {
            commu_ws_send_audio(data, size);
        }
        vRingbufferReturnItem(my_app.encoder_2_ws, data);
    }
}
