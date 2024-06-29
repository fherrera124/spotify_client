#pragma once

#include "esp_http_client.h"

#include <portmacro.h>
#include "../lib/include/strlib.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/
typedef enum {
    ACTIVE_DEVICES_FOUND,
    NO_ACTIVE_DEVICES,
    LAST_DEVICE_FAILED,
    PLAYLISTS_ERROR,
    PLAYLISTS_OK,
    VOLUME_CHANGED
} Event_t;

typedef enum {
    ENABLE_PLAYER_EVENT,
    DISABLE_PLAYER_EVENT,
    DATA_PROCESSED_EVENT,
} SendEvent_t;

typedef struct
{
    char* id;
    bool  is_active;
    char* name;
    char* type;
    char  volume_percent[4];
} Device;

typedef struct
{
    char*   name;
    StrList artists;
    char*   album;
    time_t  duration_ms;
    time_t  progress_ms;
    bool    isPlaying : 1;
    Device  device;
} TrackInfo;

typedef struct {
    Event_t foo;
    union {
        TrackInfo trackInfo;
    };
} spotify_client_event_t;

typedef enum {
    PAUSE = 1,
    PLAY,
    PREVIOUS,
    NEXT,
    CHANGE_VOLUME,
    GET_STATE
} Player_cmd_t;

typedef enum {
    playBackTransferredSuccess = 17,
    playBackTransferredFailed,
    unknowEvent
} spotify_extra_events_t;



/* Exported functions prototypes ---------------------------------------------*/
esp_err_t       spotify_client_init(UBaseType_t priority);
esp_err_t       spotify_dispatch_event(SendEvent_t event);
void            waitForQueueEvent(spotify_client_event_t* event);
HttpStatus_Code player_cmd(Player_cmd_t event, void* payload);
HttpStatus_Code http_play_context_uri(const char* uri);
