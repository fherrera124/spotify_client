#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <time.h>

#include "spotify_client.h"
#include "spotify_client_priv.h"

/* Exported types ------------------------------------------------------------*/
typedef struct {
    char*   items_string;
    StrList values;
} u8g2_items_list_t;

/* Globally scoped variables declarations ------------------------------------*/
extern u8g2_items_list_t PLAYLISTS;
extern u8g2_items_list_t DEVICES;

/* Exported functions prototypes ---------------------------------------------*/
void      init_functions_cb(void);
void      parseTrackInfo(const char* js, TrackInfo* track);
void      parseAccessToken(const char* js, AccessToken* token);
void      parse_playlist(const char* js);
esp_err_t parse_available_devices(const char* js);
uint32_t  parseWebsocketEvent(const char* js, char** str);

void parseConnectionId(const char* js, char** str);

#ifdef __cplusplus
}
#endif