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

SpotifyClientEvent_t parse_ws_event(const char* js, TrackInfo** track_info)
{
    // ESP_LOGW(TAG, "%s", js);

    SpotifyClientEvent_t spotify_evt = { 0 };

    jparse_ctx_t jctx;
    ERR_CHECK(json_parse_start_static(&jctx, js, strlen(js), tokens, MAX_TOKENS));

    int num_elem;
    ERR_CHECK(json_obj_get_array(&jctx, "payloads", &num_elem));
    // TODO: check if the array is greater than one
    ERR_CHECK(json_arr_get_object(&jctx, 0));
    ERR_CHECK(json_obj_get_array(&jctx, "events", &num_elem));
    // TODO: check if the array is greater than one
    ERR_CHECK(json_arr_get_object(&jctx, 0));
    // json_tok_t* t = jctx.cur;
    // printf("token content: %.*s\n", (int)t->end - t->start, js + t->start);

    bool match;
    ERR_CHECK(json_obj_match_string(&jctx, "type", "DEVICE_STATE_CHANGED", &match));
    if (match) {
        spotify_evt.event = DEVICE_STATE_CHANGED;
        return spotify_evt;
    }
    ERR_CHECK(json_obj_match_string(&jctx, "type", "PLAYER_STATE_CHANGED", &match));
    if (match) {
        spotify_evt.event = PLAYER_STATE_CHANGED;
        ERR_CHECK(json_obj_get_object(&jctx, "event"));
        ERR_CHECK(json_obj_get_object(&jctx, "state"));
        ERR_CHECK(json_obj_get_object(&jctx, "item"));

        // TODO: buscar el string del id, con su longitud, sin duplicar,
        // luego comparar con el ya existente, si cambio, se actualiza el
        // track con este

        return spotify_evt;
    }
    // unknow event
    spotify_evt.event = UNKNOW;
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

static void onTrackName(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* value = object_get_member(js, root, "item");
    assert(value && "key \"item\" missing");

    value = object_get_member(js, value, "name");
    assert(value && "key \"name\" missing");

    track->name = jsmn_obj_dup(js, value);
    assert(track->name && "Error allocating memory");

    ESP_LOGD(TAG, "Track: %s", track->name);
}

static void onArtistsName(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* value = object_get_member(js, root, "item");
    assert(value && "key \"item\" missing");

    value = object_get_member(js, value, "artists");
    assert(value && "key \"artists\" missing");

    jsmntok_t* artists = value;
    for (uint16_t i = 0; i < (artists->size); i++) {
        value = array_get_at(artists, i);
        assert(value && "array_get_at() failed. Maybe not an array");

        value = object_get_member(js, value, "name");
        assert(value && "key \"name\" missing");

        char* artist = jsmn_obj_dup(js, value);
        assert(artist && "Error allocating memory");

        Node* node = spotify_append_item_to_list(&track->artists, (void*)artist);
        assert(node && "Error allocating memory for node");
    }
}

static void onAlbumName(const char* js)
{
    TrackInfo* track = (TrackInfo*)obj;

    jsmntok_t* value = object_get_member(js, root, "item");
    assert(value && "key \"item\" missing");

    value = object_get_member(js, value, "album");
    assert(value && "key \"album\" missing");

    value = object_get_member(js, value, "name");
    assert(value && "key \"name\" missing");

    track->album = jsmn_obj_dup(js, value);
    assert(track->album && "Error allocating memory");

    ESP_LOGD(TAG, "Album: %s", track->album);
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

static void onExpiresIn(const char* js)
{
    AccessToken* token = (AccessToken*)obj;

    jsmntok_t* value = object_get_member(js, root, "expires_in");
    assert(value && "key \"expires_in\" missing");

    int seconds = natoi(js + value->start, value->end - value->start);
    token->expiresIn = time(0) + seconds;
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
