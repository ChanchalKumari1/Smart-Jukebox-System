#include "jukebox.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
#include "audio_hal.h"

static const char *TAG = "AUDIO_PLAYER";

audio_pipeline_handle_t pipeline = NULL;
audio_element_handle_t fatfs_stream_reader = NULL;
audio_element_handle_t i2s_stream_writer = NULL;
audio_element_handle_t music_decoder = NULL;
audio_board_handle_t board_handle = NULL;
esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
esp_periph_set_handle_t set;
// Event listener handle (persistent)
static audio_event_iface_handle_t evt = NULL;

// Current playback status
static bool is_playing = false;
static bool is_paused = false;

// Forward declaration
static void player_task(void *arg);

void create_audio_pipeline(void)
{
    ESP_LOGI(TAG, "[1] Initialising peripherals and codec");

    // 1. Create peripheral set (for SD card and others)
    set = esp_periph_set_init(&periph_cfg);
    if (!set)
    {
        ESP_LOGE(TAG, "Failed to init periph set");
        return;
    }

    // 2. Initialise SD card using ADF (you may keep your own sd_card_init instead)
    audio_board_sdcard_init(set, SD_MODE_1_LINE);

    // 3. Initialise codec
    board_handle = audio_board_init();
    if (!board_handle)
    {
        ESP_LOGE(TAG, "Board init failed");
        return;
    }
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, DEFAULT_VOLUME);

    // 4. Create audio pipeline
    ESP_LOGI(TAG, "[2] Creating pipeline elements");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline)
    {
        ESP_LOGE(TAG, "Pipeline init failed");
        return;
    }

    // 5. Create FATFS reader (stream from SD card)
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.out_rb_size = 2048; // 2 KB ringbuffer
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    if (!fatfs_stream_reader)
    {
        ESP_LOGE(TAG, "FATFS stream init failed");
        return;
    }

    // 6. Create I2S writer (to codec)
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = 2048;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (!i2s_stream_writer)
    {
        ESP_LOGE(TAG, "I2S stream init failed");
        return;
    }

    // 7. Create MP3 decoder (increase stack to avoid overflow)
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_stack = 4096; // increased from default
    mp3_cfg.out_rb_size = 2048;
    music_decoder = mp3_decoder_init(&mp3_cfg);
    if (!music_decoder)
    {
        ESP_LOGE(TAG, "MP3 decoder init failed");
        return;
    }

    // 8. Register elements
    ESP_LOGI(TAG, "[3] Registering elements");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, music_decoder, "dec");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    // 9. Link them
    const char *link_tag[3] = {"file", "dec", "i2s"};
    audio_pipeline_link(pipeline, link_tag, 3);

    // 10. Create event listener (persistent)
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    if (!evt)
    {
        ESP_LOGE(TAG, "Event listener init failed");
        return;
    }
    audio_pipeline_set_listener(pipeline, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    // 11. Create player task (only once)
    xTaskCreatePinnedToCore(player_task, "player_task", 8192, NULL, 10, NULL, 1);

    ESP_LOGI(TAG, "Audio pipeline ready");
}

// Stop playback without destroying pipeline
static void stop_current_playback(void)
{
    if (pipeline)
    {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_elements(pipeline);
        // Do NOT terminate or deinit – keep pipeline alive
    }
    is_playing = false;
    is_paused = false;
    g_player_state = PLAYER_STATE_STOPPED;
}

// Play a new file (assumes pipeline is ready)
static void play_audio_file(const char *filename)
{
    if (!pipeline || !fatfs_stream_reader)
    {
        ESP_LOGE(TAG, "Pipeline not ready");
        return;
    }

    // Stop any current playback
    stop_current_playback();

    // Set new URI
    ESP_LOGI(TAG, "Playing: %s", filename);
    audio_element_set_uri(fatfs_stream_reader, filename);
    strncpy(g_current_file, filename, MAX_FILENAME_LEN - 1);

    // Run the pipeline (non-blocking – event loop runs in player_task)
    esp_err_t ret = audio_pipeline_run(pipeline);
    if (ret == ESP_OK)
    {
        is_playing = true;
        g_player_state = PLAYER_STATE_PLAYING;
    }
    else
    {
        ESP_LOGE(TAG, "Pipeline run failed: %d", ret);
    }
}

