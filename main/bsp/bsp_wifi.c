#include "bsp_wifi.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "qrcode.h"
#include "display.h"

static const char *TAG = "wifi";

#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_BLE "ble"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"

typedef struct
{
    EventGroupHandle_t eg;
    void (*wifi_succ)(void);
    void (*wifi_fail)(void);
} wifi_t;

static wifi_t my_wifi;

/* Event handler for catching system events */
static void event_handler(void            *arg,
                          esp_event_base_t event_base,
                          int32_t          event_id,
                          void            *event_data)
{

    static int retries;

    if(event_base == WIFI_PROV_EVENT)
    {
        switch(event_id)
        {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV:
            {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                              "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *)wifi_sta_cfg->ssid,
                         (const char *)wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL:
            {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                              "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");

                retries++;
                if(retries >= 5)
                {
                    ESP_LOGI(TAG, "Failed to connect with provisioned AP, resetting provisioned credentials");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    retries = 0;
                }
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                display_del_qr();

                retries = 0;

                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    }
    else if(event_base == WIFI_EVENT)
    {
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
                esp_wifi_connect();
                if(my_wifi.wifi_fail)
                {
                    my_wifi.wifi_fail();
                }
                break;
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

        // 不能阻塞事件
        xEventGroupSetBits(my_wifi.eg, 1 << 0);
        /* if(my_wifi.wifi_succ)
        {
            my_wifi.wifi_succ();
        } */
    }
    else if(event_base == PROTOCOMM_TRANSPORT_BLE_EVENT)
    {
        switch(event_id)
        {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(TAG, "BLE transport: Connected!");
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(TAG, "BLE transport: Disconnected!");
                break;
            default:
                break;
        }
    }
    else if(event_base == PROTOCOMM_SECURITY_SESSION_EVENT)
    {
        switch(event_id)
        {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
    }
}

void bsp_wifi_init(void)
{

    my_wifi.eg = xEventGroupCreate();
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize TCP/IP 初始化tcp/ip协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop 初始化事件循环*/
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 注册wifi配网事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme               = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t     eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if(inbuf)
    {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf         = (uint8_t *)strdup(response);
    if(*outbuf == NULL)
    {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

static void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport)
{
    if(!name || !transport)
    {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    char payload[150] = {0};

    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\""
                                       ",\"transport\":\"%s\"}",
             PROV_QR_VERSION,
             name,
             transport);

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);

    display_show_qr(payload);


    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, payload);
}

void bsp_wifi_start(void (*wifi_succ)(void), void (*wifi_fail)(void))
{
    my_wifi.wifi_succ = wifi_succ;
    my_wifi.wifi_fail = wifi_fail;
    // 表示是否已经配网成功
    bool provisioned = false;

    //wifi_prov_mgr_reset_provisioning();
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if(!provisioned)
    {
        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        const char *service_key = NULL;

        uint8_t custom_service_uuid[] = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4,
            0xdf,
            0x5a,
            0x1c,
            0x3f,
            0x6b,
            0xf4,
            0xbf,
            0xea,
            0x4a,
            0x82,
            0x03,
            0x04,
            0x90,
            0x1a,
            0x02,
        };
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        wifi_prov_mgr_endpoint_create("custom-data");

        // 启动配网服务
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0,
                                                         (const void *)NULL,
                                                         service_name,
                                                         service_key));
        wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

        wifi_prov_print_qr(service_name, NULL, NULL, PROV_TRANSPORT_BLE);
    }
    else
    {
        wifi_prov_mgr_deinit();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    // 等待wifi连接成功
    xEventGroupWaitBits(my_wifi.eg, 1 << 0, true, true, portMAX_DELAY);
    if(my_wifi.wifi_succ)
    {
        my_wifi.wifi_succ();
    }
}
