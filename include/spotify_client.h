#pragma once

#include "esp_http_client.h"
#include "spotify_utils.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/
typedef enum {
    PLAYER_STATE_CHANGED,
    SAME_TRACK, // for testing
    NEW_TRACK, // for testing
    DEVICE_STATE_CHANGED,
    ACTIVE_DEVICES_FOUND,
    NO_ACTIVE_DEVICES,
    LAST_DEVICE_FAILED,
    VOLUME_CHANGED,
    TRANSFERRED_OK,
    TRANSFERRED_FAIL,
    NO_PLAYER_ACTIVE,
    UNKNOW
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
    char   id[30];
    char*  name;
    List   artists;
    char*  album;
    time_t duration_ms;
    time_t progress_ms;
    bool   isPlaying;
    Device device;
} TrackInfo;

typedef struct {
    Event_t type;
    void*   payload;
} SpotifyClientEvent_t;

typedef enum {
    PAUSE = 1,
    PLAY,
    PREVIOUS,
    NEXT,
    CHANGE_VOLUME,
    GET_STATE
} PlayerCommand_t;

/* Exported functions prototypes ---------------------------------------------*/
esp_err_t  spotify_client_init(UBaseType_t priority);
esp_err_t  spotify_dispatch_event(SendEvent_t event);
BaseType_t spotify_wait_event(SpotifyClientEvent_t* event, TickType_t xTicksToWait);
esp_err_t  player_cmd(PlayerCommand_t cmd, void* payload, HttpStatus_Code* status_code);
esp_err_t  http_play_context_uri(const char* uri, HttpStatus_Code* status_code);
List*      spotify_user_playlists();
List*      spotify_available_devices();
void       spotify_clear_track(TrackInfo* track);
esp_err_t  spotify_clone_track(TrackInfo* dest, const TrackInfo* src);
