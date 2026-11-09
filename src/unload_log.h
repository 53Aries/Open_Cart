// Open_Cart
#pragma once
// Unload history kept in its own NVS partition ("unloads", see partitions.csv) as a ring of
// UNLOAD_LOG_CAPACITY records: when full, the oldest record is overwritten. Also stores the
// detector thresholds and keeps the wall-clock time the web page sends (there is no RTC).

#include <cstddef>
#include <cstdint>
#include "unload.h"

struct UnloadRecord {
    uint32_t seq;        // 1, 2, 3 ... (restarts at 1 after the history is cleared)
    uint32_t epoch;      // Unix time of the end of the unload, 0 if the clock was not set
    float start_lb;
    float end_lb;
    float total_lb;
    char field[32];      // name of the field being harvested when the unload ended
};

static constexpr size_t kFieldLen = 32;                   // including the NUL
static constexpr const char* kDefaultField = "default field";

// Current field name, kept across restarts. Stamped on each new unload. Empty or blank resets to the default.
void field_get(char* out, size_t cap);
void field_set(const char* name);          // also adds the name to the saved list if it is new

// Running total (lb) and number of unloads for the current field, from the records still in the history.
void field_total(double* lb, uint32_t* count);

// Saved field names the user can pick from (kept across restarts, at most kMaxFields).
static constexpr size_t kMaxFields = 50;
size_t fields_list(char (*out)[kFieldLen], size_t max);
bool field_add(const char* name);          // false if the list is full or the name is blank/default
void field_remove(const char* name);

bool unload_log_init();                                   // call after nvs_flash_init()
bool unload_log_append(double start_lb, double end_lb, double total_lb);   // may hold the unload if a choice is needed
// When the history is full the user must choose before the oldest unloads are overwritten.
// need_choice stays true (new unloads wait in RAM) until unload_log_continue() is called.
void unload_log_status(uint32_t* count, bool* need_choice, uint32_t* pending);
void unload_log_continue();                               // user chose to roll over: write waiting unloads, ask again after 100 more
size_t unload_log_count();                                // records currently stored
uint32_t unload_log_newest_seq();                         // 0 if empty
bool unload_log_read(uint32_t seq, UnloadRecord* out);    // false if overwritten or missing
void unload_log_clear();

UnloadConfig unload_cfg_load();
bool unload_cfg_save(const UnloadConfig& c);

// Set the system clock from the browser (only if unset or off by more than 30 s).
void clock_set_from_client(uint32_t epoch);
