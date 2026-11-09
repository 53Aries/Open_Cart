// Open_Cart
#include "status_led.h"

#include <initializer_list>

#include "app.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char* TAG = "led";

static constexpr uint8_t kLevel = 24;            // WS2812s are very bright; keep it low
static constexpr uint32_t kTickMs = 100;
static constexpr uint32_t kStallTicks = 20;      // 2 s without a new sample = ADC stalled

static led_strip_handle_t g_strip, g_strip_alt;
static bool g_adc_ok = true;

static void set(uint8_t r, uint8_t g, uint8_t b) {
    for (led_strip_handle_t h : {g_strip, g_strip_alt}) {
        if (!h) continue;
        led_strip_set_pixel(h, 0, r * kLevel / 255, g * kLevel / 255, b * kLevel / 255);
        led_strip_refresh(h);
    }
}

static void led_task(void*) {
    uint32_t last_count = 0, stall = 0, tick = 0;
    for (;;) {
        const Snapshot s = app_snapshot();
        stall = (s.sample_count != last_count) ? 0 : stall + 1;
        last_count = s.sample_count;
        const bool blink_off = (tick++ / 5) & 1;   // 0.5 s on, 0.5 s off

        if (!g_adc_ok) {
            set(255, 0, 0);
        } else if (stall >= kStallTicks) {
            if (blink_off) set(0, 0, 0); else set(255, 0, 0);
        } else if (!s.ready) {
            set(255, 120, 0);
        } else if (s.motion && blink_off) {
            set(0, 0, 0);
        } else if (s.approximate || !s.has_cal) {
            set(0, 200, 200);
        } else {
            set(0, 255, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

static led_strip_handle_t strip_new(int gpio) {
    if (gpio < 0) return nullptr;
    led_strip_config_t strip{};
    strip.strip_gpio_num = gpio;
    strip.max_leds = 1;
    strip.led_model = LED_MODEL_WS2812;
    strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt{};
    rmt.resolution_hz = 10 * 1000 * 1000;
    led_strip_handle_t h = nullptr;
    if (led_strip_new_rmt_device(&strip, &rmt, &h) != ESP_OK) {
        ESP_LOGW(TAG, "status LED init failed (GPIO%d)", gpio);
        return nullptr;
    }
    return h;
}

void status_led_start(bool adc_ok) {
    g_adc_ok = adc_ok;
    g_strip = strip_new(PIN_STATUS_LED);
    g_strip_alt = strip_new(PIN_STATUS_LED_ALT);
    if (!g_strip && !g_strip_alt) return;
    ESP_LOGI(TAG, "status LED on GPIO%d", PIN_STATUS_LED);
    xTaskCreate(led_task, "led", 2560, nullptr, 2, nullptr);
}
