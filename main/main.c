/**
 * @file main.c
 * @brief ESP32 LyraT-Mini V1.2 JukeBox 
 *
 * Initialises all subsystems:
 *   1. NVS flash
 *   2. SD card (FAT/SPI)
 *   3. WiFi (station mode)
 *   4. Audio player pipeline (ESP-ADF)
 *   5. HTTP web server (upload + control)
 *   6. Physical button handler
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "jukebox.h"

static const char *TAG = "JUKEBOX_MAIN";

/* ─── Global State ─────────────────────────────────────── */
QueueHandle_t g_player_queue;
EventGroupHandle_t g_wifi_event_group;
volatile player_state_t g_player_state = PLAYER_STATE_IDLE;
volatile int g_current_track = 0;
volatile int g_total_tracks = 0;
volatile int g_volume = DEFAULT_VOLUME;
volatile bool g_auto_play = false;
char g_playlist[MAX_PLAYLIST][MAX_FILENAME_LEN];
char g_current_file[MAX_FILENAME_LEN] = {0};

/* ─── LED blink task (visual feedback) ──────────────────── */
static void led_task(void *arg)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << LED_GREEN) | (1ULL << LED_BLUE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    while (1)
    {
        switch (g_player_state)
        {
        case PLAYER_STATE_PLAYING:
            /* Green solid, Blue off */
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case PLAYER_STATE_PAUSED:
            /* Both blink alternately */
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            break;

        case PLAYER_STATE_IDLE:
        case PLAYER_STATE_STOPPED:
        default:
            /* Blue slow blink, Green off */
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(LED_BLUE, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
    }
}

/* ─── app_main ───────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== JukeBox ESP32 LyraT-Mini V1.2 Booting ===");

    /* 1. NVS Flash */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "[1/6] NVS initialised");

    g_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_LOGI(TAG, "[3/6] WiFi initialised");

    /* Wait for connection (10 s timeout) */
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "WiFi connected — web dashboard ready");
    }
    else
    {
        ESP_LOGW(TAG, "WiFi not connected — running offline");
    }

    /* 2. Player command queue */
    g_player_queue = xQueueCreate(16, sizeof(player_msg_t));
    if (!g_player_queue)
    {
        ESP_LOGE(TAG, "Failed to create player queue!");
        esp_restart();
    }

    /* 5. Audio Pipeline */
    create_audio_pipeline();
    ESP_LOGI(TAG, "[4/6] Audio player initialised");
    ensure_music_dir_exists();

    /* Scan initial playlist */
    g_total_tracks = sd_scan_playlist();
    ESP_LOGI(TAG, "Found %d audio tracks", g_total_tracks);

    /* 6. Web Server */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_ERROR_CHECK(web_server_init());
        ESP_LOGI(TAG, "[5/6] Web server started on port %d", WEB_SERVER_PORT);
    }

    ESP_LOGI(TAG, "=== JukeBox Ready! ===");
}
