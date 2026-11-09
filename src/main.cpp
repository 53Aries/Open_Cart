// Open_Cart: ESP32-S3 + ADS1220.
// Outputs (each switchable in settings): Wi-Fi web page, serial scoreboard, ISOBUS VT.

#include "ads1220.h"
#include "app.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "isobus_vt.h"
#include "nvs_flash.h"
#include "scoreboard.h"
#include "settings.h"
#include "status_led.h"
#include "unload_log.h"
#include "web_server.h"

static const char* TAG = "main";
static ADS1220 g_ads;

// Reads the ADC when DRDY goes low and feeds the filter.
static void sample_task(void*) {
    int fails = 0;
    for (;;) {
        if (g_ads.data_ready()) {
            int32_t raw = 0;
            if (g_ads.read(&raw) == ESP_OK) {
                AppLock l;
                g_app.scale.push_raw(raw);
                g_app.last_raw = raw;
                ++g_app.sample_count;
                fails = 0;
            } else if (++fails % 100 == 1) {
                ESP_LOGE(TAG, "ADC read failed (%d in a row)", fails);
            }
        }
        vTaskDelay(1);   // one tick (4 ms at 250 Hz); the ADC runs at 20 SPS
    }
}

// Feeds the unload detector at 10 Hz and logs finished unloads.
static void unload_task(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(UNLOAD_TICK_MS));
        UnloadTracker::Event ev;
        {
            AppLock l;
            if (!g_app.scale.ready() || !g_app.scale.has_calibration()) g_app.unload.invalidate();
            else ev = g_app.unload.update(g_app.scale.gross_lb());
        }
        if (ev.finished) {
            ESP_LOGI(TAG, "unload finished: %.0f lb (%.0f -> %.0f)", ev.total_lb, ev.start_lb, ev.end_lb);
            unload_log_append(ev.start_lb, ev.end_lb, ev.total_lb);   // flash write outside the lock
        }
    }
}

static bool recovery_held() {
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << PIN_RECOVERY;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(20));
    return gpio_get_level(static_cast<gpio_num_t>(PIN_RECOVERY)) == 0;
}

extern "C" void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    g_app.lock = xSemaphoreCreateMutex();
    settings_load(&g_app.settings);

    if (recovery_held()) {
        ESP_LOGW(TAG, "recovery input held: forcing Wi-Fi web page on for this boot");
        g_app.settings.enable_wifi_web = 1;
    }

    {
        AppLock l;
        const Settings& s = g_app.settings;
        g_app.scale.configure(s.filter_samples, s.display_increment_lb, s.motion_threshold_lb);
        g_app.scale.set_calibration(s.cal, s.n_cal);
        g_app.scale.set_tare_lb(s.tare_lb);
        g_app.cells = cells_load();
        g_app.scale.set_nominal_counts_per_lb(Scale::nominal_slope(g_app.cells));
    }

    unload_log_init();
    {
        AppLock l;
        g_app.unload.configure(unload_cfg_load());
    }
    UnloadRecord last;
    if (unload_log_read(unload_log_newest_seq(), &last)) {
        AppLock l;
        g_app.unload.set_last(last.total_lb);
    }

    err = g_ads.begin();
    if (err != ESP_OK) {
        // Keep the web page up so the problem is visible, but there will be no readings.
        ESP_LOGE(TAG, "ADS1220 init failed: %s", esp_err_to_name(err));
    } else {
        xTaskCreate(sample_task, "sample", 3072, nullptr, 6, nullptr);
    }

    status_led_start(err == ESP_OK);
    xTaskCreate(unload_task, "unload", 3072, nullptr, 3, nullptr);

    const Settings s = g_app.settings;
    if (s.enable_wifi_web) web_server_start();
    if (s.enable_scoreboard) scoreboard_start();
    if (s.enable_isobus) isobus_start();

    // Everything came up, so keep this firmware (otherwise the bootloader rolls back after a reset).
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "running: web=%u scoreboard=%u isobus=%u", s.enable_wifi_web, s.enable_scoreboard, s.enable_isobus);
}
