/**
 * @file jukebox.h
 * @brief ESP32 LyraT-Mini V1.2 JukeBox - Shared Definitions
 *
 * Board: Espressif ESP32-LyraT-Mini V1.2
 * Codec: ES8311
 * Buttons: BOOT (GPIO0), REC (GPIO36), MODE (not used here)
 * LEDs: GPIO22 (Green), GPIO19 (Blue) — board-specific
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "periph_sdcard.h"
#include "board.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "board.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

/* ESP-ADF includes */
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "board.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "flac_decoder.h"
#include "aac_decoder.h"
#include "audio_element.h"

extern audio_pipeline_handle_t pipeline;
extern audio_element_handle_t fatfs_stream_reader;
extern audio_element_handle_t i2s_stream_writer;
extern audio_element_handle_t music_decoder;
extern esp_periph_config_t periph_cfg;
extern esp_periph_set_handle_t set;
extern audio_board_handle_t board_handle;

/* ─── WiFi Credentials (override in sdkconfig or here) ─── */
#ifndef WIFI_SSID
#define WIFI_SSID "Airtel_DigitalMonk"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "digimonk"
#endif
#define WIFI_MAX_RETRY 5

/* ─── SD Card SPI Pins (LyraT-Mini V1.2) ─── */
#define SD_PIN_NUM_MISO GPIO_NUM_15
#define SD_PIN_NUM_MOSI GPIO_NUM_12
#define SD_PIN_NUM_CLK GPIO_NUM_14
#define SD_PIN_NUM_CS GPIO_NUM_13

#define SD_MOUNT_POINT "/sdcard"
#define AUDIO_DIR "/sdcard/music"

/* ─── LyraT-Mini Buttons (GPIO) ─── */
#define BTN_PLAY_PAUSE GPIO_NUM_36 /* REC button (active low, pull-up) */
#define BTN_NEXT GPIO_NUM_39       /* Vol+ button                      */
#define BTN_PREV GPIO_NUM_33       /* Vol- button                      */
#define BTN_BOOT GPIO_NUM_0        /* Boot/Mode button                 */

/* ─── LyraT-Mini On-board LEDs ─── */
#define LED_GREEN GPIO_NUM_22
#define LED_BLUE GPIO_NUM_19

/* ─── Audio Config ─── */
#define SAMPLE_RATE 44100
#define BIT_DEPTH 16
#define CHANNELS 2
#define DEFAULT_VOLUME 60 /* 0–100 */
#define MAX_PLAYLIST 128
#define MAX_FILENAME_LEN 1024

/* ─── Web Server ─── */
#define WEB_SERVER_PORT 80
#define UPLOAD_CHUNK_SZ 1024

/* ─── Event group bits ─── */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define AUDIO_STOP_BIT BIT2
#define AUDIO_PAUSE_BIT BIT3

/* ─── Player States ─── */
typedef enum
{
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_PLAYING = 1,
    PLAYER_STATE_PAUSED = 2,
    PLAYER_STATE_STOPPED = 3,
} player_state_t;

/* ─── Player Commands (sent via queue) ─── */
typedef enum
{
    CMD_PLAY = 0,
    CMD_PAUSE = 1,
    CMD_RESUME = 2,
    CMD_STOP = 3,
    CMD_NEXT = 4,
    CMD_PREV = 5,
    CMD_AUTO_PLAY = 6,
    CMD_STOP_AUTO = 7,
    CMD_SET_VOLUME = 8,
    CMD_PLAY_FILE = 9,
} player_cmd_t;

/* ─── Player Command Message ─── */
typedef struct
{
    player_cmd_t cmd;
    char filename[MAX_FILENAME_LEN]; /* for CMD_PLAY_FILE */
    int value;                       /* for CMD_SET_VOLUME */
} player_msg_t;

/* ─── Global Handles (defined in main.c) ─── */
extern QueueHandle_t g_player_queue;
extern EventGroupHandle_t g_wifi_event_group;
extern volatile player_state_t g_player_state;
extern volatile int g_current_track;
extern volatile int g_total_tracks;
extern volatile int g_volume;
extern volatile bool g_auto_play;
extern char g_playlist[MAX_PLAYLIST][MAX_FILENAME_LEN];
extern char g_current_file[MAX_FILENAME_LEN];

/* ─── Module Init Prototypes ─── */
esp_err_t wifi_manager_init(void);
esp_err_t sd_card_init(void);
esp_err_t audio_player_init(void);
esp_err_t web_server_init(void);
esp_err_t button_handler_init(void);

/* ─── SD Card Helpers ─── */
int sd_scan_playlist(void);
bool sd_file_exists(const char *path);
void ensure_music_dir_exists(void);

/* ─── Audio Player Helpers ─── */
void audio_send_cmd(player_cmd_t cmd);
void audio_send_play_file(const char *filename);
void audio_send_volume(int vol);
const char *player_state_str(player_state_t s);
void create_audio_pipeline();
void stop_audio_play();
void play_audio(const char *filename);
static void player_task(void *arg);
