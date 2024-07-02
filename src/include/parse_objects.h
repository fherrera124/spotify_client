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

/* Globally scoped variables declarations ------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
void     init_functions_cb(void);
void     parseTrackInfo(const char* js, TrackInfo* track);
void     parseAccessToken(const char* js, AccessToken* token);
void     parse_playlist(const char* js, PlaylistItem_t* playlist_item);
void     parse_available_devices(const char* js, List*);
void     parseConnectionId(const char* js, char** str);
uint32_t parseWebsocketEvent(const char* js, char** str);

#ifdef __cplusplus
}
#endif