// Player task: processes commands and listens for pipeline events
static void player_task(void *arg)
{
    player_msg_t msg;

    while (1)
    {
        // Process incoming commands from web/buttons
        if (xQueueReceive(g_player_queue, &msg, 0) == pdTRUE)
        {
            switch (msg.cmd)
            {
            case CMD_PLAY:
                if (g_total_tracks > 0)
                    play_audio_file(g_playlist[g_current_track]);
                else
                    ESP_LOGW(TAG, "Playlist empty");
                break;

            case CMD_PAUSE:
                if (is_playing && pipeline)
                {
                    audio_pipeline_pause(pipeline);
                    is_paused = true;
                    g_player_state = PLAYER_STATE_PAUSED;
                    ESP_LOGI(TAG, "Paused");
                }
                break;

            case CMD_RESUME:
                if (is_paused && pipeline)
                {
                    audio_pipeline_resume(pipeline);
                    is_paused = false;
                    g_player_state = PLAYER_STATE_PLAYING;
                    ESP_LOGI(TAG, "Resumed");
                }
                break;

            case CMD_STOP:
                stop_current_playback();
                break;

            case CMD_NEXT:
                if (g_total_tracks > 0)
                {
                    g_current_track = (g_current_track + 1) % g_total_tracks;
                    play_audio_file(g_playlist[g_current_track]);
                }
                break;

            case CMD_PREV:
                if (g_total_tracks > 0)
                {
                    g_current_track = (g_current_track - 1 + g_total_tracks) % g_total_tracks;
                    play_audio_file(g_playlist[g_current_track]);
                }
                break;

            case CMD_AUTO_PLAY:
                g_auto_play = true;
                if (g_total_tracks > 0)
                    play_audio_file(g_playlist[0]);
                break;

            case CMD_STOP_AUTO:
                g_auto_play = false;
                stop_current_playback();
                break;

            case CMD_SET_VOLUME:
                g_volume = msg.value;
                if (board_handle && board_handle->audio_hal)
                    audio_hal_set_volume(board_handle->audio_hal, g_volume);
                ESP_LOGI(TAG, "Volume: %d", g_volume);
                break;

            case CMD_PLAY_FILE:
            {
                char full_path[MAX_FILENAME_LEN + 32];
                snprintf(full_path, sizeof(full_path), "%s/%s", AUDIO_DIR, msg.filename);
                stop_current_playback();
                // Find track index
                for (int i = 0; i < g_total_tracks; i++)
                {
                    if (strcmp(g_playlist[i], full_path) == 0)
                    {
                        g_current_track = i;
                        break;
                    }
                }
                play_audio_file(full_path);
                break;
            }

            default:
                break;
            }
        }

        // Handle pipeline events (non-blocking)
        if (evt && pipeline)
        {
            audio_event_iface_msg_t evt_msg;
            if (audio_event_iface_listen(evt, &evt_msg, pdMS_TO_TICKS(10)) == ESP_OK)
            {
                // Track finished
                if (evt_msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                    evt_msg.source == (void *)i2s_stream_writer &&
                    evt_msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
                    ((int)evt_msg.data == AEL_STATUS_STATE_STOPPED ||
                     (int)evt_msg.data == AEL_STATUS_STATE_FINISHED))
                {

                    ESP_LOGI(TAG, "Track finished");
                    stop_current_playback();

                    // Auto‑play next if enabled
                    if (g_auto_play && g_total_tracks > 0)
                    {
                        g_current_track = (g_current_track + 1) % g_total_tracks;
                        play_audio_file(g_playlist[g_current_track]);
                    }
                }
                // Music info received (optional)
                else if (evt_msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                         evt_msg.source == (void *)music_decoder &&
                         evt_msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
                {
                    audio_element_info_t info = {0};
                    audio_element_getinfo(music_decoder, &info);
                    audio_element_setinfo(i2s_stream_writer, &info);
                    i2s_stream_set_clk(i2s_stream_writer, info.sample_rates, info.bits, info.channels);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Public API
void audio_send_cmd(player_cmd_t cmd)
{
    player_msg_t msg = {.cmd = cmd};
    xQueueSend(g_player_queue, &msg, pdMS_TO_TICKS(100));
}

void audio_send_play_file(const char *filename)
{
    player_msg_t msg = {.cmd = CMD_PLAY_FILE};
    strncpy(msg.filename, filename, MAX_FILENAME_LEN - 1);
    xQueueSend(g_player_queue, &msg, pdMS_TO_TICKS(100));
}

void audio_send_volume(int vol)
{
    player_msg_t msg = {.cmd = CMD_SET_VOLUME, .value = vol};
    xQueueSend(g_player_queue, &msg, pdMS_TO_TICKS(100));
}

const char *player_state_str(player_state_t s)
{
    switch (s)
    {
    case PLAYER_STATE_PLAYING:
        return "playing";
    case PLAYER_STATE_PAUSED:
        return "paused";
    case PLAYER_STATE_STOPPED:
        return "stopped";
    default:
        return "idle";
    }
}