// Open_Cart
#include "unload_log.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <sys/time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "unload_log";
static const char* PART = "unloads";
static const char* NS = "log";

static nvs_handle_t g_h;
static bool g_open = false;
static uint32_t g_next_seq = 1;
static SemaphoreHandle_t g_lock;

static void key_for(uint32_t seq, char* k, size_t n) {
    std::snprintf(k, n, "r%u", (unsigned)(seq % UNLOAD_LOG_CAPACITY));
}

static char g_field[kFieldLen];
static char g_list[kMaxFields][kFieldLen];   // saved field names; an empty string is a free slot

static void list_save() {   // caller holds g_lock
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "flist", g_list, sizeof(g_list)) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static bool list_add(const char* clean) {   // caller holds g_lock; true if now in the list
    if (strcasecmp(clean, kDefaultField) == 0) return false;
    int free_slot = -1;
    for (size_t i = 0; i < kMaxFields; ++i) {
        if (!g_list[i][0]) { if (free_slot < 0) free_slot = static_cast<int>(i); }
        else if (strcasecmp(g_list[i], clean) == 0) return true;
    }
    if (free_slot < 0) return false;
    std::snprintf(g_list[free_slot], kFieldLen, "%s", clean);
    list_save();
    return true;
}

// Records written before the field name existed are shorter; they read back as the default field.
static constexpr size_t kOldRecordLen = offsetof(UnloadRecord, field);

static bool load_blob(const char* k, UnloadRecord* r) {
    size_t len = sizeof(*r);
    *r = UnloadRecord{};
    if (nvs_get_blob(g_h, k, r, &len) != ESP_OK) return false;
    if (len == kOldRecordLen) std::snprintf(r->field, sizeof(r->field), "%s", kDefaultField);
    else if (len != sizeof(*r)) return false;
    r->field[kFieldLen - 1] = 0;
    return true;
}

static bool read_slot(uint32_t seq, UnloadRecord* out) {
    char k[16];
    key_for(seq, k, sizeof(k));
    UnloadRecord r;
    if (!load_blob(k, &r) || r.seq != seq) return false;
    *out = r;
    return true;
}

// Drop characters that would break JSON/CSV/HTML and trim spaces. Empty becomes the default.
static void clean_field(const char* in, char* out) {
    size_t o = 0;
    for (; *in && o < kFieldLen - 1; ++in) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (c < 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '<' || c == '>') continue;
        if (c == ' ' && o == 0) continue;
        out[o++] = static_cast<char>(c);
    }
    out[o] = 0;
    for (size_t n = std::strlen(out); n > 0 && out[n - 1] == ' '; --n) out[n - 1] = 0;
    if (!out[0]) std::snprintf(out, kFieldLen, "%s", kDefaultField);
}

// Running total for the current field, summed over the records still in the ring.
static uint32_t g_ok_until = UNLOAD_LOG_CAPACITY;   // highest seq that may be written without asking (see unload_log_append)
static double g_ft_lb = 0;
static uint32_t g_ft_n = 0;

static void total_rescan() {   // caller holds g_lock (or runs at init)
    g_ft_lb = 0;
    g_ft_n = 0;
    if (!g_open) return;
    const uint32_t newest = g_next_seq - 1;
    const uint32_t count = newest > UNLOAD_LOG_CAPACITY ? UNLOAD_LOG_CAPACITY : newest;
    for (uint32_t s = newest - count + 1; count && s <= newest; ++s) {
        UnloadRecord r;
        if (read_slot(s, &r) && strcasecmp(r.field, g_field) == 0) { g_ft_lb += r.total_lb; ++g_ft_n; }
    }
}

