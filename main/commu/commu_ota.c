#include "commu_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "nvs.h"
#include "esp_app_desc.h"

#define TAG "OTA"
#define OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define ACTIVATE_BIT (1 << 0)
ota_t my_ota;

static EventGroupHandle_t eg;

void ota_task(void *);
void ota_set_request_header(esp_http_client_handle_t);
void ota_set_request_body(esp_http_client_handle_t);
bool is_activated(void);

void commu_ota_version_check(void)
{
    eg = xEventGroupCreate();
    // 创建任务发送ota的http请求
    xTaskCreate(ota_task, "ota_task", 1024 * 8, NULL, 5, NULL);

    xEventGroupWaitBits(eg, ACTIVATE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    MY_LOGI("设备已激活成功, 开始向下执行.....");
}

static char *output_buffer;   // Buffer to store response of http request from event handler
static int   output_len;      // Stores number of bytes read
esp_err_t    _http_event_handler(esp_http_client_event_t *evt)
{

    switch(evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            // 请求头发送成功回调
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // 响应头回调
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            if(strcmp(evt->header_key, "Content-Length") == 0)
            {
                if(output_buffer) free(output_buffer);   // 如果已经分配过空间,则先释放
                int len       = atoi(evt->header_value); // 把字符串数字转成数字  "123" => 123
                output_buffer = malloc(len + 1);
                output_len    = 0;   // 存储已经读取数据的长度
            }
            break;
        case HTTP_EVENT_ON_DATA:
            // 响应数据回调
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // MY_LOGI("%.*s", evt->data_len, (char *)evt->data);
            // 每次收到的数据copy到缓冲区
            memcpy(output_buffer + output_len, evt->data, evt->data_len);
            // 更新已经读取到的数据长度
            output_len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            // 数据传输完成回调
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");

            break;
        case HTTP_EVENT_DISCONNECTED:
            // 断开连接回调
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");

            break;
        case HTTP_EVENT_REDIRECT:
            // 重定向回调
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

void ota_task(void *args)
{

    esp_http_client_config_t config = {
        .url                   = OTA_URL,
        .event_handler         = _http_event_handler,
        .disable_auto_redirect = true,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .method                = HTTP_METHOD_POST};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 设置请求头
    ota_set_request_header(client);

    // 设置请求体
    ota_set_request_body(client);

    while(1)
    {
        esp_err_t err = esp_http_client_perform(client);

        if(err == ESP_OK)
        {
            MY_LOGI("HTTP POST request succeeded");
            // 数据已经接收完毕, 开始解析结果
            // MY_LOGI("%.*s", output_len, output_buffer);
            bool activated = is_activated();
            if(activated)
            {
                MY_LOGI("设备已激活");
                break;
            }
            else
            {
                MY_LOGI("设备未激活");
                MY_LOGI("%s, %s, %s", my_ota.wsURL, my_ota.token, my_ota.code);
            }
        }
        else
        {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(10000);
    }
    xEventGroupSetBits(eg, ACTIVATE_BIT);
    free(output_buffer);
    vTaskDelete(NULL);
}

char *get_device_id(void)
{
    uint8_t mac[6];
    // 获取MAC地址
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char *mac_str = malloc(18);
    sprintf(mac_str,
            "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0],
            mac[1],
            mac[2],
            mac[3],
            mac[4],
            mac[5]);
    return mac_str;
}

char *get_client_id(void)
{

    size_t len = 0;
    // 先从nvs中读取uuid
    nvs_handle_t handle;
    nvs_open("ota", NVS_READWRITE, &handle);
    nvs_get_str(handle, "uuid", NULL, &len);   // 读取字符串需要分两次: 第一次读取长度, 第二次读取真实数据
    if(len > 0)
    {
        char *uuid = malloc(len + 1);
        nvs_get_str(handle, "uuid", uuid, &len);
        nvs_close(handle);
        return uuid;
    }
    char         *uuid = malloc(37);   // 36 字符 + 1个 '\0'
    unsigned char bytes[16];
    int           i;

    // 初始化随机数种子（仅需一次）
    static int seeded = 0;
    if(!seeded)
    {
        srand((unsigned int)time(NULL) ^ (uintptr_t)&uuid);
        seeded = 1;
    }

    // 生成 16 个随机字节
    for(i = 0; i < 16; i++)
    {
        bytes[i] = (unsigned char)(rand() % 256);
    }

    // 设置 UUID v4 标识（version = 4, variant = 10xxxxxx）
    bytes[6] = (bytes[6] & 0x0F) | 0x40;   // 第7字节高4位为0100
    bytes[8] = (bytes[8] & 0x3F) | 0x80;   // 第9字节高2位为10

    // 格式化为字符串
    sprintf(uuid,
            "%02x%02x%02x%02x-"
            "%02x%02x-"
            "%02x%02x-"
            "%02x%02x-"
            "%02x%02x%02x%02x%02x%02x",
            bytes[0],
            bytes[1],
            bytes[2],
            bytes[3],
            bytes[4],
            bytes[5],
            bytes[6],
            bytes[7],
            bytes[8],
            bytes[9],
            bytes[10],
            bytes[11],
            bytes[12],
            bytes[13],
            bytes[14],
            bytes[15]);

    // 存储到nvs中
    nvs_set_str(handle, "uuid", uuid);
    nvs_commit(handle);
    nvs_close(handle);
    return uuid;
}

void ota_set_request_header(esp_http_client_handle_t client)
{
    esp_http_client_set_header(client, "User-Agent", "bread-compact-wifi-128x64/1.0.1");
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char *device_id = get_device_id();
    my_ota.deviceId = strdup(device_id);

    esp_http_client_set_header(client, "Device-Id", my_ota.deviceId);   // strdup: 深copy字符串

    char *client_id = get_client_id();
    my_ota.clientId = strdup(client_id);
    esp_http_client_set_header(client, "Client-Id", my_ota.clientId);

    MY_LOGI("Device-Id: %s", device_id);
    MY_LOGI("Client-Id: %s", client_id);

    free(device_id);
    free(client_id);
}

void ota_set_request_body(esp_http_client_handle_t client)
{
    cJSON *root = cJSON_CreateObject();

    cJSON *app = cJSON_CreateObject();
    cJSON_AddStringToObject(app, "version", "1.0.1");
    cJSON_AddStringToObject(app, "elf_sha256", esp_app_get_elf_sha256_str());

    cJSON_AddItemToObject(root, "application", app);

    cJSON *board = cJSON_CreateObject();
    cJSON_AddStringToObject(board, "type", "bread-compact-wifi");
    cJSON_AddStringToObject(board, "name", "bread-compact-wifi-128x64");
    cJSON_AddStringToObject(board, "ssid", "xiaozhi");
    cJSON_AddNumberToObject(board, "rssi", -55);
    cJSON_AddNumberToObject(board, "channel", 1);
    cJSON_AddStringToObject(board, "ip", "192.168.1.1");
    cJSON_AddStringToObject(board, "mac", my_ota.deviceId);

    cJSON_AddItemToObject(root, "board", board);

    char *body = cJSON_PrintUnformatted(root);

    MY_LOGI("Body: %s", body);

    // 设置请求体
    esp_http_client_set_post_field(client, body, strlen(body));

    cJSON_Delete(root);
}

bool is_activated(void)
{
    my_ota.code = NULL;

    cJSON *root = cJSON_ParseWithLength(output_buffer, output_len);

    cJSON *wsItem = cJSON_GetObjectItem(root, "websocket");
    my_ota.wsURL  = strdup(cJSON_GetObjectItem(wsItem, "url")->valuestring);
    my_ota.token  = strdup(cJSON_GetObjectItem(wsItem, "token")->valuestring);

    cJSON *activationItem = cJSON_GetObjectItem(root, "activation");
    if(activationItem)
    {
        my_ota.code = strdup(cJSON_GetObjectItem(activationItem, "code")->valuestring);
    }

    cJSON_Delete(root);
    return my_ota.code == NULL;   // 没有激活码, 表示激活成功
}
