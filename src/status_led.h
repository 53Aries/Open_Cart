// Open_Cart
#pragma once
// Status indicator on the DevKitC-1 on-board RGB LED (WS2812).
//   red         ADC not found at start-up
//   red blink   ADC stopped delivering samples
//   amber       warming up (filter filling)
//   green       stable and calibrated
//   cyan        stable, approximate (not fully calibrated)
//   blinking    in motion (green or cyan)

// Call once after the ADC init attempt. adc_ok = false latches the red "ADC not found" state.
void status_led_start(bool adc_ok);
