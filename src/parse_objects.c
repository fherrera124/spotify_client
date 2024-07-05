/* Includes ------------------------------------------------------------------*/
#include "parse_objects.h"
#include "esp_log.h"
#include "json_parser.h"
#include "spotify_client_priv.h"
#include <stdlib.h>
#include <string.h>

/* Private macro -------------------------------------------------------------*/
#define MAX_TOKENS 500

// early check of unrecoverable error
#define ERR_CHECK(x) ESP_ERROR_CHECK(x)

/* Private types -------------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Locally scoped variables --------------------------------------------------*/
static const char* TAG = "PARSE_OBJECT";
static json_tok_t  tokens[MAX_TOKENS];

/* Globally scoped variables definitions -------------------------------------*/

/* Exported functions --------------------------------------------------------*/
void parse_access_token(const char* js, char* access_token, int size)
{
    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));
    ERR_CHECK(json_obj_get_string(&jctx, "access_token", access_token, size));
    json_parse_end_static(&jctx);
}

void parse_available_devices(const char* js, List* devices_list)
{
    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));
    int num_elem;
    ERR_CHECK(json_obj_get_array(&jctx, "devices", &num_elem));
    for (int i = 0; i < num_elem; i++) {
        json_arr_get_object(&jctx, i);
        DeviceItem_t* item = malloc(sizeof(*item));
        assert(item);
        ERR_CHECK(json_obj_dup_string(&jctx, "name", &item->name));
        ERR_CHECK(json_obj_dup_string(&jctx, "id", &item->id));
        assert(spotify_append_item_to_list(devices_list, (void*)item));
        ERR_CHECK(json_arr_leave_object(&jctx));
    }
    json_parse_end_static(&jctx);
}

void parse_playlist(const char* js, PlaylistItem_t* playlist_item)
{
    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));
    ERR_CHECK(json_obj_dup_string(&jctx, "name", &playlist_item->name));
    ERR_CHECK(json_obj_dup_string(&jctx, "uri", &playlist_item->uri));
    json_parse_end_static(&jctx);
}

void parse_connection_id(const char* js, char** data)
{
    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));
    ERR_CHECK(json_obj_get_object(&jctx, "headers"));
    ERR_CHECK(json_obj_dup_string(&jctx, "Spotify-Connection-Id", data));
    json_parse_end_static(&jctx);
}

SpotifyClientEvent_t parse_ws_event(const char* js, TrackInfo** track)
{
    // ESP_LOGW(TAG, "%s", js);
    assert(track && *track);

    SpotifyClientEvent_t spotify_evt = { 0 };

    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));

    int num_elem;
    ERR_CHECK(json_obj_get_array(&jctx, "payloads", &num_elem));
    // TODO: check if the array is greater than one
    ERR_CHECK(json_arr_get_object(&jctx, 0));
    if (json_obj_get_array(&jctx, "events", &num_elem) != OS_SUCCESS) {
        ESP_LOGW(TAG, "Not an event:\n%s", js);
        spotify_evt.type = UNKNOW;
        return spotify_evt;
    }
    // TODO: check if the array is greater than one
    ERR_CHECK(json_arr_get_object(&jctx, 0));
    // json_tok_t* t = jctx.cur;
    // printf("token content: %.*s\n", (int)t->end - t->start, js + t->start);

    bool match;
    ERR_CHECK(json_obj_match_string(&jctx, "type", "DEVICE_STATE_CHANGED", &match));
    if (match) {
        spotify_evt.type = DEVICE_STATE_CHANGED;
        return spotify_evt;
    }
    ERR_CHECK(json_obj_match_string(&jctx, "type", "PLAYER_STATE_CHANGED", &match));
    if (match) {
        spotify_evt.type = PLAYER_STATE_CHANGED;
        ERR_CHECK(json_obj_get_object(&jctx, "event"));
        ERR_CHECK(json_obj_get_object(&jctx, "state"));
        ERR_CHECK(json_obj_get_object(&jctx, "item"));
        ERR_CHECK(json_obj_match_string(&jctx, "id", (*track)->id, &match));
        if (match) {
            spotify_evt.type = SAME_TRACK;
            // TODO: evaluar que informacion posiblemente cambió
            // y emitir ese evento en particular
        } else {
            spotify_evt.type = NEW_TRACK;
            spotify_evt.payload = *track;
            ERR_CHECK(json_obj_get_string(&jctx, "id", (*track)->id, 30));
            free((*track)->name);
            ERR_CHECK(json_obj_dup_string(&jctx, "name", &(*track)->name));
            spotify_free_nodes(&(*track)->artists);
            ERR_CHECK(json_obj_get_array(&jctx, "artists", &num_elem));
            for (int i = 0; i < num_elem; i++) {
                ERR_CHECK(json_arr_get_object(&jctx, i));
                char * artist_name;
                ERR_CHECK(json_obj_dup_string(&jctx, "name", &artist_name));
                assert(spotify_append_item_to_list(&(*track)->artists, artist_name));
                ERR_CHECK(json_arr_leave_object(&jctx));
            }
            ERR_CHECK(json_obj_leave_array(&jctx));
            free((*track)->album);
            ERR_CHECK(json_obj_get_object(&jctx, "album"));
            ERR_CHECK(json_obj_dup_string(&jctx, "name", &(*track)->album));
        }

        return spotify_evt;
    }
    // unknow event
    spotify_evt.type = UNKNOW;
    return spotify_evt;
}

