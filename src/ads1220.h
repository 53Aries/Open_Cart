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

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

class ADS1220 {
public:
    esp_err_t begin();                 // init SPI, reset, write config, measure offset, start conversions
    bool data_ready() const;           // DRDY pin low
    esp_err_t read(int32_t* out);      // offset-corrected 24-bit result; call when data_ready()
    esp_err_t calibrate_offset(int samples = 20);   // re-measure ADC offset (~1 s, pauses readings)
    int32_t offset() const { return offset_; }      // last measured offset, in counts

private:
    esp_err_t command(uint8_t cmd);
    esp_err_t write_regs(const uint8_t* regs, size_t n);
    esp_err_t read_regs(uint8_t* regs, size_t n);
    esp_err_t read_raw(int32_t* out);
    bool wait_drdy(int timeout_ms) const;

    void* dev_ = nullptr;              // spi_device_handle_t
    int32_t offset_ = 0;               // ADC offset in counts (subtracted in read())
};
