#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "spotify_client.h"

/* Locally scoped variables --------------------------------------------------*/
static const char*        TAG = "SPOTIFY_CLIENT_EXAMPLE";
static EventGroupHandle_t event_group = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("spotify_client", ESP_LOG_DEBUG);
    esp_log_level_set("HANDLER_CALLBACKS", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(spotify_client_init(5, &event_group));

    EventBits_t uxBits;
    uint32_t    mask = PLAYER_FIRST_EVENT | NO_PLAYER_ACTIVE_EVENT | PLAYER_STATE_CHANGED | DEVICE_STATE_CHANGED | ERROR_EVENT;
    xEventGroupSetBits(event_group, ENABLE_PLAYER);
    while (1) {
        uxBits = xEventGroupWaitBits(
            event_group,
            mask,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if (uxBits & PLAYER_FIRST_EVENT) {
            ESP_LOGI(TAG, "Event: PLAYER_FIRST_EVENT");
        } else if (uxBits & NO_PLAYER_ACTIVE_EVENT) {
            ESP_LOGI(TAG, "Event: NO_PLAYER_ACTIVE_EVENT");
        } else if (uxBits & PLAYER_STATE_CHANGED) {
            ESP_LOGI(TAG, "Event: PLAYER_STATE_CHANGED");
        } else if (uxBits & DEVICE_STATE_CHANGED) {
            ESP_LOGI(TAG, "Event: DEVICE_STATE_CHANGED");
        } else if (uxBits & ERROR_EVENT) {
            ESP_LOGI(TAG, "Event: ERROR_EVENT");
        }
    }
}
