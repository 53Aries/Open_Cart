// Open_Cart
#pragma once
// Status indicator on the DevKitC-1 on-board RGB LED (WS2812).
//   red         ADC SPI bus could not be set up (firmware/pin problem; no retries)
//   red blink   no ADC samples: chip not answering at start-up or stopped later (the firmware
//               keeps resetting it every second until it answers)
//   amber       warming up (filter filling)
//   green       stable and calibrated
//   cyan        stable, approximate (not fully calibrated)
//   blinking    in motion (green or cyan)

// Call once after the ADC init attempt. adc_ok = false latches the solid red state.
void status_led_start(bool adc_ok);
