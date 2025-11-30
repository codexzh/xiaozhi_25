#include "audio_sr.h"

sr_t my_sr = {
    .is_running = false};

esp_afe_sr_iface_t *afe_handle;
esp_afe_sr_data_t  *afe_data;
void                audio_sr_init(void)
{
    // 1. 初始化afe
    srmodel_list_t *models = esp_srmodel_init("model");   // 初始化sr模型
    // 参数1: 输入通道 参数2:sr模型 参数3:afe的类型,语音识别  参数4: afe的运行模式
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    // 唤醒相关配置
    afe_config->wakenet_init = true;
    afe_config->afe_mode     = DET_MODE_90;   // 唤醒模式: 值越大, 越容易被唤醒,也容易被误唤

    // vad相关的配置
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_2;   // 值越大, 越敏感

    // 其他配置
    afe_config->aec_init = false;   // 回声消除
    afe_config->ns_init  = false;   // 噪声抑制
    afe_config->se_init  = false;   // 语音增强

    // 2. 串讲afe实例
    // 获取句柄
    afe_handle = esp_afe_handle_from_config(afe_config);
    // 创建实例
    afe_data = afe_handle->create_from_config(afe_config);
}

void feed_task(void *);
void fetch_task(void *);
void audio_sr_start(RingbufHandle_t ringBuf,
                    void (*wake_cb)(void),
                    void (*vad_change_cb)(vad_state_t))
{

    my_sr.is_running     = true;
    my_sr.is_wake        = false;
    my_sr.last_vad_state = VAD_SILENCE;

    my_sr.ringBuf       = ringBuf;
    my_sr.vad_change_cb = vad_change_cb;
    my_sr.wake_cb       = wake_cb;

    // 创建两个任务:
    // 任务1:给模型喂数据
    xTaskCreate(feed_task, "feed_task", 4 * 1024, NULL, 5, NULL);

    // 任务2:从模型中获取结果
    xTaskCreate(fetch_task, "fetch_task", 4 * 1024, NULL, 5, NULL);
}

void feed_task(void *args)
{
    // 获取每次给模型喂的数据的大小(每次从es8311读取的音频的长度)
    int feed_chunksize = afe_handle->get_feed_chunksize(afe_data);
    // 获取通道数
    int feed_nch = afe_handle->get_feed_channel_num(afe_data);

    // 真正每次音频的长度
    int size = feed_chunksize * feed_nch * sizeof(int16_t);

    MY_LOGI("feed_task: feed_chunksize=%d, feed_nch=%d, size=%d",
            feed_chunksize,
            feed_nch,
            size);

    int16_t *feed_buff = (int16_t *)malloc(size);
    while(my_sr.is_running)
    {
        // 从es8311读取数据
        bsp_sound_read(feed_buff, size);

        // 喂给模型
        afe_handle->feed(afe_data, feed_buff);
    }

    free(feed_buff);
    vTaskDelete(NULL);
}
void fetch_task(void *args)
{

    while(my_sr.is_running)
    {
        // 获取识别结果
        afe_fetch_result_t *result = afe_handle->fetch(afe_data);
        // 识别后的音频数据
        int16_t *processed_audio = result->data;
        // vad状态
        vad_state_t vad_state = result->vad_state;
        // 唤醒状态
        wakenet_state_t wakeup_state = result->wakeup_state;

        if(wakeup_state == WAKENET_DETECTED)
        {
            my_sr.is_wake = true;
            // 识别到唤醒词
            if(my_sr.wake_cb)
            {
                my_sr.wake_cb();
            }
        }

        if(my_sr.is_wake)
        {
            // 当vad发生变化的时候, 调用回调函数
            if(vad_state != my_sr.last_vad_state)
            {
                my_sr.last_vad_state = vad_state;
                if(my_sr.vad_change_cb)
                {
                    my_sr.vad_change_cb(vad_state);
                }
            }
        }

        if(my_sr.is_wake && vad_state == VAD_SPEECH)
        {
            // if vad cache is exists, please attach the cache to the front of processed_audio to avoid data loss
            if(result->vad_cache_size > 0)
            {
                int16_t *vad_cache = result->vad_cache;

                if(my_sr.ringBuf)
                {
                    xRingbufferSend(my_sr.ringBuf, vad_cache, result->vad_cache_size, 0);
                }
            }

            // 唤醒中, 识别出语音
            // 添加到环形缓冲区
            if(my_sr.ringBuf)
            {
                xRingbufferSend(my_sr.ringBuf, processed_audio, result->data_size, 0);
            }
        }
    }

    vTaskDelete(NULL);
}