bool unload_log_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    esp_err_t err = nvs_flash_init_partition(PART);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(PART);
        err = nvs_flash_init_partition(PART);
    }
    if (err == ESP_OK) err = nvs_open_from_partition(PART, NS, NVS_READWRITE, &g_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unload history unavailable: %s", esp_err_to_name(err));
        return false;
    }
    g_open = true;

    // The newest record is the one with the highest seq.
    uint32_t newest = 0;
    for (uint32_t i = 0; i < UNLOAD_LOG_CAPACITY; ++i) {
        char k[16];
        std::snprintf(k, sizeof(k), "r%u", (unsigned)i);
        UnloadRecord r;
        if (load_blob(k, &r) && r.seq > newest) newest = r.seq;
    }
    g_next_seq = newest + 1;
    g_ok_until = newest > UNLOAD_LOG_CAPACITY ? newest : UNLOAD_LOG_CAPACITY;   // already past full: ask on the next one

    std::snprintf(g_field, sizeof(g_field), "%s", kDefaultField);
    nvs_handle_t fh;
    if (nvs_open("scale", NVS_READONLY, &fh) == ESP_OK) {
        char buf[kFieldLen];
        size_t flen = sizeof(buf);
        if (nvs_get_str(fh, "field", buf, &flen) == ESP_OK) clean_field(buf, g_field);
        size_t llen = sizeof(g_list);
        // A list saved with a smaller kMaxFields is a prefix of this one; the rest stays zero.
        std::memset(g_list, 0, sizeof(g_list));
        if (nvs_get_blob(fh, "flist", g_list, &llen) != ESP_OK) std::memset(g_list, 0, sizeof(g_list));
        for (auto& e : g_list) e[kFieldLen - 1] = 0;
        nvs_close(fh);
    }
    total_rescan();

    nvs_stats_t st;
    if (nvs_get_stats(PART, &st) == ESP_OK)
        ESP_LOGI(TAG, "%u unloads stored, capacity %d; flash entries used %u of %u", (unsigned)unload_log_count(),
                 UNLOAD_LOG_CAPACITY, (unsigned)st.used_entries, (unsigned)st.total_entries);
    return true;
}

// Once the ring is full, the next unload would overwrite the oldest. Until the user chooses
// (download first, or let it roll over), new unloads wait in RAM. Writes are allowed up to
// seq g_ok_until; a choice moves it kRollPrompt further and writes whatever is waiting.
static constexpr size_t kPendingMax = 20;
static constexpr uint32_t kRollPrompt = 100;   // rollovers allowed before asking again
static UnloadRecord g_pend[kPendingMax];
static size_t g_npend = 0;

static bool write_locked(UnloadRecord r) {   // caller holds g_lock; assigns the seq
    r.seq = g_next_seq;
    char k[16];
    key_for(r.seq, k, sizeof(k));
    // The ring is about to overwrite the oldest record: take it out of the running total.
    UnloadRecord old;
    const bool had_old = r.seq > UNLOAD_LOG_CAPACITY && read_slot(r.seq - UNLOAD_LOG_CAPACITY, &old);
    esp_err_t err = nvs_set_blob(g_h, k, &r, sizeof(r));
    if (err == ESP_OK) err = nvs_commit(g_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    ++g_next_seq;
    if (had_old && strcasecmp(old.field, g_field) == 0 && g_ft_n) { g_ft_lb -= old.total_lb; --g_ft_n; }
    if (strcasecmp(r.field, g_field) == 0) { g_ft_lb += r.total_lb; ++g_ft_n; }
    return true;
}

static void flush_pending_locked() {
    for (size_t i = 0; i < g_npend; ++i) write_locked(g_pend[i]);
    g_npend = 0;
}

bool unload_log_append(double start_lb, double end_lb, double total_lb) {
    if (!g_open) return false;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    UnloadRecord r{};
    time_t now = time(nullptr);
    r.epoch = now > 1600000000 ? static_cast<uint32_t>(now) : 0;
    r.start_lb = static_cast<float>(start_lb);
    r.end_lb = static_cast<float>(end_lb);
    r.total_lb = static_cast<float>(total_lb);
    std::snprintf(r.field, sizeof(r.field), "%s", g_field);
    bool ok = true;
    if (g_next_seq > g_ok_until && g_npend < kPendingMax) {
        g_pend[g_npend++] = r;
        ESP_LOGW(TAG, "history full: unload held until the user chooses (%u waiting)", (unsigned)g_npend);
    } else {
        flush_pending_locked();   // only non-empty when the waiting list overflowed: roll over rather than lose it
        ok = write_locked(r);
    }
    xSemaphoreGive(g_lock);
    return ok;
}

void unload_log_continue() {
    if (!g_open) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    flush_pending_locked();
    g_ok_until = (g_next_seq - 1) + kRollPrompt;
    xSemaphoreGive(g_lock);
}

void unload_log_status(uint32_t* count, bool* need_choice, uint32_t* pending) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    *count = unload_log_count();
    *need_choice = g_open && g_next_seq > g_ok_until;
    *pending = static_cast<uint32_t>(g_npend);
    xSemaphoreGive(g_lock);
}

