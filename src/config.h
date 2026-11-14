// Open_Cart
#pragma once
// Pin map and compile-time defaults. See DESIGN.md. Proposal only: change to match your board.
// ESP32-S3 pins avoided: strapping GPIO3, 45, 46 (GPIO0 used only as the BOOT button),
// flash/PSRAM GPIO26-37 (octal PSRAM modules), native USB GPIO19/20, UART0 console GPIO43/44.

#ifndef ENABLE_ISOBUS_BUILD
#define ENABLE_ISOBUS_BUILD 0
#endif

// ADS1220 (SPI mode 1). SCLK/MOSI are swapped vs the SPI2 IOMUX defaults (routed via GPIO matrix;
// fine at 1 MHz) to avoid crossed traces on the PCB.
#define PIN_ADS_DRDY   15
#define PIN_ADS_MISO   16
#define PIN_ADS_SCLK   11
#define PIN_ADS_MOSI   12
#define PIN_ADS_CS     10
#define ADS_SPI_HZ     1000000

// Scoreboard serial
#define PIN_SB_TX      5      // MAX3232 DIN1 (pin 11)
#define PIN_SB_RX      4      // MAX3232 ROUT1 (pin 12)
#define PIN_SB_DE      8      // RS485 driver enable, -1 if unused
#define SB_UART_NUM    1
// Second MAX3232 channel (J905 pin 4 printer-style Tx via DOUT2, J905 pin 7 Com #2 Rx via RIN2).
// Wired "just in case"; the port is opened and left idle (TX rests at the mark level).
#define PIN_SB2_TX     6      // MAX3232 DIN2 (pin 10)
#define PIN_SB2_RX     7      // MAX3232 ROUT2 (pin 9)
#define SB2_UART_NUM   2

// CAN (TWAI)
#define PIN_CAN_TX     14
#define PIN_CAN_RX     13

// On-board WS2812 RGB LED. GPIO48 on this board (v1.0 style; v1.1 boards use GPIO38).
// PIN_STATUS_LED_ALT is a second pin to drive too, -1 = unused.
#define PIN_STATUS_LED     48
#define PIN_STATUS_LED_ALT -1

// Recovery input: hold LOW while powering up to force the Wi-Fi web page on for this boot
// (in case it was turned off in settings). GPIO0 is the BOOT button on the S3 DevKitC-1.
// It is a strapping pin, so use it only as a button to ground with the usual pull-up.
#define PIN_RECOVERY   0

// Defaults for settings that live in NVS
#define DEFAULT_AP_SSID_PREFIX "Open_Cart-"
#define DEFAULT_AP_PASSWORD    "changeme123"   // CHANGE THIS before using in the field
#define DEFAULT_DISPLAY_INCREMENT_LB 10.0
#define DEFAULT_FILTER_SAMPLES 20               // about 1 s at 20 SPS
#define DEFAULT_MOTION_THRESHOLD_LB 15.0        // spread in window that counts as "in motion"

// Load cell layout defaults (user-configurable on the web page). Cells are summed by a passive
// junction box, so full scale = number of cells x capacity per cell.
#define DEFAULT_CELL_COUNT        5
#define DEFAULT_CELL_CAPACITY_LB  31000.0
#define DEFAULT_CELL_MV_PER_V     1.55
#define ADC_GAIN                  128.0       // ADS1220 PGA gain (see ads1220.cpp)
#define BRIDGE_EXCITATION_V       5.0
#define CELL_RESISTANCE_OHM       350.0       // typical; only used for the excitation current estimate

// Unload detection (adjustable on the web page; these are the first-boot defaults)
#define UNLOAD_TICK_MS                100     // weight is sampled for the detector at 10 Hz
#define DEFAULT_UNLOAD_START_DROP_LB  100.0   // drop below the recent peak that means "unloading"
#define DEFAULT_UNLOAD_QUIET_S        5.0     // no further drop for this long = unload finished
#define DEFAULT_UNLOAD_MIN_LB         250.0   // smaller drops are not logged
#define UNLOAD_LOG_CAPACITY           1500   // records kept; oldest overwritten (see partitions.csv)

// ADS1220 health (see ads1220.h). Each temperature reading pauses the bridge readings for about 150 ms.
#define ADC_TEMP_INTERVAL_MS      30000   // chip temperature reading
// Re-measure the ADC offset (inputs shorted, ~1 s) when the chip temperature has moved this far
// since the last measurement. ADC offset drift is about 2-6 lb per degC at gain 128 (DESIGN.md).
// Skipped while the weight is in motion or an unload is in progress. 0 = never.
#define ADC_OFFSET_RECAL_C        3.0
#define ADC_STALL_MS              1000    // no sample for this long (normally 50 ms): reset the ADC
// No sample for this long: outputs stop showing the weight as live (scoreboard stops sending). Longer than
// the scale's own pauses (temperature 0.15 s, offset re-measurement ~1.2 s).
#define ADC_STALE_MS              2000

// Max calibration points kept (piecewise linear)
#define MAX_CAL_POINTS 8