/* Private functions ---------------------------------------------------------*/
/* static void onDevicePlaying(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* device = object_get_member(js, root, "device");
    assert(device && "key \"device\" missing");

    jsmntok_t* value = object_get_member(js, device, "id");
    assert(value && "key \"id\" missing");

    track->device.id = jsmn_obj_dup(js, value);
    assert(track->device.id && "Error allocating memory");

    value = object_get_member(js, device, "name");
    assert(value && "key \"name\" missing");

    track->device.name = jsmn_obj_dup(js, value);
    assert(track->device.name && "Error allocating memory");

    value = object_get_member(js, device, "volume_percent");
    assert(value && "key \"volume_percent\" missing");

    snprintf(track->device.volume_percent, 4, "%s\n", js + value->start);

    ESP_LOGD(TAG, "Device id: %s, name: %s", track->device.id, track->device.name);
}

static void onTrackIsPlaying(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* value = object_get_member(js, root, "is_playing");
    assert(value && "key \"is_playing\" missing");

    char type = (js + (value->start))[0];
    track->isPlaying = type == 't' ? true : false;
}

static void onTrackTime(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* value = object_get_member(js, root, "progress_ms");
    assert(value && "key \"progress_ms\" missing");

    track->progress_ms = natoi(js + value->start, value->end - value->start);

    value = object_get_member(js, root, "item");
    assert(value && "key \"item\" missing");

    value = object_get_member(js, value, "duration_ms");
    assert(value && "key \"duration_ms\" missing");

    track->duration_ms = natoi(js + value->start, value->end - value->start);
}

static inline int natoi(const char* str, short len)
{
    int ret = 0;
    for (short i = 0; i < len; ++i) {
        ret = ret * 10 + (str[i] - '0');
    }
    return ret;
} */

/**
 * @brief u8g2 selection list menu uses a string with '\\n' as
 * item separator. For example: 'item1\\nitem2\\ngo to Menu\\nEtc...'.
 * This function build that string with each playlist name.
 *
 */
/* esp_err_t static str_append(jsmntok_t* obj, const char* buf, char** str)
{
    if (*str == NULL) {
        *str = jsmn_obj_dup(buf, obj);
        return (*str == NULL) ? ESP_ERR_NO_MEM : ESP_OK;
    }

    uint16_t obj_len = obj->end - obj->start;
    uint16_t str_len = strlen(*str);

    char* r = realloc(*str, str_len + obj_len + 2);
    if (r == NULL)
        return ESP_ERR_NO_MEM;

    *str = r;

    (*str)[str_len++] = '\n';

    for (uint16_t i = 0; i < obj_len; i++) {
        (*str)[i + str_len] = *(buf + obj->start + i);
    }
    (*str)[str_len + obj_len] = '\0';

    ESP_LOGI(TAG, "str len: %d", strlen(*str));

    return ESP_OK;
} */
