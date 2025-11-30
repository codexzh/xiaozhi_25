#include "audio_decoder.h"

my_decoder_t my_decoder;

esp_audio_dec_handle_t decoder;
void                   audio_decoder_init(void)
{
    // 使用通用api
    // 1. 注册opus解码器
    esp_opus_dec_register();

    esp_opus_dec_cfg_t opus_cfg = {
        .sample_rate    = 16000,
        .channel        = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false};

    esp_audio_dec_cfg_t dec_cfg = {
        .type   = ESP_AUDIO_TYPE_OPUS,
        .cfg    = &opus_cfg,
        .cfg_sz = sizeof(opus_cfg),
    };

    esp_audio_dec_open(&dec_cfg, &decoder);
}

void audio_decoder_task(void *arg);
void audio_decoder_start(RingbufHandle_t in_buffer)
{

    my_decoder.is_running = true;
    my_decoder.in_buffer  = in_buffer;

    xTaskCreatePinnedToCoreWithCaps(audio_decoder_task,
                                    "audio_decoder_task",
                                    64 * 1024,
                                    NULL,
                                    5,
                                    NULL,
                                    0,
                                    MALLOC_CAP_SPIRAM);
}

void audio_decoder_task(void *arg)
{

    esp_audio_dec_in_raw_t in_frame = {0};

    uint8_t *out_data = (uint8_t *)heap_caps_malloc(8 * 1024, MALLOC_CAP_SPIRAM);

    esp_audio_dec_out_frame_t out_frame = {
        .buffer = out_data,
        .len    = 16 * 1024,
    };

    while(my_decoder.is_running)
    {

        in_frame.buffer  = xRingbufferReceive(my_decoder.in_buffer, &in_frame.len, portMAX_DELAY);
        void *raw_buffer = in_frame.buffer;
        // 从环形缓冲区读取的音频的数据,可能是多帧, 但是一次只能解码一帧
        while(in_frame.len > 0)
        {
            esp_audio_dec_process(decoder, &in_frame, &out_frame);
            in_frame.len -= in_frame.consumed;   // 更新剩余的未解码的数据的长度
            in_frame.buffer += in_frame.consumed;

            // 播放解码后的数据
            bsp_sound_write(out_frame.buffer, out_frame.decoded_size);
        }
        vRingbufferReturnItem(my_decoder.in_buffer, raw_buffer);
    }
    free(out_data);
    vTaskDelete(NULL);
}
