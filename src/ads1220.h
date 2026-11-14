// Open_Cart
#pragma once
// ADS1220 24-bit ADC driver for the ESP-IDF SPI master. SPI mode 1 (CPOL=0, CPHA=1).
// Config: AIN1/AIN2 differential, gain 128 with PGA on, 20 SPS continuous,
// 50/60 Hz rejection, reference = analog supply (ratiometric with the bridge excitation).
//
// Wire the bridge excitation and AVDD to the same 5 V rail. Register settings were checked
// against the ADS1220 datasheet (SBAS501D): SPI mode 1, VREF[1:0]=11 (AVDD as reference) is a
// valid ratiometric choice, and the bridge example in section 9.2.3 uses the same gain/rate.
//
// begin() also measures the ADC's own offset (inputs shorted to mid-supply, MUX = 1110b, as the
// datasheet recommends) and read() subtracts it. This removes the ADC offset (typ +/-4 uV, about
// 80 lb here) but not the load cell's zero balance, so zero/tare still work as before.
//
// read_temperature() briefly switches away from the bridge to the chip's temperature sensor
// (TS = 1, SBAS501D 8.3.13: register 0 is ignored and the internal reference is used meanwhile).
// The firmware uses it to re-measure the offset when the temperature moves (offset drifts with it).
//
// SPI clocks set DRDY high, so register reads must only happen right after read(), never while
// a sample is waiting (it would be lost). The sample task calls everything here in that order.

#include <cstddef>
#include <cstdint>
#include "driver/spi_master.h"
#include "esp_err.h"

class ADS1220 {
public:
    // Init SPI, then reset, write config, measure offset, start conversions. If only the chip part
    // fails, bus_ready() is still true and recover() can be retried later.
    esp_err_t begin();
    bool bus_ready() const { return dev_ != nullptr; }
    bool data_ready() const;           // DRDY pin low
    esp_err_t read(int32_t* out);      // offset-corrected 24-bit result; call when data_ready()
    esp_err_t calibrate_offset(int samples = 20);   // re-measure ADC offset (~1 s, pauses readings)
    int32_t offset() const { return offset_; }      // last measured offset, in counts

    // True if the registers still hold our configuration. A brown-out or ESD reset puts the chip
    // back to defaults (single-shot, AIN0/AIN1, gain 1): samples stop or turn to garbage.
    bool config_ok();
    // Reset and reconfigure the chip (including the offset measurement, ~1 s). After config_ok()
    // fails or samples stop. The SPI bus from begin() is reused.
    esp_err_t recover() { return configure(); }
    // Chip temperature (degC, +/-0.5 typ). Takes about 150 ms (three conversions at 20 SPS) and
    // then returns to the bridge; call right after read().
    // On an error the bridge configuration may be lost: check config_ok() or call recover().
    esp_err_t read_temperature(float* temp_c);

private:
    esp_err_t command(uint8_t cmd);
    esp_err_t write_regs(const uint8_t* regs, size_t n);   // n = 1..4, starting at register 0
    esp_err_t write_reg(uint8_t addr, uint8_t value);      // one register, addr 0..3
    esp_err_t read_regs(uint8_t* regs, size_t n);          // n = 1..4, starting at register 0
    esp_err_t read_raw(int32_t* out);
    bool wait_drdy(int timeout_ms) const;
    esp_err_t convert(int discard, int32_t* out);          // START, drop `discard` results, read the next
    esp_err_t configure();             // reset, write and verify the registers, measure the offset
    void release_bus();                // undo begin() after a failure

    static constexpr size_t kNumRegs = 4;
    spi_device_handle_t dev_ = nullptr;
    bool bus_ = false;                 // SPI bus initialized by begin()
    int32_t offset_ = 0;               // ADC offset in counts (subtracted in read())
};
