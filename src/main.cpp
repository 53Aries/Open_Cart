// Open_Cart: ESP32-S3 + ADS1220.
// Outputs (each switchable in settings): Wi-Fi web page, serial scoreboard, ISOBUS VT.

#include "ads1220.h"
#include "app.h"
#include "config.h"
#include <cmath>
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

// Reset and reconfigure the ADC after it lost its registers (brown-out, ESD) or stopped converting.
// Without this the scale would stay dead (or read garbage) until the power was cycled.
static void recover_adc(const char* why) {
    static uint32_t attempts = 0;
    ++attempts;
    const bool log = attempts <= 5 || attempts % 60 == 0;   // an unplugged ADC retries every second
    if (log) ESP_LOGW(TAG, "ADC %s: resetting it (attempt %u)", why, static_cast<unsigned>(attempts));
    const esp_err_t err = g_ads.recover();
    if (err != ESP_OK) {
        if (log) ESP_LOGE(TAG, "ADC reset failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "ADC reconfigured and running again");
    attempts = 0;
    AppLock l;
    ++g_app.adc.recoveries;
    g_app.adc.offset = g_ads.offset();
    g_app.adc.offset_temp_valid = false;   // anchored at the next temperature reading
}

// Reads the chip temperature and re-measures the ADC offset if the temperature has moved.
// Called right after a sample was read (see ads1220.h). Pauses readings for 0.15 s (1.2 s with an
// offset measurement); the filter keeps the earlier samples meanwhile.
static void temperature_check() {
    float temp_c = 0;
    if (g_ads.read_temperature(&temp_c) != ESP_OK) {
        if (!g_ads.config_ok()) recover_adc("did not return to the bridge after a temperature reading");
        return;
    }
    bool recal;
    {
        AppLock l;
        AdcHealth& h = g_app.adc;
        h.temp_valid = true;
        h.temp_c = temp_c;
        if (!h.offset_temp_valid) {
            h.offset_temp_valid = true;
            h.offset_temp_c = temp_c;
        }
        // Not while the weight is changing: the correction shifts the reading by the drift it removes.
        recal = ADC_OFFSET_RECAL_C > 0 && std::fabs(temp_c - h.offset_temp_c) >= ADC_OFFSET_RECAL_C &&
                !g_app.scale.in_motion() && !g_app.unload.active();
    }
    if (!recal) return;

    const int32_t before = g_ads.offset();
    if (g_ads.calibrate_offset() != ESP_OK) {
        recover_adc("failed an offset measurement");
        return;
    }
    AppLock l;
    AdcHealth& h = g_app.adc;
    ESP_LOGI(TAG, "ADC temperature %.1f C (was %.1f C at the last offset measurement): offset %ld -> %ld counts",
             temp_c, h.offset_temp_c, static_cast<long>(before), static_cast<long>(g_ads.offset()));
    h.offset = g_ads.offset();
    h.offset_temp_c = temp_c;
    ++h.offset_recals;
}

// Reads the ADC when DRDY goes low and feeds the filter. Also owns every other use of the ADC
// (temperature, offset, recovery), so nothing else touches the SPI device.
static void sample_task(void*) {
    int fails = 0;
    TickType_t last_sample = xTaskGetTickCount();
    TickType_t next_temp = last_sample + pdMS_TO_TICKS(5000);   // first reading shortly after start-up
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        if (g_ads.data_ready()) {
            int32_t raw = 0;
            if (g_ads.read(&raw) != ESP_OK) {
                if (++fails % 100 == 1) ESP_LOGE(TAG, "ADC read failed (%d in a row)", fails);
            } else if (!g_ads.config_ok() && !g_ads.config_ok()) {
                // Registers changed (read twice to rule out one garbled transfer): this sample was
                // taken with the wrong setting, so it is dropped.
                fails = 0;
                recover_adc("lost its configuration");
                last_sample = xTaskGetTickCount();
            } else {
                fails = 0;
                last_sample = now;
                {
                    AppLock l;
                    g_app.scale.push_raw(raw);
                    g_app.last_raw = raw;
                    ++g_app.sample_count;
                    g_app.last_sample_tick = now;
                }
                if (static_cast<int32_t>(now - next_temp) >= 0) {
                    next_temp = now + pdMS_TO_TICKS(ADC_TEMP_INTERVAL_MS);
                    temperature_check();
                    last_sample = xTaskGetTickCount();
                }
            }
        } else if (now - last_sample > pdMS_TO_TICKS(ADC_STALL_MS)) {
            // A reset chip comes back in single-shot mode and never converts again by itself.
            recover_adc("stopped delivering samples");
            last_sample = xTaskGetTickCount();   // wait another ADC_STALL_MS before the next attempt
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
            if (!g_app.scale.has_calibration()) {
                // No weight in pounds at all: nothing to measure an unload against.
                if (g_app.unload.active()) ESP_LOGW(TAG, "calibration cleared during an unload: unload abandoned");
                g_app.unload.invalidate();
            } else if (g_app.scale.ready()) {
                ev = g_app.unload.update(g_app.scale.gross_lb());
            }
            // else: the filter is refilling after a settings change (about 1 s). Pause rather than
            // invalidate, so an unload in progress carries on and is still logged.
        }
        if (ev.finished) {
            ESP_LOGI(TAG, "unload finished: %.0f lb (%.0f -> %.0f)", ev.total_lb, ev.start_lb, ev.end_lb);
            unload_log_append(ev.start_lb, ev.end_lb, ev.total_lb);   // flash write outside the lock
        }
        unload_log_service();   // retries unloads held after a failed flash write
    }
}

static bool start_task(TaskFunction_t fn, const char* name, uint32_t stack, UBaseType_t prio) {
    if (xTaskCreate(fn, name, stack, nullptr, prio, nullptr) == pdPASS) return true;
    ESP_LOGE(TAG, "could not start task \"%s\" (out of memory)", name);
    return false;
}

static bool recovery_held() {
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << PIN_RECOVERY;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&io) != ESP_OK) return false;
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

    // Nothing can run without the locks; restarting is the only useful response.
    ESP_ERROR_CHECK(app_init() ? ESP_OK : ESP_ERR_NO_MEM);

    {
        AppLock l;
        settings_load(&g_app.settings);
    }
    if (recovery_held()) {
        ESP_LOGW(TAG, "recovery input held: forcing Wi-Fi web page on for this boot");
        AppLock l;
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

    const bool log_ok = unload_log_init();
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
    const bool adc_bus = g_ads.bus_ready();
    if (err != ESP_OK) {
        // If the SPI bus is up, the sample task retries the chip every ADC_STALL_MS until it answers
        // (red blinking LED meanwhile). Otherwise there will be no readings until a restart.
        ESP_LOGE(TAG, "ADS1220 init failed: %s%s", esp_err_to_name(err), adc_bus ? "; retrying in the background" : "");
    }
    if (adc_bus) {
        {
            AppLock l;
            g_app.adc.offset = g_ads.offset();
        }
        start_task(sample_task, "sample", 4096, 6);   // 4 KB: offset logging formats floats
    }

    status_led_start(adc_bus);
    const bool unload_ok = start_task(unload_task, "unload", 3072, 3);

    Settings s;
    {
        AppLock l;
        s = g_app.settings;
    }
    const bool web_ok = !s.enable_wifi_web || web_server_start();
    if (s.enable_scoreboard) scoreboard_start();
    if (s.enable_isobus) isobus_start();

    // Keep this firmware only if unload logging and the web page (the way to install another
    // update) came up. Otherwise, if this is a fresh update, the bootloader rolls back to the
    // previous firmware at the next reset; the scale keeps running meanwhile. On firmware that
    // is already marked valid this call does nothing.
    if (log_ok && unload_ok && web_ok) {
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(TAG, "start-up incomplete: firmware not marked valid");
    }

    ESP_LOGI(TAG, "running: web=%u scoreboard=%u isobus=%u", s.enable_wifi_web, s.enable_scoreboard, s.enable_isobus);
}
