/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_websocket_client.h"
#include "spotify_client_priv.h"
#include "handler_callbacks.h"

/* Private macro -------------------------------------------------------------*/

#define ITEMS_START     "\"items\""
#define ITEMS_START_LEN strlen(ITEMS_START)
#define TOO_LARGE       "{error: \"Message too large for buffer\"}"

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static const char* TAG = "HANDLER_CALLBACKS";

/* External variables --------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
void default_http_event_handler(char* http_buffer, esp_http_client_event_t* evt)
{

    static int output_len = 0; // Stores number of bytes stored in buffer

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if ((output_len + evt->data_len) > MAX_HTTP_BUFFER) {
            ESP_LOGE(TAG, "Buffer overflow, data_len=%d", evt->data_len);
            return;
        }

        memcpy(http_buffer + output_len, evt->data, evt->data_len);
        output_len += evt->data_len;
        break;
    case HTTP_EVENT_ON_FINISH:
        http_buffer[output_len] = 0;
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:;
        int       mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            http_buffer[output_len] = 0;
            output_len = 0;
        }
        break;
    default:
        break;
    }
}

void default_ws_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{

    handler_args_t* args = (handler_args_t*)handler_args;

    char*        buffer = args->buffer;
    EventGroupHandle_t* event_group = args->event_group;

    ESP_LOGD(TAG, "event_group: '%p'", event_group);

    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGD(TAG, "WebSocket Connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "WebSocket Disconnected");
        xEventGroupSetBits(*event_group, WS_DISCONNECT_EVENT);
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGD(TAG, "WebSocket Data Received: Opcode=%d, Length=%d", data->op_code, data->data_len);

        if (data->op_code == 0xA) {
            break;
        }

        if (data->op_code == 0x1 || data->op_code == 0x2) {

            EventBits_t uxBits = xEventGroupGetBits(*event_group);

            /* if (!(uxBits & BUFFER_CONSUMED)) {
                ESP_LOGW(TAG, "Buffer is bussy, discarding incoming data");
                break;
            } */

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

            xEventGroupSetBits(*event_group, WS_DATA_EVENT);
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
    static int       output_len = 0; // Stores number of bytes stored in buffer
    static int       in_items = 0; // Bandera para indicar si estamos dentro del arreglo "items"
    static int       brace_count = 0; // Contador de llaves para detectar el final de un elemento
    static esp_err_t err = ESP_OK;

    char* data = (char*)evt->data;
    int   left = evt->data_len;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        // ESP_LOGI(TAG, "Data received: %s", data);
        if (err == ESP_FAIL) {
            return;
        }
        if (!in_items) {
            char* match_found = memmem(data, left, ITEMS_START, ITEMS_START_LEN);
            if (match_found) {
                in_items = 1;
                match_found += ITEMS_START_LEN;
                left -= match_found - data;
                data = match_found;
            }
        }
        if (in_items) {
            for (int i = 0; i < left; i++) {
                if (data[i] == '{') {
                    if (brace_count == 0) {
                        // Start of new playlist
                        output_len = 0;
                    }
                    brace_count++;
                }
                if (brace_count > 0) {
                    if (output_len < MAX_HTTP_BUFFER - 1) {
                        http_buffer[output_len++] = data[i];
                    } else {
                        ESP_LOGE(TAG, "Buffer overflow, data_len=%d", left);
                        err = ESP_FAIL;
                        return;
                    }
                }
                if (data[i] == '}') {
                    brace_count--;
                    if (brace_count == 0) {
                        // End of playlist
                        http_buffer[output_len] = '\0';
                        //parse_playlist(http_buffer);
                        output_len = 0;
                    }
                }
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        //err ? NOTIFY_DISPLAY(PLAYLISTS_ERROR) : NOTIFY_DISPLAY(PLAYLISTS_OK);
        output_len = in_items = brace_count = err = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        output_len = in_items = brace_count = err = 0;
        //NOTIFY_DISPLAY(PLAYLISTS_ERROR);
        break;
    default:
        break;
    }
}

/* Private functions ---------------------------------------------------------*/
