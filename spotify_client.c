/* Includes ------------------------------------------------------------------*/
#include <string.h>

#include "credentials.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_websocket_client.h"
#include "handler_callbacks.h"
#include "limits.h"
#include "parse_objects.h"
#include "spotify_client.h"
#include "spotify_client_priv.h"
#include "string_utils.h"

/* Private macro -------------------------------------------------------------*/
#define ACCESS_TOKEN_URL    "https://discord.com/api/v8/users/@me/connections/spotify/" SPOTIFY_UID "/access-token"
#define PLAYER              "/me/player"
#define TOKEN_URL           "https://accounts.spotify.com/api/token"
#define PLAYING             PLAYER "?market=AR&additional_types=episode"
#define PLAY_TRACK          PLAYER "/play"
#define PAUSE_TRACK         PLAYER "/pause"
#define PREV_TRACK          PLAYER "/previous"
#define NEXT_TRACK          PLAYER "/next"
#define VOLUME              PLAYER "/volume?volume_percent="
#define PLAYERURL(ENDPOINT) "https://api.spotify.com/v1" ENDPOINT
#define ACQUIRE_LOCK(mux)   xSemaphoreTake(mux, portMAX_DELAY)
#define RELEASE_LOCK(mux)   xSemaphoreGive(mux)
#define RETRIES_ERR_CONN    3
#define SPRINTF_BUF_SIZE    100

#define PREPARE_CLIENT(state, AUTH, TYPE)                                  \
    esp_http_client_set_url(http_client.handle, http_client.endpoint);     \
    esp_http_client_set_method(http_client.handle, http_client.method);    \
    esp_http_client_set_header(http_client.handle, "Authorization", AUTH); \
    esp_http_client_set_header(http_client.handle, "Content-Type", TYPE)

/* DRY macros */
#define CALLOC(var, size)  \
    var = calloc(1, size); \
    assert((var) && "Error allocating memory")

/* Private types -------------------------------------------------------------*/
typedef void (*handler_cb_t)(char*, esp_http_client_event_t*);

typedef struct {
    esp_http_client_handle_t handle; /*!<*/
    AccessToken              token; /*!<*/
    const char*              endpoint; /*!<*/
    esp_http_client_method_t method; /*!<*/
    handler_cb_t             handler_cb; /*!< Callback function to handle http events */
} HttpClient_data_t;

/* Locally scoped variables --------------------------------------------------*/
static const char*            TAG = "spotify_client";
EventGroupHandle_t            event_group = NULL;
static char                   http_buffer[MAX_HTTP_BUFFER];
static char                   ws_buffer[4096];
static char                   sprintf_buf[SPRINTF_BUF_SIZE];
static SemaphoreHandle_t      http_buf_lock = NULL; /* Mutex to manage access to the http client buffer */
static uint8_t                s_retries = 0; /* number of retries on error connections */
static HttpClient_data_t      http_client = { .token.value = { 'B', 'e', 'a', 'r', 'e', 'r', ' ', '\0' } };
static const char*            HTTP_METHOD_LOOKUP[] = { "GET", "POST", "PUT" };
esp_websocket_client_handle_t ws_client_handle;
static QueueHandle_t          event_queue;

/* Globally scoped variables definitions -------------------------------------*/
TrackInfo* TRACK = &(TrackInfo) { 0 }; /* pointer to an unnamed object, constructed in place
by the the COMPOUND LITERAL expression "(TrackInfo) { 0 }". NOTE: Although the syntax of a compound
literal is similar to a cast, the important distinction is that a cast is a non-lvalue expression
while a compound literal is an lvalue */

/* External variables --------------------------------------------------------*/
extern const char certs_pem_start[] asm("_binary_certs_pem_start");
extern const char certs_pem_end[] asm("_binary_certs_pem_end");

/* Private function prototypes -----------------------------------------------*/
static HttpStatus_Code get_access_token();
static esp_err_t       _http_event_handler(esp_http_client_event_t* evt);
static void            player_task(void* pvParameters);
static HttpStatus_Code confirm_ws_session(char* conn_id);
static void            free_track(TrackInfo* track);
static void            handle_track_fetched(TrackInfo* new_track);
static void            handle_err_connection();
static void            debug_mem();
bool                   is_player_state_changed(const char* message);
HttpStatus_Code        http_user_playlists();

