#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "esp_http_client.h"
#include "esp_websocket_client.h"

/* Exported types ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
esp_err_t default_http_event_cb(esp_http_client_event_t* evt);
esp_err_t json_http_event_cb(esp_http_client_event_t* evt);
esp_err_t playlist_http_event_cb(esp_http_client_event_t *evt);
void default_ws_event_cb(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

#ifdef __cplusplus
}
#endif