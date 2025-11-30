#include "display.h"
#include "font_emoji.h"
#include "lv_examples.h"

// 引入 LEDC (PWM) 驱动头文件
#include "driver/ledc.h"

#define LCD_H_RES (240)
#define LCD_V_RES (320)

#define LCD_DRAW_BUFF_HEIGHT 50
#define LCD_SPI_NUM (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
// #define LCD_DRAW_BUFF_HEIGHT (50) // 此宏定义重复，可以注释掉一个
#define LCD_BL_ON_LEVEL (1)

/* LCD pins */
#define LCD_GPIO_SCLK (GPIO_NUM_47)
#define LCD_GPIO_MOSI (GPIO_NUM_48)
#define LCD_GPIO_RST (GPIO_NUM_16)
#define LCD_GPIO_DC (GPIO_NUM_45)
#define LCD_GPIO_CS (GPIO_NUM_21)
#define LCD_GPIO_BL (GPIO_NUM_40)

// --- 新增: PWM 配置宏 ---
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // PWM占空比分辨率，10位 (0-1023)
#define LEDC_FREQUENCY          (5000) // PWM 频率 5 kHz

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io    = NULL;
static esp_lcd_panel_handle_t    lcd_panel = NULL;

/* LVGL display */
static lv_display_t *lvgl_disp = NULL;

lv_obj_t *label1;
lv_obj_t *label2;

// 声明中文字体
LV_FONT_DECLARE(font_puhui_14_1);

// --- 修改: display_lcd_init 函数 ---
void display_lcd_init(void)
{
    // 1. 配置背光为 PWM 模式
    // 准备并配置 LEDC PWM 定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 准备并配置 LEDC PWM 通道
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_GPIO_BL,
        .duty           = 0, // 初始占空比为 0
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 2. SPI 总线初始化 (这部分代码保持不变)
    const spi_bus_config_t buscfg = {
        .sclk_io_num     = LCD_GPIO_SCLK,
        .mosi_io_num     = LCD_GPIO_MOSI,
        .miso_io_num     = GPIO_NUM_NC,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);

    // 3. LCD IO 配置 (这部分代码保持不变)
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = LCD_GPIO_DC,
        .cs_gpio_num       = LCD_GPIO_CS,
        .pclk_hz           = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits      = LCD_CMD_BITS,
        .lcd_param_bits    = LCD_PARAM_BITS,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io);

    // 4. LCD 面板配置 (这部分代码保持不变)
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel);

    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_mirror(lcd_panel, true, true);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* 5. 设置初始亮度 (不再使用 gpio_set_level) */
    // 这里会调用下面修改过的 SetBrightness 函数
}


void display_lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority     = 4,    /* LVGL task priority */
        .task_stack        = 4096, /* LVGL task stack size */
        .task_affinity     = -1,   /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,  /* Maximum sleep in LVGL task */
        .timer_period_ms   = 5     /* LVGL timer tick period in ms */
    };
    lvgl_port_init(&lvgl_cfg);

    /* Add LCD screen */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd_io,
        .panel_handle  = lcd_panel,
        .buffer_size   = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,   // 是否单色显示
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation      = {
                 .swap_xy  = false,
                 .mirror_x = true,
                 .mirror_y = true,
        },
        .flags = {
            .buff_dma    = true,
            .swap_bytes  = true,   // 字节序的调整
            .buff_spiram = true    // 是否使用spiram

        }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // 显示一个标签
    lv_obj_t *scr = lv_scr_act();

    lvgl_port_lock(0);

    label1 = lv_label_create(scr);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP); /*Break the long lines*/
    lv_label_set_text(label1, "智能语音聊天机器人");
    lv_obj_set_width(label1, LCD_H_RES); /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_text_font(label1, &font_puhui_14_1, 0);   // 设置字体

    label2 = lv_label_create(scr);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_WRAP); /*Break the long lines*/
    lv_label_set_text(label2, "😴");
    lv_obj_set_width(label2, LCD_H_RES); /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(label2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(label2, font_emoji_64_init(), 0);   // 设置字体

    lvgl_port_unlock();
}

static int s_brightness= 60; // 默认亮度 60%
void display_init(void)
{
    display_lcd_init();
    display_lvgl_init();
    // 初始化完成后，设置默认亮度
    SetBrightness(s_brightness);
}

void display_show_useage(void)
{
    lvgl_port_lock(0);
    lv_label_set_text(label1, "说:\"你好,小智\"来唤醒机器人聊天");
    lvgl_port_unlock();
}

void display_show_tts_stt(char *txt)
{
    lvgl_port_lock(0);
    lv_label_set_text(label1, txt);
    lvgl_port_unlock();
}

typedef struct
{
    char *emotion;
    char *text;

} emoji_t;

static emoji_t emoji_listp[] = {
    {"neutral", "😶"},
    {"happy", "🙂"},
    {"laughing", "😆"},
    {"funny", "😂"},
    {"sad", "😔"},
    {"angy", " 😠"},
    {"crying", "😭"},
    {"loving", "😍"},
    {"embarrassed", "😳"},
    {"surprised", "😯"},
    {"shocked", "😱"},
    {"thinking", "🤔"},
    {"winking", "😉"},
    {"cool", "😎"},
    {"relaxed", "😌"},
    {"delicious", "🤤"},
    {"kissy", "😘"},
    {"confident", "😏"},
    {"sleepy", "😴"},
    {"silly", "😜"},
    {"confused", "🙄"}};

void display_show_emoji(char *emotion)
{
    char *txt = "🤔";
    for(int i = 0; i < sizeof(emoji_listp) / sizeof(emoji_listp[0]); i++)
    {
        if(strcmp(emoji_listp[i].emotion, emotion) == 0)
        {
            txt = emoji_listp[i].text;
            break;
        }
    }

    lvgl_port_lock(0);
    lv_label_set_text(label2, txt);
    lvgl_port_unlock();
}

lv_obj_t *qr;
void      display_show_qr(char *content)
{
    if(qr) return;
    lvgl_port_lock(0);
    lv_color_t bg_color = lv_color_white();
    lv_color_t fg_color = lv_color_black();

    qr = lv_qrcode_create(lv_screen_active());
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, fg_color);
    lv_qrcode_set_light_color(qr, bg_color);

    /*Set data*/
    lv_qrcode_update(qr, content, strlen(content));
    lv_obj_center(qr);

    /*Add a border with bg_color*/
    lv_obj_set_style_border_color(qr, bg_color, 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    lvgl_port_unlock();
}

void display_del_qr(void)
{
    if(qr)
    {
        lvgl_port_lock(0);
        lv_obj_del(qr);
        lvgl_port_unlock();
        qr = NULL;
    }
}


// --- 修改: SetBrightness 函数 ---
void SetBrightness(int brightness)
{
    // 对亮度值进行范围限制 (0-100)
    if (brightness > 100) {
        brightness = 100;
    }
    if (brightness < 0) {
        brightness = 0;
    }
    
    s_brightness = brightness;

    // 将 0-100 的亮度百分比映射到 PWM 的占空比值 (0-1023)
    // (1 << LEDC_DUTY_RES) - 1 相当于 2^10 - 1 = 1023
    uint32_t duty = (uint32_t)((s_brightness / 100.0f) * ((1 << LEDC_DUTY_RES) - 1));
    
    // 设置 LEDC 通道的占空比
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    // 更新占空比，使其生效
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}