#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "esp_http_client.h"
#include "esp_websocket_client.h"

/* Exported types ------------------------------------------------------------*/

typedef struct {
    char* buffer;
    EventGroupHandle_t* event_group;
} handler_args_t;

/* Exported functions prototypes ---------------------------------------------*/
void init_functions_cb();
void default_http_event_handler(char* buff, esp_http_client_event_t* evt);
void default_ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
void playlists_handler(char* buff, esp_http_client_event_t* evt);

#ifdef __cplusplus
}
#endif