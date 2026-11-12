// Open_Cart
#include "ads1220.h"

#include <cstring>
#include "config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ads1220";

// Commands
static constexpr uint8_t CMD_RESET = 0x06;
static constexpr uint8_t CMD_START = 0x08;   // START/SYNC
static constexpr uint8_t CMD_RDATA = 0x10;
static constexpr uint8_t CMD_RREG  = 0x20;   // | (reg << 2) | (n - 1)
static constexpr uint8_t CMD_WREG  = 0x40;   // | (reg << 2) | (n - 1)

// Register 0: MUX[7:4]=0011 (AINP=AIN1, AINN=AIN2), GAIN[3:1]=111 (128), PGA_BYPASS[0]=0
static constexpr uint8_t REG0 = (0x3 << 4) | (0x7 << 1) | 0x0;
// Register 1: DR[7:5]=000 (20 SPS), MODE[4:3]=00 (normal), CM[2]=1 (continuous), TS=0, BCS=0
static constexpr uint8_t REG1 = (0x0 << 5) | (0x0 << 3) | (1 << 2);
// Register 2: VREF[7:6]=11 (analog supply), 50/60[5:4]=01 (reject both), PSW=0, IDAC=000
static constexpr uint8_t REG2 = (0x3 << 6) | (0x1 << 4);
// Register 3: IDAC routing off, DRDYM=0
static constexpr uint8_t REG3 = 0x00;

esp_err_t ADS1220::begin() {
    gpio_config_t drdy{};
    drdy.pin_bit_mask = 1ULL << PIN_ADS_DRDY;
    drdy.mode = GPIO_MODE_INPUT;
    drdy.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t err = gpio_config(&drdy);
    if (err != ESP_OK) return err;

    spi_bus_config_t bus{};
    bus.mosi_io_num = PIN_ADS_MOSI;
    bus.miso_io_num = PIN_ADS_MISO;
    bus.sclk_io_num = PIN_ADS_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 8;
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    bus_ = true;

    spi_device_interface_config_t dev{};
    dev.mode = 1;                       // CPOL=0, CPHA=1
    dev.clock_speed_hz = ADS_SPI_HZ;
    dev.spics_io_num = PIN_ADS_CS;
    dev.queue_size = 2;
    err = spi_bus_add_device(SPI2_HOST, &dev, &dev_);
    if (err == ESP_OK) err = configure();
    if (err != ESP_OK) release_bus();
    return err;
}

esp_err_t ADS1220::configure() {
    esp_err_t err = command(CMD_RESET);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(5));       // reset takes a few hundred microseconds; be generous

    const uint8_t regs[kNumRegs] = {REG0, REG1, REG2, REG3};
    err = write_regs(regs, kNumRegs);
    if (err != ESP_OK) return err;

    uint8_t back[kNumRegs] = {};
    err = read_regs(back, kNumRegs);
    if (err != ESP_OK) return err;
    if (std::memcmp(regs, back, kNumRegs) != 0) {
        ESP_LOGE(TAG, "register readback mismatch: %02x %02x %02x %02x", back[0], back[1], back[2], back[3]);
        return ESP_FAIL;
    }

    // Measure the ADC offset first (also leaves the device converting continuously on the bridge).
    return calibrate_offset();
}

void ADS1220::release_bus() {
    if (dev_) {
        spi_bus_remove_device(dev_);
        dev_ = nullptr;
    }
    if (bus_) {
        spi_bus_free(SPI2_HOST);
        bus_ = false;
    }
}

bool ADS1220::wait_drdy(int timeout_ms) const {
    const TickType_t start = xTaskGetTickCount();
    const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() - start < limit) {
        if (data_ready()) return true;
        vTaskDelay(1);
    }
    return data_ready();
}

