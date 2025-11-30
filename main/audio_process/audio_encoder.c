#include "audio_encoder.h"

my_encoder_t my_encoder;

esp_audio_enc_handle_t encoder = NULL;
void                   audio_encoder_init(void)
{
    esp_opus_enc_config_t opus_cfg = {
        .sample_rate      = 16000,
        .channel          = 1,
        .bits_per_sample  = 16,
        .bitrate          = 32000,
        .frame_duration   = ESP_OPUS_ENC_FRAME_DURATION_60_MS,   // 每帧时长
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,      // 音频模式
        .complexity       = 5,
        .enable_fec       = false,
        .enable_vbr       = false,
        .enable_dtx       = false};

    esp_opus_enc_open(&opus_cfg, sizeof(esp_opus_enc_config_t), &encoder);
}

void audio_encoder_task(void *);
void audio_encoder_start(RingbufHandle_t in, RingbufHandle_t out)
{
    my_encoder.inBuffer   = in;
    my_encoder.outBuffer  = out;
    my_encoder.is_running = true;

    // 启动一个任务开始进行编码
    xTaskCreatePinnedToCoreWithCaps(
        audio_encoder_task,
        "audio_encoder_task",
        64 * 1024,
        NULL,
        5,
        NULL,
        1,   // cpu核心
        MALLOC_CAP_SPIRAM);
}

void copy_from_ringbuf(RingbufHandle_t ringbuff, uint8_t *to_buffer, int size)
{
    while(size > 0)
    {
        size_t   real_size = 0;
        void *data      = xRingbufferReceiveUpTo(ringbuff, &real_size, portMAX_DELAY, size);
        memcpy(to_buffer, data, real_size);
        vRingbufferReturnItem(ringbuff, data);   // 归还缓冲区
        size -= real_size;                       // 更新剩下待读取的数据长度
        to_buffer += real_size;                  // 移动指针
    }
}

void audio_encoder_task(void *args)
{
    // 每次进行编码的输入帧的大小和编码后输出的帧大小
    int in_size = 0, out_size = 0;
    esp_opus_enc_get_frame_size(encoder, &in_size, &out_size);
    uint8_t *in_data  = heap_caps_malloc(in_size, MALLOC_CAP_SPIRAM);   // 分配内存, 并指定内存位置:外部内存
    uint8_t *out_data = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM);

    esp_audio_enc_in_frame_t in_frame = {
        .buffer = in_data,
        .len    = in_size,
    };
    esp_audio_enc_out_frame_t out_frame = {
        .buffer = out_data,
        .len    = out_size,
    };
    while(my_encoder.is_running)
    {
        // 从in_buffer读取pcm数据, 然后存入到in_data, 读取: in_size 个字节
        copy_from_ringbuf(my_encoder.inBuffer, in_data, in_size);
        esp_opus_enc_process(encoder, &in_frame, &out_frame);

        // 存储编码后的数据到输出环形缓冲区
        xRingbufferSend(my_encoder.outBuffer, out_data, out_frame.encoded_bytes, 0);
    }

    free(in_data);
    free(out_data);

    vTaskDelete(NULL);
}
