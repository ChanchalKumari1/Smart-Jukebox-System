/**
 * @file button_handler.c
 * @brief Physical Button Handler for ESP32 LyraT-Mini V1.2
 *
 * Button mapping:
 *   GPIO36 (REC)   → Play / Pause toggle
 *   GPIO39 (Vol+)  → Next track
 *   GPIO33 (Vol-)  → Previous track
 *   GPIO0  (BOOT)  → Stop / Toggle Auto-play
 *
 * Uses ESP-IDF GPIO ISR with debounce via FreeRTOS timer.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "jukebox.h"
#include "esp_timer.h"

static const char *TAG = "BTN_HANDLER";

#define DEBOUNCE_MS 150
#define LONG_PRESS_MS 800

typedef struct
{
    gpio_num_t pin;
    TimerHandle_t debounce_timer;
    int64_t press_time_us;
} btn_ctx_t;

static btn_ctx_t s_btns[4];

/* ─── Debounce callback — fires after DEBOUNCE_MS ────────── */
static void debounce_cb(TimerHandle_t timer)
{
    btn_ctx_t *btn = (btn_ctx_t *)pvTimerGetTimerID(timer);

    /* Read stable level (active LOW — buttons pulled up) */
    int level = gpio_get_level(btn->pin);
    if (level != 0)
        return; /* still bouncing or released */

    int64_t now = esp_timer_get_time();
    int64_t held_ms = (now - btn->press_time_us) / 1000;

    if (btn->pin == BTN_PLAY_PAUSE)
    {
        /* Short press → Play/Pause toggle */
        if (g_player_state == PLAYER_STATE_PLAYING ||
            g_player_state == PLAYER_STATE_PAUSED)
        {
            audio_send_cmd(CMD_PAUSE);
        }
        else
        {
            audio_send_cmd(CMD_PLAY);
        }
        ESP_LOGI(TAG, "BTN: Play/Pause");
    }
    else if (btn->pin == BTN_NEXT)
    {
        audio_send_cmd(CMD_NEXT);
        ESP_LOGI(TAG, "BTN: Next");
    }
    else if (btn->pin == BTN_PREV)
    {
        audio_send_cmd(CMD_PREV);
        ESP_LOGI(TAG, "BTN: Prev");
    }
    else if (btn->pin == BTN_BOOT)
    {
        if (held_ms > LONG_PRESS_MS)
        {
            /* Long press → toggle Auto-play */
            if (g_auto_play)
            {
                audio_send_cmd(CMD_STOP_AUTO);
                ESP_LOGI(TAG, "BTN: Auto-play OFF");
            }
            else
            {
                audio_send_cmd(CMD_AUTO_PLAY);
                ESP_LOGI(TAG, "BTN: Auto-play ON");
            }
        }
        else
        {
            /* Short press → Stop */
            audio_send_cmd(CMD_STOP);
            ESP_LOGI(TAG, "BTN: Stop");
        }
    }
}

/* ─── ISR — triggered on falling edge ───────────────────── */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    btn_ctx_t *btn = (btn_ctx_t *)arg;
    btn->press_time_us = esp_timer_get_time();

    BaseType_t higher_prio_task_woken = pdFALSE;
    xTimerResetFromISR(btn->debounce_timer, &higher_prio_task_woken);
    if (higher_prio_task_woken)
        portYIELD_FROM_ISR();
}

/* ─── Init one button ────────────────────────────────────── */
static void init_button(int idx, gpio_num_t pin)
{
    s_btns[idx].pin = pin;
    s_btns[idx].press_time_us = 0;

    /* GPIO config: input, pull-up, falling-edge interrupt */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);

    /* Debounce timer (one-shot) */
    char name[16];
    snprintf(name, sizeof(name), "dbnc_%d", pin);
    s_btns[idx].debounce_timer = xTimerCreate(
        name,
        pdMS_TO_TICKS(DEBOUNCE_MS),
        pdFALSE, /* one-shot */
        (void *)&s_btns[idx],
        debounce_cb);

    gpio_isr_handler_add(pin, gpio_isr_handler, (void *)&s_btns[idx]);
}

/* ─── Public Init ────────────────────────────────────────── */
esp_err_t button_handler_init(void)
{
    /* Install shared ISR service (priority 0 = default) */
    gpio_install_isr_service(0);

    init_button(0, BTN_PLAY_PAUSE);
    init_button(1, BTN_NEXT);
    init_button(2, BTN_PREV);
    init_button(3, BTN_BOOT);

    ESP_LOGI(TAG, "Buttons ready — REC=Play/Pause, Vol+=Next, Vol-=Prev, BOOT=Stop/Auto");
    return ESP_OK;
}
