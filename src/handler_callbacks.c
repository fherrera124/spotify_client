/* Includes ------------------------------------------------------------------*/
#include "handler_callbacks.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spotify_client_priv.h"
#include <stdio.h>
#include <string.h>

/* Private macro -------------------------------------------------------------*/

#define ITEMS_START     "\"items\""
#define ITEMS_START_LEN strlen(ITEMS_START)
#define TOO_LARGE       "{error: \"Message too large for buffer\"}"

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static const char* TAG = "HANDLER_CALLBACKS";

/* External variables --------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
size_t static inline memcpy_trimmed(char* dest, int dest_size, const char* src, size_t src_len);

/* Exported functions --------------------------------------------------------*/
void default_http_event_handler(char* http_buffer, esp_http_client_event_t* evt)
{
    static size_t chars_stored = 0;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        size_t stored = memcpy_trimmed(http_buffer + chars_stored, MAX_HTTP_BUFFER - chars_stored, evt->data, evt->data_len);
        chars_stored += stored;
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "Chars stored: %d", chars_stored);
        http_buffer[chars_stored] = 0;
        chars_stored = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        int       mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            http_buffer[chars_stored] = 0;
            chars_stored = 0;
        }
        break;
    default:
        break;
    }
}

void default_ws_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{

    handler_args_t* args = (handler_args_t*)handler_args;

    char*              buffer = args->buffer;
    EventGroupHandle_t event_group = args->event_group;

    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGD(TAG, "WebSocket Connected");
        xEventGroupSetBits(event_group, WS_CONNECT_EVENT);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "WebSocket Disconnected");
        xEventGroupSetBits(event_group, WS_DISCONNECT_EVENT);
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGD(TAG, "WebSocket Data Received: Opcode=%d, Length=%d", data->op_code, data->data_len);

        if (data->op_code == 0xA) {
            break;
        }

        if (data->op_code == 0x1 || data->op_code == 0x2) {

            // validamos previamente si el total de todo entrara en nuestro buffer
            if ((data->payload_len) + 1 > MAX_HTTP_BUFFER) {
                // first fragment of message
                if (data->payload_offset == 0) {
                    ESP_LOGE(TAG, TOO_LARGE);
                    memcpy(buffer, TOO_LARGE, strlen(TOO_LARGE));
                }
                break;
            }

            memcpy(buffer + data->payload_offset, data->data_ptr, data->data_len);
        }

        // para saber si ya procesamos todo el mensaje
        if (data->payload_offset + data->data_len == data->payload_len) {
            ESP_LOGD(TAG, "Complete message received");
            buffer[data->payload_len] = 0;
            ESP_LOGD(TAG, "%s", buffer);

            xEventGroupSetBits(event_group, WS_DATA_EVENT);
        }

        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket Error");
        break;
    }
}

/**
 * @brief We don't have enough memory to store the whole JSON. So the
 * approach is to process the "items" array one playlist at a time.
 *
 */
void playlists_handler(char* http_buffer, esp_http_client_event_t* evt)
{
    static int       chars_stored = 0; // Number of chars stored in buffer
    static int       in_items = 0; // Bandera para indicar si estamos dentro del arreglo "items"
    static int       brace_count = 0; // Contador de llaves para detectar el final de un elemento
    static esp_err_t err = ESP_OK;

    char* data = (char*)evt->data;
    int   left = evt->data_len;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (err == ESP_FAIL) {
            return;
        }
        if (!in_items) {
            char* match_found = memmem(data, left, ITEMS_START, ITEMS_START_LEN);
            if (!match_found)
                break;
            in_items = 1;
            match_found += ITEMS_START_LEN;
            left -= match_found - data;
            data = match_found;
        }
        bool in_whitespace = false;
        for (int i = 0; i < left; i++) {
            if (data[i] == ' ') {
                if (!in_whitespace) {
                    http_buffer[chars_stored++] = ' ';
                    in_whitespace = true;
                    continue;
                }
            } else if (!isspace((unsigned char)data[i])) {
                in_whitespace = false;
                if (data[i] == '{') {
                    if (brace_count == 0) {
                        // Start of new playlist
                        chars_stored = 0;
                    }
                    brace_count++;
                }
                if (brace_count > 0) {
                    if (chars_stored < MAX_HTTP_BUFFER - 1) {
                        http_buffer[chars_stored++] = data[i];
                    } else {
                        ESP_LOGE(TAG, "Buffer overflow, data will be truncated!");
                        http_buffer[chars_stored] = '\0';
                        err = ESP_FAIL;
                        return;
                    }
                }
                if (data[i] == '}') {
                    brace_count--;
                    if (brace_count == 0) {
                        // End of playlist
                        http_buffer[chars_stored] = '\0';
                        ESP_LOGW(TAG, "Playlist:\n%s", http_buffer);
                        // TODO: send playlist item
                        chars_stored = 0;
                    }
                }
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        // err ? NOTIFY_DISPLAY(PLAYLISTS_ERROR) : NOTIFY_DISPLAY(PLAYLISTS_OK);
        chars_stored = in_items = brace_count = err = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        chars_stored = in_items = brace_count = err = 0;
        // NOTIFY_DISPLAY(PLAYLISTS_ERROR);
        break;
    default:
        break;
    }
}

/* Private functions ---------------------------------------------------------*/
size_t static inline memcpy_trimmed(char* dest, int dest_size, const char* src, size_t src_len)
{
    bool   in_whitespace = false;
    size_t chars_stored = 0;
    for (size_t i = 0; i < src_len; i++) {
        if ((int)chars_stored > dest_size - 1) {
            ESP_LOGE(TAG, "Buffer overflow, stoping writing!");
            return chars_stored;
        }
        if (src[i] == ' ') {
            if (!in_whitespace) {
                dest[chars_stored++] = ' ';
                in_whitespace = true;
            }
        } else if (!isspace((unsigned char)src[i])) {
            in_whitespace = false;
            dest[chars_stored++] = src[i];
        }
    }
    return chars_stored;
}