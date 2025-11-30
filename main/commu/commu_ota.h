#ifndef __COMMU_OTA_H
#define __COMMU_OTA_H
#include "Com_Debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "freertos/event_groups.h"

typedef struct{
    char * deviceId;
    char * clientId;

    char *wsURL;
    char *token;
    char *code;
} ota_t;

extern ota_t my_ota;

void commu_ota_version_check(void);
#endif
