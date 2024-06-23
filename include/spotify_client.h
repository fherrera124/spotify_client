#include "esp_http_client.h"
#include <portmacro.h>

/* Exported macro ------------------------------------------------------------*/
#define ENABLE_PLAYER          (1 << 0)
#define DISABLE_PLAYER         (1 << 1)
#define DEVICE_STATE_CHANGED   (1 << 4)
#define PLAYER_STATE_CHANGED   (1 << 5)
#define PLAYER_FIRST_EVENT     (1 << 7)
#define NO_PLAYER_ACTIVE_EVENT (1 << 8)
#define ERROR_EVENT            (1 << 9)

/* Exported types ------------------------------------------------------------*/
typedef enum {
    PAUSE = 1,
    PLAY,
    PREVIOUS,
    NEXT,
    CHANGE_VOLUME,
    GET_STATE
} Player_cmd_t;

typedef enum {
    playerStateChanged = PLAYER_STATE_CHANGED,
    deviceStateChanged = DEVICE_STATE_CHANGED,
} spotify_base_events_t;

typedef enum {
    playBackTransferredSuccess = 17,
    playBackTransferredFailed,
    unknowEvent
} spotify_extra_events_t;

typedef union {
    spotify_base_events_t  j;
    spotify_extra_events_t k;
    uint32_t               value;

} full_events_t;

/* typedef struct
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
} TrackInfo; */

/* Exported functions prototypes ---------------------------------------------*/
esp_err_t spotify_client_init(UBaseType_t priority, EventGroupHandle_t* event_group_ptr);

HttpStatus_Code player_cmd(Player_cmd_t event, void* payload);

void http_play_context_uri(const char* uri);
