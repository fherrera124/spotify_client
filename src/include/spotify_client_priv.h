/**
 * @file spotifyclient.h
 * @author Francisco Herrera (fherrera@lifia.info.unlp.edu.ar)
 * @brief
 * @version 0.1
 * @date 2022-11-06
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "strlib.h"

/* Exported macro ------------------------------------------------------------*/
#define MAX_HTTP_BUFFER     8192
#define WS_DATA_EVENT       (1 << 2)
#define WS_DISCONNECT_EVENT (1 << 3)
#define BUFFER_CONSUMED     (1 << 6)
#define START_WS            (1 << 7)
#define WS_CONNECT_EVENT    (1 << 8)

/* Exported types ------------------------------------------------------------*/

typedef enum {
    ACTIVE_DEVICES_FOUND,
    NO_ACTIVE_DEVICES,
    LAST_DEVICE_FAILED,
    PLAYLISTS_ERROR,
    PLAYLISTS_OK,
    VOLUME_CHANGED
} spotify_client_event_t;

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

typedef struct
{
    char   value[400];
    time_t expiresIn;
} AccessToken;

/* Exported variables declarations -------------------------------------------*/
/* extern TaskHandle_t PLAYER_TASK;
extern TrackInfo*   TRACK; */

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif