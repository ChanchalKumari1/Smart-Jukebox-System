/**
 * @file sd_card.c
 * @brief SD Card (SPI) Init and Playlist Scanner
 *
 * LyraT-Mini V1.2 SD SPI pins:
 *   MISO = GPIO2, MOSI = GPIO15, CLK = GPIO14, CS = GPIO13
 *
 * Supported audio extensions: .mp3 .wav .flac .aac .m4a
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "jukebox.h"

static const char *TAG = "SD_CARD";
static sdmmc_card_t *s_card = NULL;

/* ─── Extension check ───────────────────────────────────── */
static bool is_audio_file(const char *name)
{
    if (!name)
        return false;
    const char *ext = strrchr(name, '.');
    if (!ext)
        return false;

    /* case-insensitive compare for common extensions */
    char lower[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++)
    {
        lower[i] = (ext[i] >= 'A' && ext[i] <= 'Z')
                       ? (ext[i] + 32)
                       : ext[i];
    }

    return (strcmp(lower, ".mp3") == 0 ||
            strcmp(lower, ".wav") == 0 ||
            strcmp(lower, ".flac") == 0 ||
            strcmp(lower, ".aac") == 0 ||
            strcmp(lower, ".m4a") == 0);
}

/* ─── SD Init ────────────────────────────────────────────── */
esp_err_t sd_card_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 20000; /* 20 MHz — safe for most SD cards */

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_NUM_MOSI,
        .miso_io_num = SD_PIN_NUM_MISO,
        .sclk_io_num = SD_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_PIN_NUM_CS;
    slot_cfg.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host,
                                  &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount FS. Format the card as FAT32.");
        }
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);

    /* Create music directory if missing */
    struct stat st = {0};
    if (stat(AUDIO_DIR, &st) != 0)
    {
        mkdir(AUDIO_DIR, 0755);
        ESP_LOGI(TAG, "Created directory: %s", AUDIO_DIR);
    }

    return ESP_OK;
}

/* ─── Playlist Scanner ───────────────────────────────────── */
int sd_scan_playlist(void)
{
    memset(g_playlist, 0, sizeof(g_playlist));
    int count = 0;

    DIR *dir = opendir(AUDIO_DIR);
    if (!dir)
    {
        ESP_LOGW(TAG, "Cannot open dir %s", AUDIO_DIR);
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_PLAYLIST)
    {
        if (entry->d_type == DT_REG && is_audio_file(entry->d_name))
        {
            snprintf(g_playlist[count], MAX_FILENAME_LEN,
                     "%s/%s", AUDIO_DIR, entry->d_name);
            ESP_LOGI(TAG, "  [%02d] %s", count, entry->d_name);
            count++;
        }
    }
    closedir(dir);

    /* Simple alphabetical sort (bubble sort — small N) */
    for (int i = 0; i < count - 1; i++)
    {
        for (int j = i + 1; j < count; j++)
        {
            if (strcmp(g_playlist[i], g_playlist[j]) > 0)
            {
                char tmp[MAX_FILENAME_LEN];
                strcpy(tmp, g_playlist[i]);
                strcpy(g_playlist[i], g_playlist[j]);
                strcpy(g_playlist[j], tmp);
            }
        }
    }

    ESP_LOGI(TAG, "Playlist: %d tracks found", count);
    return count;
}

/* ─── File Existence Check ───────────────────────────────── */
bool sd_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

void ensure_music_dir_exists(void)
{
    struct stat st;
    if (stat(AUDIO_DIR, &st) != 0)
    {
        ESP_LOGI(TAG, "Creating directory %s", AUDIO_DIR);
        mkdir(AUDIO_DIR, 0755);
    }
    else if (!S_ISDIR(st.st_mode))
    {
        ESP_LOGE(TAG, "%s exists but is not a directory!", AUDIO_DIR);
    }
    else
    {
        ESP_LOGI(TAG, "Directory %s already exists", AUDIO_DIR);
    }
}