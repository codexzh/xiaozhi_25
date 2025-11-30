#include "commu_ws.h"
#include "commu_ota.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#define TAG "commu_ws"
#define WS_CONNECTED (1 << 0)

ws_t my_ws = {.is_connected = false};

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id)
    {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        my_ws.is_connected = false;
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        my_ws.is_connected = true;
        xEventGroupSetBits(my_ws.eg, WS_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        my_ws.is_connected = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        my_ws.is_connected = true;
        // ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        // ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x2)
        { // Opcode 0x2 indicates binary data
            // ESP_LOG_BUFFER_HEX("Received binary data", data->data_ptr, data->data_len);
            if (my_ws.recv_audio_cb)
            {
                my_ws.recv_audio_cb(data->data_ptr, data->data_len);
            }
        }
        else if (data->op_code == 0x08 && data->data_len == 2)
        {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        }
        else if (data->op_code == 0x01)
        {
            // 文本数据
            // ESP_LOGW(TAG, "Received=%.*s\n\n", data->data_len, (char *)data->data_ptr);
            cJSON *root = cJSON_ParseWithLength((char *)data->data_ptr, data->data_len);
            MY_LOGI("%.*s", data->data_len, (char *)data->data_ptr);

            cJSON *typeItem = cJSON_GetObjectItem(root, "type");
            if (typeItem)
            {
                char *type = cJSON_GetStringValue(typeItem);

                if (strcmp(type, "hello") == 0)
                {
                    // 调用收到hello的回调函数
                    if (my_ws.hello_cb)
                    {
                        my_ws.hello_cb();
                    }
                }
                else if (strcmp(type, "tts") == 0)
                {
                    cJSON *ttsStateItem = cJSON_GetObjectItem(root, "state");
                    char *ttsState = ttsStateItem->valuestring;

                    cJSON *ttsTextItem = cJSON_GetObjectItem(root, "text");

                    char *text = NULL;
                    if (ttsTextItem != NULL)
                    {
                        text = cJSON_GetStringValue(ttsTextItem);
                    }

                    if (my_ws.recv_tts_cb)
                    {
                        tts_state_t tts_state_enum = TTS_START;
                        if (strcmp(ttsState, "start") == 0)
                        {
                            tts_state_enum = TTS_START;
                        }
                        else if (strcmp(ttsState, "sentence_end") == 0)
                        {
                            tts_state_enum = TTS_SENTENCE_END;
                        }
                        else if (strcmp(ttsState, "sentence_start") == 0)
                        {
                            tts_state_enum = TTS_SENTENCE_START;
                        }
                        else if (strcmp(ttsState, "stop") == 0)
                        {
                            tts_state_enum = TTS_STOP;
                        }
                        my_ws.recv_tts_cb(tts_state_enum, text);
                    }
                }
                else if (strcmp(type, "stt") == 0)
                {
                    cJSON *ttsTextItem = cJSON_GetObjectItem(root, "text");
                    char *text = cJSON_GetStringValue(ttsTextItem);
                    if (my_ws.recv_stt_cb)
                    {
                        my_ws.recv_stt_cb(text);
                    }
                }
                else if (strcmp(type, "llm") == 0)
                {
                    char *text = cJSON_GetObjectItem(root, "text")->valuestring;
                    char *emotion = cJSON_GetObjectItem(root, "emotion")->valuestring;
                    if (my_ws.recv_llm_cb)
                    {
                        my_ws.recv_llm_cb(text, emotion);
                    }
                }
                else if (strcmp(type, "iot") == 0)
                {
                    cJSON *iotCommandsArray = cJSON_GetObjectItem(root, "commands");
                    cJSON *commandItem = cJSON_GetArrayItem(iotCommandsArray, 0);

                    char *method = cJSON_GetObjectItem(commandItem, "method")->valuestring;

                    if (strcmp(method, "SetVolume") == 0)
                    {
                        cJSON *params = cJSON_GetObjectItem(commandItem, "parameters");
                        int volume = cJSON_GetObjectItem(params, "volume")->valueint;

                        SetVolume(volume);
                    }
                    // TODO
                    else if (strcmp(method, "SetBrightness") == 0)
                    {
                        cJSON *params = cJSON_GetObjectItem(commandItem, "parameters");
                        int brightness = cJSON_GetObjectItem(params, "brightness")->valueint;

                        SetBrightness(brightness);
                    }
                    else if (strcmp(method, "SetMute") == 0)
                    {
                        cJSON *params = cJSON_GetObjectItem(commandItem, "parameters");
                        cJSON *muateItem = cJSON_GetObjectItem(params, "mute");
                        if (cJSON_IsTrue(muateItem))
                        {
                            SetMute(true);
                        }
                        else
                        {
                            SetMute(false);
                        }
                    }
                    else if (strcmp(method, "SetPower") == 0)
                    {
                        // TODO
                        cJSON *params = cJSON_GetObjectItem(commandItem, "parameters");
                        cJSON *ledItem = cJSON_GetObjectItem(params, "power");
                        if (cJSON_IsTrue(ledItem))
                        {
                            SetLed(true);
                        }
                        else
                        {
                            SetLed(false);
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }

        break;
    case WEBSOCKET_EVENT_FINISH:
        my_ws.is_connected = false;
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        if (my_ws.on_close_cb)
        {
            my_ws.on_close_cb();
        }
        break;
    }
}

esp_websocket_client_handle_t client;
void commu_ws_init(void)
{
    my_ws.eg = xEventGroupCreate();

    esp_websocket_client_config_t websocket_cfg = {
        .uri = my_ota.wsURL,
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2 * 1024,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 5000,
    };

    client = esp_websocket_client_init(&websocket_cfg);

    char *auth = NULL;
    asprintf(&auth, "Bearer %s", my_ota.token);
    esp_websocket_client_append_header(client, "Authorization", auth);
    esp_websocket_client_append_header(client, "Protocol-Version", "1");
    esp_websocket_client_append_header(client, "Device-Id", my_ota.deviceId);
    esp_websocket_client_append_header(client, "Client-Id", my_ota.clientId);

    free(auth);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
}

void commu_ws_open_audio_channel(void)
{
    if (client && !esp_websocket_client_is_connected(client))
    {
        esp_websocket_client_start(client);
        // 等待ws连接成功
        xEventGroupWaitBits(my_ws.eg, WS_CONNECTED, true, true, portMAX_DELAY);
    }
}

void commu_ws_send_text(char *text)
{
    if (client && esp_websocket_client_is_connected(client) && text)
    {
        esp_websocket_client_send_text(client, text, strlen(text), portMAX_DELAY);
    }
}

void commu_ws_send_bin(char *data, int len)
{
    if (client && esp_websocket_client_is_connected(client) && data && len > 0)
    {
        esp_websocket_client_send_bin(client, data, len, portMAX_DELAY);
    }
}

void commu_ws_send_audio(char *data, int len)
{
    commu_ws_send_bin(data, len);
}

void commu_ws_send_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON *features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);

    cJSON *audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", 60);

    cJSON_AddItemToObject(root, "audio_params", audio);

    commu_ws_send_text(cJSON_PrintUnformatted(root));

    cJSON_Delete(root);
}

void commu_ws_send_wake(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", "你好,小智");

    commu_ws_send_text(cJSON_PrintUnformatted(root));

    cJSON_Delete(root);
}

void commu_ws_send_start_listen(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", "manual");

    commu_ws_send_text(cJSON_PrintUnformatted(root));

    cJSON_Delete(root);
}

void commu_ws_send_stop_listen(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");

    commu_ws_send_text(cJSON_PrintUnformatted(root));

    cJSON_Delete(root);
}

void commu_ws_send_abort(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "abort");
    cJSON_AddStringToObject(root, "reason", "wake_word_detected");

    commu_ws_send_text(cJSON_PrintUnformatted(root));

    cJSON_Delete(root);
}
void commu_ws_send_iot_descriptor(void)
{
    commu_ws_send_text(iot_get_descriptor());
}

void commu_ws_send_iot_state(void)
{
    commu_ws_send_text(iot_get_state());
}

void commu_ws_register_helo_cb(void (*hello_cb)(void))
{
    my_ws.hello_cb = hello_cb;
}

void commu_ws_register_recv_audio_cb(void (*recv_audio_cb)(char *, int))
{
    my_ws.recv_audio_cb = recv_audio_cb;
}

void commu_ws_register_recv_tts_cb(void (*recv_tts_cb)(tts_state_t, char *))
{
    my_ws.recv_tts_cb = recv_tts_cb;
}

void commu_ws_register_recv_llm_cb(void (*recv_llm_cb)(char *, char *))
{
    my_ws.recv_llm_cb = recv_llm_cb;
}

void commu_ws_register_recv_stt_cb(void (*recv_stt_cb)(char *))
{
    my_ws.recv_stt_cb = recv_stt_cb;
}

void commu_ws_register_on_close_cb(void (*on_close_cb)(void))
{
    my_ws.on_close_cb = on_close_cb;
}

void commu_ws_register_recv_iot_cb(void (*recv_iot_cb)(char *, void *))
{
    my_ws.recv_iot_cb = recv_iot_cb;
}
