# Open_Cart: load cell scale reader (ESP32-S3 + ADS1220)

Reads a bridge-type load cell scale (designed around five Digi-Star 3.5 DB-30 EZ Mate cells
through a passive junction box) and shows the weight on:

- a Wi-Fi web page served by the ESP32 itself (default on; works on an iPad, no internet needed)
- a serial scoreboard output (default **off**; sends the Digi-Star EZ2000 frame at 1200 baud 7E1; the digit mapping is unverified)
- an ISOBUS Virtual Terminal client (default **off**; separate build, untested on hardware)

Each output is switched on or off from the web page and takes effect after a restart.
See `DESIGN.md` for the hardware (parts, power, pins, signal budget, bring-up order).

## What has and has not been tested

| Part | State |
|---|---|
| `scale.cpp` (filter, tare, calibration, motion) | Unit-tested on a host (`test/test_scale.cpp`) |
| `unload.cpp` (unload detector) | Behaviour checked with a Python port of the algorithm only. `test/test_unload.cpp` exists but has **not been run** (no host C++ compiler here) |
| Web helpers (form parsing, JSON escaping), old placeholder scoreboard frame (since replaced, not re-tested) | Tested on a host with stand-in ESP headers |
| All other files | Syntax-checked on a host against stand-in ESP headers only |
| ESP-IDF build, ADS1220 driver, Wi-Fi, web page in a browser, UART, CAN | **Not built or run on real hardware or the real SDK** |
| Digi-Star scoreboard protocol | **Unknown.** Waiting on Digi-Star |
| ISOBUS VT | **Built, untested.** Five tabs (Home, Unloads, Fields, Calibrate, Setup); needs a VT simulator / real display, see `isobus/README.md` |

Expect to fix compile errors and register-level issues on first build.

## Build and flash

```
pio run -e s3            # Wi-Fi web + scoreboard
pio run -e s3 -t upload
pio device monitor
```

Every `pio run` bumps the version in `version.txt` (1.3 -> 1.4 ...), shows it on the web page as
"Running version", and writes the Wi-Fi update image as `.pio/build/<env>/open_cart_v<version>.bin`
(see `scripts/version.py`). Upload that file from the page's Firmware update box. Commit `version.txt`
along with your changes so the next build continues from the right number.

ISOBUS build: `pio run -e s3_isobus` (see `isobus/README.md`).

Console and flashing use the DevKitC-1 "UART" USB port (UART0). To use the native "USB" port
instead, add `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` to `sdkconfig.defaults` and delete any
generated `sdkconfig.*` file.

## Host tests

```
g++ -std=c++17 -I src -o /tmp/test_scale test/test_scale.cpp src/scale.cpp && /tmp/test_scale
```

## First use

1. Join the Wi-Fi network `Open_Cart-XXXX` (password `changeme123`). **Change the password**
   in Settings straight away.
2. Open `http://opencart.local/` (or `http://192.168.4.1/` if the name does not resolve).
3. In **Load cells**, set the number of cells, the capacity of one cell, and its mV/V rating (defaults:
   5 x 31,000 lb at 1.55 mV/V). This sets the approximate reading and the signal budget.
4. Calibrate with the scale empty: enter `0`, press Set point. Then add known weights across the
   range you actually use (more points = better). Weights must increase with the signal; the
   page refuses a point that breaks that.
5. Or calibrate against a certified scale: in **Calibrate from a certified scale**, enter the weight the
   cart displayed for a load and the weight the certified scale read for the same load, then press Apply.
   The load does not need to be on the cart at that moment, but the zero point must already be set and
   the calibration must not have changed since the cart reading. Repeat at other loads to add points.
6. Tare as needed.

With only the zero point set, the page shows "approximate": it uses the datasheet slope
(about 10.7 counts per lb for 5 x 31,000 lb cells at 1.55 mV/V, gain 128).

## If you turn the Wi-Fi page off

Hold the recovery input (GPIO0, the BOOT button on the DevKitC-1) low while powering up.
That forces the web page on for that boot.

## Unload detection and history

The weight is sampled 10 times a second. An unload starts when the weight falls 100 lb below the
highest weight of the last 10 s (that peak is the "last known weight before the drop"). While it runs the
page shows **Unloading: N lb** (peak minus current weight). It ends when the weight has stopped
falling for 5 s (or rises back up), and the total becomes **Last unload** and is saved. Needs a
calibrated scale; tare does not matter.

The **Unloads** tab lists previous unloads (newest first, with totals) and has the three thresholds
(start drop, quiet time, minimum size). History is a ring of 1500 records in its own 256 KB flash
partition (`partitions.csv`); when full, the oldest is overwritten. The board has no clock, so the page
sends the browser's time when it loads; unloads logged before that show no date.

## Not for commerce

This is a hobby prototype. It is not legal-for-trade, and the web page has no login beyond
the Wi-Fi password.
