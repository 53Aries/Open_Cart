// Open_Cart
#include "scoreboard.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "scoreboard";

// ---------------------------------------------------------------------------
// Digi-Star EZ2000 scoreboard frame (manual section 5.6): <stx> A B B B C D <cr>, 8 bytes.
//   A   = '-' (negative), SPACE or a digit      BBB = digit or SPACE
//   C   = digit, SPACE, or '-' when a TR (tare/zero) command is active
//   D   = digit, or '-' when motion is active
// Confirmed by capturing a real EZ2000 (SCOREM 4: 9600 baud 7E1, ~9 frames/s): the six characters are the
// weight, right-justified with leading spaces, e.g. "<stx>    50<cr>"; a negative weight has '-' in A and
// 5 digits, e.g. "<stx>-16050<cr>". The motion flag replaces the last digit D with '-' (per the manual; the
// capture never showed it, so turn it off here if the display misbehaves). The TR flag is never sent.
// ---------------------------------------------------------------------------
static constexpr bool SB_SEND_MOTION_FLAG = true;

size_t scoreboard_format(char* buf, size_t cap, const Snapshot& snap) {
    if (!snap.ready || cap < 8) return 0;
    long w = std::lround(snap.display_lb);
    const bool neg = w < 0;
    if (neg) w = -w;
    const long max_mag = neg ? 99999 : 999999;
    if (w > max_mag) w = max_mag;

    char digits[8];
    if (neg) std::snprintf(digits, sizeof(digits), "-%5ld", w);   // A = '-', 5 digits right-justified
    else     std::snprintf(digits, sizeof(digits), "%6ld", w);    // 6 digits right-justified
    if (SB_SEND_MOTION_FLAG && snap.motion) digits[5] = '-';       // D = motion

    buf[0] = 0x02;                       // <stx>
    std::memcpy(buf + 1, digits, 6);     // A BBB C D
    buf[7] = 0x0D;                       // <cr>
    return 8;
}

static void scoreboard_task(void*) {
    Settings s;
    { AppLock l; s = g_app.settings; }
    TickType_t period = pdMS_TO_TICKS(s.sb_interval_ms);
    if (period < 1) period = 1;
    TickType_t last = xTaskGetTickCount();
    char buf[16];
    for (;;) {
        Snapshot snap = app_snapshot();
        size_t n = scoreboard_format(buf, sizeof(buf), snap);
        if (n) uart_write_bytes(static_cast<uart_port_t>(SB_UART_NUM), buf, n);
        vTaskDelayUntil(&last, period);
    }
}

void scoreboard_start() {
    Settings s;
    { AppLock l; s = g_app.settings; }

    const uart_port_t port = static_cast<uart_port_t>(SB_UART_NUM);
    uart_config_t cfg{};
    cfg.baud_rate = static_cast<int>(s.sb_baud);
    cfg.data_bits = UART_DATA_7_BITS;       // Digi-Star scoreboard: 7E1 (even parity), no handshaking
    cfg.parity = UART_PARITY_EVEN;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    // The scoreboard is optional: on any error log it and carry on, never restart the scale.
    esp_err_t err = uart_driver_install(port, 256, 256, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(port, &cfg);
    if (err == ESP_OK) {
        if (s.sb_use_rs485 && PIN_SB_DE >= 0) {
            err = uart_set_pin(port, PIN_SB_TX, PIN_SB_RX, PIN_SB_DE, UART_PIN_NO_CHANGE);
            if (err == ESP_OK) err = uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX);
        } else {
            err = uart_set_pin(port, PIN_SB_TX, PIN_SB_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scoreboard UART setup failed (%s): output off", esp_err_to_name(err));
        return;
    }

    // Second channel: open it with the same line settings and leave it idle.
    const uart_port_t port2 = static_cast<uart_port_t>(SB2_UART_NUM);
    err = uart_driver_install(port2, 256, 256, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(port2, &cfg);
    if (err == ESP_OK) err = uart_set_pin(port2, PIN_SB2_TX, PIN_SB2_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) ESP_LOGW(TAG, "second serial channel setup failed: %s", esp_err_to_name(err));

    if (xTaskCreate(scoreboard_task, "scoreboard", 3072, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "could not start the scoreboard task");
        return;
    }
    ESP_LOGW(TAG, "scoreboard output running (EZ2000 frame) at %u baud, 7E1",
             static_cast<unsigned>(s.sb_baud));
}
