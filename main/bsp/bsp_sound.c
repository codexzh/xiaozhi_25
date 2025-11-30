#include "bsp_sound.h"

#define SCL_PIN GPIO_NUM_1
#define SDA_PIN GPIO_NUM_0

#define I2S_MCK_PIN 3
#define I2S_BCK_PIN 2
#define I2S_DATA_WS_PIN 5
#define I2S_DATA_OUT_PIN 6
#define I2S_DATA_IN_PIN 4

#define PA_GPIO 7

// i2c总线句柄
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

i2s_chan_handle_t micHandle     = NULL;
i2s_chan_handle_t speakerHandle = NULL;

esp_codec_dev_handle_t codec_dev = NULL;
void                   bsp_sound_i2c_init(void)
{
    // i2c总线配置
    i2c_master_bus_config_t i2c_bus_config      = {0};
    i2c_bus_config.clk_source                   = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port                     = I2C_NUM_0;
    i2c_bus_config.scl_io_num                   = SCL_PIN;
    i2c_bus_config.sda_io_num                   = SDA_PIN;
    i2c_bus_config.glitch_ignore_cnt            = 7;      /* 对sda和scl上的信号做滤波 ,默认7*/
    i2c_bus_config.flags.enable_internal_pullup = true;   // 开启内部上拉
    // 创建i2c总线
    i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
}

void bsp_sound_i2s_init(void)
{
    // 通道配置
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),                             // 16000KHZ 采样率
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),   // 采样深度:16,  单声道
        .gpio_cfg = {
            .mclk = I2S_MCK_PIN,
            .bclk = I2S_BCK_PIN,
            .ws   = I2S_DATA_WS_PIN,
            .dout = I2S_DATA_OUT_PIN,
            .din  = I2S_DATA_IN_PIN,
        },
    };
    // 创建通道
    i2s_new_channel(&chan_cfg,
                    &speakerHandle,
                    &micHandle);
    // 初始化通道
    i2s_channel_init_std_mode(speakerHandle, &std_cfg);
    i2s_channel_init_std_mode(micHandle, &std_cfg);

    // 通道使能
    i2s_channel_enable(speakerHandle);
    i2s_channel_enable(micHandle);
}

void bsp_sound_es8311_init(void)
{
    // i2s配置
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = micHandle,
        .tx_handle = speakerHandle,
    };
    // 数据接口
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    // i2配置
    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr       = ES8311_CODEC_DEFAULT_ADDR,
        .port       = I2C_NUM_0,
        .bus_handle = i2c_bus_handle};
    // 控制接口
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    // gpio接口
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,   // 编码器模式:  播放和录音
        .ctrl_if    = out_ctrl_if,
        .gpio_if    = gpio_if,
        .pa_pin     = PA_GPIO,
        .use_mclk   = true,   // 使用主时钟
    };
    // 创建编码器接口
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);

    // 编解码器设备配置
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,               // codec interface from es8311_codec_new
        .data_if  = data_if,                    // data interface from audio_codec_new_i2s_data
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT   // codec support both playback and record
    };
    codec_dev = esp_codec_dev_new(&dev_cfg);

    // 设置音量
    esp_codec_dev_set_out_vol(codec_dev, 30.0);
    // 设置输入增益
    esp_codec_dev_set_in_gain(codec_dev, 10.0);
}

void bsp_sound_init(void)
{
    // 1. i2c的初始化
    bsp_sound_i2c_init();
    // 2. i2s的初始化
    bsp_sound_i2s_init();

    // 3. es8311的初始化
    bsp_sound_es8311_init();
}

void bsp_sound_open(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = 16000,   // 16KHZ 采样率
        .channel         = 1,       // 单声道
        .bits_per_sample = 16,      // 16bit 采样深度
    };
    if(codec_dev)
    {
        esp_codec_dev_open(codec_dev, &fs);
    }
}

void bsp_sound_close(void)
{
    if(codec_dev)
    {
        esp_codec_dev_close(codec_dev);
    }
}

db_state_t bsp_sound_write(uint8_t *data, int size)
{
    if(codec_dev && data && size > 0)
    {
        return esp_codec_dev_write(codec_dev, data, size) == ESP_CODEC_DEV_OK ? DB_OK : DB_ERROR;
    }

    return DB_ERROR;
}

/**
 * @brief 读取麦克风的数据
 *
 * @param data  数据存储的缓冲区
 * @param size  读取的数据长度
 */
db_state_t bsp_sound_read(uint8_t data[], int size)
{
    if(codec_dev && data && size > 0)
    {
        return esp_codec_dev_read(codec_dev, data, size) == ESP_CODEC_DEV_OK ? DB_OK : DB_ERROR;
    }
    return DB_ERROR;
}

static int s_volume = 60;
void SetVolume(int volume)
{
    s_volume = volume;
    esp_codec_dev_set_out_vol(codec_dev, volume);
}

void SetMute(bool mute)
{
    if(mute)
    {
        esp_codec_dev_set_out_vol(codec_dev, 0);
    }
    else
    {
        esp_codec_dev_set_out_vol(codec_dev, s_volume);
    }
}