/* Exported functions --------------------------------------------------------*/
esp_err_t spotify_client_init(UBaseType_t priority)
{
    static esp_http_client_config_t http_cfg = {
        .url = "https://api.spotify.com/v1",
        .event_handler = _http_event_handler,
        .cert_pem = certs_pem_start,
        .buffer_size_tx = DEFAULT_HTTP_BUF_SIZE + 256,
    };

    static esp_websocket_client_config_t websocket_cfg = {
        .uri = "wss://dealer.spotify.com",
        .cert_pem = certs_pem_start,
        .ping_interval_sec = 30,
    };

    CALLOC(TRACK->name, 1);

    http_client.handle = esp_http_client_init(&http_cfg);
    if (!http_client.handle) {
        ESP_LOGE(TAG, "Error on esp_http_client_init()");
        return ESP_FAIL;
    }

    ws_client_handle = esp_websocket_client_init(&websocket_cfg);
    if (!ws_client_handle) {
        ESP_LOGE(TAG, "Error on esp_websocket_client_init()");
        return ESP_FAIL;
    }

    http_buf_lock = xSemaphoreCreateMutex();
    if (!http_buf_lock) {
        ESP_LOGE(TAG, "Error on xSemaphoreCreateMutex()");
        return ESP_FAIL;
    }

    event_queue = xQueueCreate(1, sizeof(spotify_client_event_t));
    if (!event_queue) {
        ESP_LOGE(TAG, "Failed to create queue for events");
        return ESP_FAIL;
    }

    http_client.handler_cb = default_http_event_handler;

    init_functions_cb();

    event_group = xEventGroupCreate();

    if (!event_group) {
        ESP_LOGE("EventGroup", "Failed to create event group");
        return ESP_FAIL;
    }

    int res = xTaskCreate(player_task, "player_task", 4096, NULL, priority, NULL);
    if (!res) {
        ESP_LOGE(TAG, "Error creating task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t spotify_dispatch_event(SendEvent_t event)
{
    if (!event_group) {
        ESP_LOGE(TAG, "Run spotify_client_init() first");
        return ESP_FAIL;
    }
    switch (event) {
    case ENABLE_PLAYER_EVENT:
        xEventGroupSetBits(event_group, ENABLE_PLAYER);
        break;
    case DISABLE_PLAYER_EVENT:
        xEventGroupSetBits(event_group, DISABLE_PLAYER);
        break;
    case DATA_PROCESSED_EVENT:
        xEventGroupSetBits(event_group, DATA_PROCESSED);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void waitForQueueEvent(spotify_client_event_t* data)
{
    xQueueReceive(event_queue, data, portMAX_DELAY);

    // maybe we can send the DATA_PROCESSED_EVENT here
}

HttpStatus_Code player_cmd(Player_cmd_t cmd, void* payload)
{
    switch (cmd) {
    case PAUSE:
        http_client.method = HTTP_METHOD_PUT;
        http_client.endpoint = PLAYERURL(PAUSE_TRACK);
        break;
    case PLAY:
        http_client.method = HTTP_METHOD_PUT;
        http_client.endpoint = PLAYERURL(PLAY_TRACK);
        break;
    case PREVIOUS:
        http_client.method = HTTP_METHOD_POST;
        http_client.endpoint = PLAYERURL(PREV_TRACK);
        break;
    case NEXT:
        http_client.method = HTTP_METHOD_POST;
        http_client.endpoint = PLAYERURL(NEXT_TRACK);
        break;
    case CHANGE_VOLUME:
        break;
    case GET_STATE:
        http_client.method = HTTP_METHOD_GET;
        http_client.endpoint = PLAYERURL(PLAYER);
        break;
    default:
        ESP_LOGE(TAG, "Unknow command");
        return 999;
    }

    ACQUIRE_LOCK(http_buf_lock);
    http_client.handler_cb = default_http_event_handler;
    PREPARE_CLIENT(http_client, http_client.token.value, "application/json");
retry:
    ESP_LOGD(TAG, "Endpoint to send: %s", http_client.endpoint);
    esp_err_t err = esp_http_client_perform(http_client.handle);
    if (err == ESP_OK) {
        s_retries = 0;
        HttpStatus_Code status_code = esp_http_client_get_status_code(http_client.handle);
        int             length = esp_http_client_get_content_length(http_client.handle);
        ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d", status_code, length);
        ESP_LOGD(TAG, "%s", http_buffer);

        /* If for any reason, we dont have the actual state
         * of the player, then when sending play command when
         * paused, or viceversa, we receive error 403. */
        if (status_code == HttpStatus_Forbidden) {
            if (cmd == PLAY) {
                http_client.endpoint = PLAYERURL(PAUSE_TRACK);
            } else if (cmd == PAUSE) {
                http_client.endpoint = PLAYERURL(PLAY_TRACK);
            }
            esp_http_client_set_url(http_client.handle, http_client.endpoint);
            goto retry; // add max number of retries maybe
        }
        RELEASE_LOCK(http_buf_lock);
        return status_code;
    } else {
        handle_err_connection(err);
        goto retry;
    }
}

HttpStatus_Code http_play_context_uri(const char* uri)
{
    ACQUIRE_LOCK(http_buf_lock);
    int str_len = sprintf(sprintf_buf, "{\"context_uri\":\"%s\"}", uri);
    assert((str_len <= SPRINTF_BUF_SIZE) && "uri too long");

    http_client.handler_cb = default_http_event_handler;
    http_client.method = HTTP_METHOD_PUT;
    http_client.endpoint = PLAYERURL(PLAY_TRACK);

    esp_http_client_set_post_field(http_client.handle, sprintf_buf, str_len);
    PREPARE_CLIENT(http_client, http_client.token.value, "application/json");
retry:
    ESP_LOGD(TAG, "Endpoint to send: %s", http_client.endpoint);
    esp_err_t       err = esp_http_client_perform(http_client.handle);
    HttpStatus_Code status_code = esp_http_client_get_status_code(http_client.handle);
    if (err == ESP_OK) {
        s_retries = 0;
        int length = esp_http_client_get_content_length(http_client.handle);
        ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d", status_code, length);
        ESP_LOGD(TAG, "%s", http_buffer);
        esp_http_client_set_post_field(http_client.handle, NULL, 0);
        RELEASE_LOCK(http_buf_lock);
        return status_code;
    } else {
        handle_err_connection(err);
        goto retry;
    }
}

/* Private functions ---------------------------------------------------------*/
static void player_task(void* pvParameters)
{
    handler_args_t handler_args = {
        .buffer = ws_buffer,
        .event_group = event_group
    };
    EventBits_t uxBits;

    uint32_t notif;

    while (1) {
        uxBits = xEventGroupWaitBits(
            event_group,
            ENABLE_PLAYER | DISABLE_PLAYER | WS_DATA_EVENT | WS_DISCONNECT_EVENT | DATA_PROCESSED,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if (uxBits & (ENABLE_PLAYER | WS_DISCONNECT_EVENT)) {

            if (get_access_token() != HttpStatus_Ok) {
                ESP_LOGE(TAG, "Error trying to get an access token");
                // TODO: send error to queue
                break;
            }

            // initial state
            HttpStatus_Code status_code = player_cmd(GET_STATE, NULL);
            if (status_code == HttpStatus_Ok) {
                // there is a device atached to playback,
                // fire as a first event
                // TODO: send to queue NO_PLAYER_ACTIVE_EVENT
            } else if (status_code == 204) {
                // no device is atached to playback,
                // fire an event of no device playing
                // TODO: send to queue NO_PLAYER_ACTIVE_EVENT
            } else {
                ESP_LOGE(TAG, "Error trying to get player state");
                // TODO: send error to queue
                break;
            }

            // start the ws session
            char* uri = http_utils_join_string("wss://dealer.spotify.com/?access_token=", 0, http_client.token.value + 7, strlen(http_client.token.value) - 7);
            esp_websocket_client_set_uri(ws_client_handle, uri);
            free(uri);
            esp_websocket_register_events(ws_client_handle, WEBSOCKET_EVENT_ANY, default_ws_event_handler, &handler_args);
            esp_err_t err = esp_websocket_client_start(ws_client_handle);
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group, READY_FOR_DATA);
            } else {
                // TODO: send error to queue
            }
        } else if (uxBits & DISABLE_PLAYER) {
            esp_websocket_client_close(ws_client_handle, portMAX_DELAY);
        } else if (uxBits & WS_DATA_EVENT) {

            // now the ws buff is our
            // analize data of ws event

            char* data = NULL;
            parseConnectionId(ws_buffer, &data);

            if (data) {
                ESP_LOGD(TAG, "Connection id: '%s'", data);

                if (confirm_ws_session(data) != HttpStatus_Ok) {
                    ESP_LOGE(TAG, "Error trying to confirm ws session");
                    // TODO: send error to queue
                    break;
                }
                xEventGroupSetBits(event_group, READY_FOR_DATA);
            } else {
                // handle_track_fetched(track);
                TrackInfo foo;
                xQueueSend(event_queue, &foo, portMAX_DELAY);
            }
        } else if (uxBits & DATA_PROCESSED) {
            xEventGroupSetBits(event_group, READY_FOR_DATA);
            // now the ws buff isn't our anymore
        }
    }
}

static inline void handle_track_fetched(TrackInfo* track)
{
    parseTrackInfo(ws_buffer, track);

    if (0 == strcmp(TRACK->name, track->name)) {
        free_track(track);
    } else {
        ESP_LOGI(TAG, "Title: %s", TRACK->name);
        StrListItem* artist = TRACK->artists.first;
        while (artist) {
            ESP_LOGI(TAG, "Artist: %s", artist->str);
            artist = artist->next;
        }
        ESP_LOGI(TAG, "Album: %s", TRACK->album);
    }
}

HttpStatus_Code confirm_ws_session(char* conn_id)
{
    ACQUIRE_LOCK(http_buf_lock);
    http_client.handler_cb = default_http_event_handler;
    http_client.method = HTTP_METHOD_PUT;
    char* url = http_utils_join_string("https://api.spotify.com/v1/me/notifications/player?connection_id=", 0, conn_id, 0);
    http_client.endpoint = url; // esp_http_client_set_url(http_client.handle, url);
    PREPARE_CLIENT(http_client, http_client.token.value, "application/json");

retry:
    ESP_LOGD(TAG, "Endpoint to send: %s", http_client.endpoint);
    esp_err_t err = esp_http_client_perform(http_client.handle);
    if (err == ESP_OK) {
        s_retries = 0;
        HttpStatus_Code status_code = esp_http_client_get_status_code(http_client.handle);
        int             length = esp_http_client_get_content_length(http_client.handle);
        ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d", status_code, length);
        free(conn_id);
        free(url);
        RELEASE_LOCK(http_buf_lock);
        return status_code;
    } else {
        handle_err_connection(err);
        goto retry;
    }
}

static inline void handle_err_connection(esp_err_t err)
{
    ESP_LOGE(TAG, "HTTP %s request failed: %s",
        HTTP_METHOD_LOOKUP[http_client.method],
        esp_err_to_name(err));
    assert((++s_retries <= RETRIES_ERR_CONN) && "Restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "Retrying %d/%d...", s_retries, RETRIES_ERR_CONN);
    debug_mem();
}

static esp_err_t _http_event_handler(esp_http_client_event_t* evt)
{
    http_client.handler_cb(http_buffer, evt);
    return ESP_OK;
}

static inline void free_track(TrackInfo* track)
{
    free(track->name);
    free(track->album);

    strListClear(&track->artists);

    free(track->device.id);
    free(track->device.name);
    free(track->device.type);
    strcpy(track->device.volume_percent, "-1");
}

static inline void debug_mem()
{
    /* uxTaskGetStackHighWaterMark() returns the minimum amount of remaining
     * stack space that was available to the task since the task started
     * executing - that is the amount of stack that remained unused when the
     * task stack was at its greatest (deepest) value. This is what is referred
     * to as the stack 'high water mark'.
     * */
    ESP_LOGI(TAG, "stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    ESP_LOGI(TAG, "minimum free heap size: %lu", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "free heap size: %lu", esp_get_free_heap_size());
}

static HttpStatus_Code get_access_token()
{
    ACQUIRE_LOCK(http_buf_lock);
    http_client.handler_cb = default_http_event_handler;
    http_client.method = HTTP_METHOD_GET;
    http_client.endpoint = ACCESS_TOKEN_URL;
    PREPARE_CLIENT(http_client, DISCORD_TOKEN, "application/json");

retry:
    ESP_LOGD(TAG, "Endpoint to send: %s", http_client.endpoint);
    esp_err_t err = esp_http_client_perform(http_client.handle);
    if (err == ESP_OK) {
        s_retries = 0;
        HttpStatus_Code status_code = esp_http_client_get_status_code(http_client.handle);
        int             length = esp_http_client_get_content_length(http_client.handle);
        ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d", status_code, length);
        if (status_code == HttpStatus_Ok) {
            parseAccessToken(http_buffer, &http_client.token);
            ESP_LOGD(TAG, "Access Token obtained:\n%s", &http_client.token.value[7]);
        }
        RELEASE_LOCK(http_buf_lock);
        return status_code;
    } else {
        handle_err_connection(err);
        goto retry;
    }
}

HttpStatus_Code http_user_playlists()
{
    ACQUIRE_LOCK(http_buf_lock);
    http_client.handler_cb = playlists_handler;
    http_client.method = HTTP_METHOD_GET;
    http_client.endpoint = PLAYERURL("/me/playlists?offset=0&limit=50");
    PREPARE_CLIENT(http_client, http_client.token.value, "application/json");
retry:
    ESP_LOGD(TAG, "Endpoint to send: %s", http_client.endpoint);
    esp_err_t err = esp_http_client_perform(http_client.handle);

    if (err == ESP_OK) {
        s_retries = 0;
        HttpStatus_Code status_code = esp_http_client_get_status_code(http_client.handle);
        int             length = esp_http_client_get_content_length(http_client.handle);
        ESP_LOGD(TAG, "HTTP Status Code = %d, content_length = %d", status_code, length);
        RELEASE_LOCK(http_buf_lock);
        return status_code;
    } else {
        handle_err_connection(err);
        goto retry;
    }
}