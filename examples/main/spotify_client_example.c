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
static const char* TAG = "SPOTIFY_CLIENT_EXAMPLE";

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
    ESP_ERROR_CHECK(spotify_client_init(5));

    // obtain the user playlists
    List* playlists = spotify_user_playlists();
    if (playlists->count > 0) {
        ESP_LOGI(TAG, "User playlists:");
        Node* playlist_node = playlists->first;
        while (playlist_node) {
            PlaylistItem_t* data = playlist_node->data;
            ESP_LOGI(TAG, "Playlist name: %s", data->name);
            ESP_LOGI(TAG, "Playlist uri: %s", data->uri);
            playlist_node = playlist_node->next;
        }
        spotify_free_nodes(playlists);
    } else {
        ESP_LOGW(TAG, "No playlists found");
    }
    assert(playlists->count == 0);

    // obtain the available devices
    List* available_devices = spotify_available_devices();
    if (available_devices->count > 0) {
        ESP_LOGI(TAG, "Available devices:");
        Node* device_node = available_devices->first;
        while (device_node) {
            DeviceItem_t* data = device_node->data;
            ESP_LOGI(TAG, "Device name: %s", data->name);
            ESP_LOGI(TAG, "Device id: %s", data->id);
            device_node = device_node->next;
        }
        spotify_free_nodes(available_devices);
    } else {
        ESP_LOGW(TAG, "No available devices found");
    }
    assert(available_devices->count == 0);

    // enable the player and wait for events
    spotify_dispatch_event(ENABLE_PLAYER_EVENT);
    SpotifyClientEvent_t event;
    while (1) {
        spotify_wait_event(&event);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Sending event that the event was processed");
        spotify_dispatch_event(DATA_PROCESSED_EVENT);
    }
}
