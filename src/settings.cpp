// Open_Cart
#include "settings.h"

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "settings";
static const char* NS = "scale";
static const char* KEY = "cfg";

void settings_defaults(Settings* s) {
    std::memset(s, 0, sizeof(*s));
    s->version = SETTINGS_VERSION;
    s->enable_wifi_web = 1;
    s->enable_scoreboard = 0;      // off until the protocol is confirmed
    s->enable_isobus = 0;          // off until the build and display are validated

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(s->ap_ssid, sizeof(s->ap_ssid), "%s%02X%02X", DEFAULT_AP_SSID_PREFIX, mac[4], mac[5]);
    std::snprintf(s->ap_password, sizeof(s->ap_password), "%s", DEFAULT_AP_PASSWORD);

    s->display_increment_lb = DEFAULT_DISPLAY_INCREMENT_LB;
    s->filter_samples = DEFAULT_FILTER_SAMPLES;
    s->motion_threshold_lb = DEFAULT_MOTION_THRESHOLD_LB;
    s->tare_lb = 0.0;
    s->n_cal = 0;

    s->sb_baud = 1200;             // Digi-Star standard (9600 when the indicator's SCOREM = 4)
    s->sb_interval_ms = 250;
    s->sb_use_rs485 = 0;
}

bool settings_load(Settings* s) {
    settings_defaults(s);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    Settings tmp;
    size_t len = sizeof(tmp);
    esp_err_t err = nvs_get_blob(h, KEY, &tmp, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(tmp) || tmp.version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "no valid saved settings (err=%d len=%u), using defaults", (int)err, (unsigned)len);
        return false;
    }
    // Make sure strings are terminated and counts are sane even if the blob is damaged.
    tmp.ap_ssid[sizeof(tmp.ap_ssid) - 1] = 0;
    tmp.ap_password[sizeof(tmp.ap_password) - 1] = 0;
    if (tmp.n_cal > MAX_CAL_POINTS) tmp.n_cal = 0;
    if (tmp.filter_samples < 1 || tmp.filter_samples > Scale::kMaxWindow) tmp.filter_samples = DEFAULT_FILTER_SAMPLES;
    if (tmp.display_increment_lb <= 0) tmp.display_increment_lb = DEFAULT_DISPLAY_INCREMENT_LB;
    *s = tmp;
    return true;
}

bool settings_save(const Settings& s) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, KEY, &s, sizeof(s));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %d", (int)err);
    return err == ESP_OK;
}

// ------------------------------------------------------------- load cells ----
struct CellBlob {
    uint32_t version;
    CellConfig cfg;
};
static constexpr uint32_t kCellVersion = 1;

CellConfig cells_load() {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return CellConfig::defaults();
    CellBlob b;
    size_t len = sizeof(b);
    esp_err_t err = nvs_get_blob(h, "cells", &b, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(b) || b.version != kCellVersion || !b.cfg.valid()) return CellConfig::defaults();
    return b.cfg;
}

bool cells_save(const CellConfig& c) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    CellBlob b{kCellVersion, c};
    esp_err_t err = nvs_set_blob(h, "cells", &b, sizeof(b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