uint32_t unload_log_newest_seq() { return g_open ? g_next_seq - 1 : 0; }

size_t unload_log_count() {
    uint32_t n = unload_log_newest_seq();
    return n > UNLOAD_LOG_CAPACITY ? UNLOAD_LOG_CAPACITY : n;
}

bool unload_log_read(uint32_t seq, UnloadRecord* out) {
    if (!g_open || seq == 0) return false;
    return read_slot(seq, out);
}

void field_get(char* out, size_t cap) {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    std::snprintf(out, cap, "%s", g_field[0] ? g_field : kDefaultField);
    xSemaphoreGive(g_lock);
}

void field_set(const char* name) {
    char clean[kFieldLen];
    clean_field(name ? name : "", clean);
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const bool changed = strcasecmp(g_field, clean) != 0;
    std::snprintf(g_field, sizeof(g_field), "%s", clean);
    if (changed) total_rescan();
    list_add(clean);
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_str(h, "field", clean) == ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(g_lock);
}

void field_total(double* lb, uint32_t* count) {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    *lb = g_ft_lb < 0 ? 0 : g_ft_lb;
    *count = g_ft_n;
    xSemaphoreGive(g_lock);
}

size_t fields_list(char (*out)[kFieldLen], size_t max) {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    size_t n = 0;
    for (size_t i = 0; i < kMaxFields && n < max; ++i)
        if (g_list[i][0]) std::snprintf(out[n++], kFieldLen, "%s", g_list[i]);
    xSemaphoreGive(g_lock);
    return n;
}

bool field_add(const char* name) {
    char clean[kFieldLen];
    clean_field(name ? name : "", clean);
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    bool ok = list_add(clean);
    xSemaphoreGive(g_lock);
    return ok;
}

void field_remove(const char* name) {
    char clean[kFieldLen];
    clean_field(name ? name : "", clean);
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    for (auto& e : g_list)
        if (e[0] && strcasecmp(e, clean) == 0) { e[0] = 0; list_save(); }
    xSemaphoreGive(g_lock);
}

void unload_log_clear() {
    if (!g_open) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    nvs_erase_all(g_h);
    nvs_commit(g_h);
    g_next_seq = 1;
    g_ok_until = UNLOAD_LOG_CAPACITY;
    g_ft_lb = 0;
    g_ft_n = 0;
    flush_pending_locked();   // unloads that were waiting on a full history are newer than what was cleared
    xSemaphoreGive(g_lock);
}

// ---------------------------------------------------------------- thresholds ----
struct CfgBlob {
    uint32_t version;
    UnloadConfig cfg;
};
static constexpr uint32_t kCfgVersion = 1;

UnloadConfig unload_cfg_load() {
    UnloadConfig def = UnloadTracker::default_config();
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READONLY, &h) != ESP_OK) return def;
    CfgBlob b;
    size_t len = sizeof(b);
    esp_err_t err = nvs_get_blob(h, "unl", &b, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(b) || b.version != kCfgVersion) return def;
    return b.cfg;
}

bool unload_cfg_save(const UnloadConfig& c) {
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READWRITE, &h) != ESP_OK) return false;
    CfgBlob b{kCfgVersion, c};
    esp_err_t err = nvs_set_blob(h, "unl", &b, sizeof(b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// --------------------------------------------------------------------- clock ----
void clock_set_from_client(uint32_t epoch) {
    if (epoch < 1600000000) return;
    time_t now = time(nullptr);
    long diff = static_cast<long>(epoch) - static_cast<long>(now);
    if (now > 1600000000 && diff > -30 && diff < 30) return;
    timeval tv{static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "clock set from browser: %u", (unsigned)epoch);
}