// Short both PGA inputs to mid-supply (MUX = 1110b, gain unchanged), average the readings, then
// switch back to the bridge inputs and restart continuous conversion. A conversion takes ~50 ms at
// 20 SPS and the digital filter settles in one cycle, so the first result is valid; one extra is
// discarded anyway.
esp_err_t ADS1220::calibrate_offset(int samples) {
    if (samples < 4) samples = 4;
    const uint8_t shorted = static_cast<uint8_t>((0xE << 4) | (REG0 & 0x0F));
    esp_err_t err = write_regs(&shorted, 1);
    if (err == ESP_OK) err = command(CMD_START);
    if (err != ESP_OK) return err;

    int64_t sum = 0;
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int i = 0; i < samples + 1; ++i) {
        if (!wait_drdy(200)) {
            ESP_LOGE(TAG, "offset calibration: no DRDY (check power and wiring)");
            return ESP_ERR_TIMEOUT;
        }
        int32_t v = 0;
        err = read_raw(&v);
        if (err != ESP_OK) return err;
        if (i == 0) continue;           // discard the first result
        sum += v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    offset_ = static_cast<int32_t>(sum / samples);
    ESP_LOGI(TAG, "ADC offset %ld counts (spread %ld counts p-p over %d samples)",
             static_cast<long>(offset_), static_cast<long>(hi - lo), samples);
    if (hi - lo > 2000)                 // expected noise is about 90 counts p-p at gain 128, 20 SPS
        ESP_LOGW(TAG, "offset readings are noisy; check the 5 V supply and layout");

    err = write_regs(&REG0, 1);         // back to AIN1/AIN2
    if (err != ESP_OK) return err;
    return command(CMD_START);          // restart continuous conversions on the bridge
}

bool ADS1220::data_ready() const { return gpio_get_level(static_cast<gpio_num_t>(PIN_ADS_DRDY)) == 0; }

esp_err_t ADS1220::command(uint8_t cmd) {
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    return spi_device_polling_transmit(dev_, &t);
}

esp_err_t ADS1220::write_regs(const uint8_t* regs, size_t n) {
    if (n < 1 || n > kNumRegs) return ESP_ERR_INVALID_ARG;
    uint8_t tx[kNumRegs + 1];
    tx[0] = static_cast<uint8_t>(CMD_WREG | ((n - 1) & 0x3));   // starting at register 0
    std::memcpy(tx + 1, regs, n);
    spi_transaction_t t{};
    t.length = 8 * (n + 1);
    t.tx_buffer = tx;
    return spi_device_polling_transmit(dev_, &t);
}

esp_err_t ADS1220::read_regs(uint8_t* regs, size_t n) {
    if (n < 1 || n > kNumRegs) return ESP_ERR_INVALID_ARG;
    uint8_t tx[kNumRegs + 1] = {};
    uint8_t rx[kNumRegs + 1] = {};
    tx[0] = static_cast<uint8_t>(CMD_RREG | ((n - 1) & 0x3));
    spi_transaction_t t{};
    t.length = 8 * (n + 1);
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_polling_transmit(dev_, &t);
    if (err == ESP_OK) std::memcpy(regs, rx + 1, n);
    return err;
}

esp_err_t ADS1220::read(int32_t* out) {
    int32_t v = 0;
    esp_err_t err = read_raw(&v);
    if (err == ESP_OK) *out = v - offset_;
    return err;
}

esp_err_t ADS1220::read_raw(int32_t* out) {
    uint8_t tx[4] = {CMD_RDATA, 0, 0, 0};
    uint8_t rx[4] = {};
    spi_transaction_t t{};
    t.length = 32;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_polling_transmit(dev_, &t);
    if (err != ESP_OK) return err;
    int32_t v = (static_cast<int32_t>(rx[1]) << 16) | (static_cast<int32_t>(rx[2]) << 8) | rx[3];
    if (v & 0x800000) v -= 0x1000000;   // sign-extend 24-bit two's complement
    *out = v;
    return ESP_OK;
}
