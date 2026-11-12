// Open_Cart
#pragma once
// Unload history kept in its own NVS partition ("unloads", see partitions.csv) as a ring of
// UNLOAD_LOG_CAPACITY records: when full, the oldest record is overwritten. Also stores the
// detector thresholds and keeps the wall-clock time the web page sends (there is no RTC).

#include <cstddef>
#include <cstdint>
#include "unload.h"

static constexpr size_t kFieldLen = 32;                   // including the NUL
static constexpr const char* kDefaultField = "default field";

// Times before this (Sep 2020) mean the clock has not been set since power-up.
static constexpr uint32_t kClockValidEpoch = 1600000000;

// Stored in the "unloads" partition as raw bytes: never change the layout, or the history becomes unreadable.
struct UnloadRecord {
    uint32_t seq;        // 1, 2, 3 ... (restarts at 1 after the history is cleared)
    uint32_t epoch;      // Unix time of the end of the unload, 0 if the clock was not set
    float start_lb;
    float end_lb;
    float total_lb;
    char field[kFieldLen];   // name of the field being harvested when the unload ended
};
static_assert(sizeof(UnloadRecord) == 52, "UnloadRecord layout changed: saved unloads would be unreadable");

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

bool unload_log_init();                                   // call after nvs_flash_init(), before any other task starts
// Saves a finished unload. It is held in RAM instead if the history is full and the user has not chosen
// yet, or if the flash write fails; held unloads are written (in order) on the next append, continue or clear.
// False only if the unload could not be saved or held.
bool unload_log_append(double start_lb, double end_lb, double total_lb);
// When the history is full the user must choose before the oldest unloads are overwritten.
// need_choice stays true (new unloads wait in RAM) until unload_log_continue() is called.
void unload_log_status(uint32_t* count, bool* need_choice, uint32_t* pending);
void unload_log_continue();                               // user chose to roll over: write waiting unloads, ask again after 100 more
void unload_log_service();                                // call often (unload task): retries held unloads every few seconds
size_t unload_log_count();                                // records currently stored
uint32_t unload_log_newest_seq();                         // 0 if empty
bool unload_log_read(uint32_t seq, UnloadRecord* out);    // false if overwritten or missing
bool unload_log_clear();                                  // false if the flash erase failed (history kept)

UnloadConfig unload_cfg_load();
bool unload_cfg_save(const UnloadConfig& c);

// Set the system clock from the browser (only if unset or off by more than 30 s).
void clock_set_from_client(uint32_t epoch);
