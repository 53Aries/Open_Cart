<!-- Open_Cart -->
# ISOBUS Virtual Terminal setup

Status: **builds, never run against a VT or on hardware.** Treat the first test as bring-up.

The VT client (`src/isobus_vt.cpp`) is compiled only in the `s3_isobus` PlatformIO environment.
It is designed for John Deere Gen4 / Gen5 terminals (touch screen, 6+ soft keys), but uses only
VT version 3 objects, so other terminals should load it too.

## Screens

The same tabs as the Wi-Fi page, as a touch tab bar across the top of every screen.
Soft keys do the actions; **Prev tab / Next tab** are on every screen for knob or key use.

| Tab | Shows | Soft keys |
|---|---|---|
| Home | Weight (large), status (stable / motion / not calibrated), unload in progress or last unload, current field and its total, tare and gross, history-full warning | Tare, Clear tare |
| Unloads | History, newest first, 14 per page (number, field, pounds), page total, history-full warning | Newer, Older, Roll over |
| Fields | Saved fields with a cursor, `*` marks the current one, box to type a new field name | Up, Down, Use, Delete |
| Calibrate | Gross, tare, raw counts, calibration points, known-weight entry, and "Cart said" / "Certified scale" entries | Set point, Apply cmp, Clear cal, Tare (Clear tare is on Home; the soft key mask holds 6 keys) |
| Setup | Display rounding, filter samples, load cell count / capacity / mV/V, unload detection (start drop, quiet time, minimum) | (none) |

Notes:
- Certified-scale calibration: enter the weight the cart displayed and the weight the certified scale
  read for the same load, then press Apply cmp. The zero point must already be set, and the calibration
  must not have changed since the cart reading.
- Delete, Clear cal and Roll over need a second press within 5 seconds.
- Setup values are saved as soon as you enter them. Cell changes apply immediately, but
  re-calibrate if the real cells changed.
- Wi-Fi name and password, which outputs are enabled, firmware update and CSV download are
  web-page only.
- **Clock:** the scale has no RTC, so it listens for the tractor's Time/Date message (ISOBUS
  PGN 65254, which most displays broadcast) and sets its clock from it. Unloads are then
  time-stamped without a phone connected, and the web page and CSV get the same times. The
  Home tab shows the time and whether it is tractor local time or UTC; unload rows show
  `MM-DD HH:MM` (a trailing `Z` means UTC, used when the clock came from the browser or the
  tractor sent no local offset). The clock only changes if it is unset or off by more than 30 s.
  Check on the bench that your tractor actually sends the message.
- To add a field, touch the box at the bottom of the Fields tab, type a name, confirm. It is
  added to the list and the box clears. Press Use to make it current.

## Building

The object pool is generated, not hand drawn:

    python isobus/make_pool.py     # writes isobus/scale.iop and src/isobus_ids.h
    pio run -e s3_isobus

Both generated files are committed so a build works without Python. Re-run the script after
editing it, then rebuild. The script ends with a layout check of the pool it wrote; that checks
field layout and that every referenced object exists, not that a VT accepts it.

The build embeds `scale.iop` as binary data (never as text: a trailing NUL byte makes a VT reject
the pool). The pool is designed for a 480x480 data mask with 60x60 soft keys and the firmware asks
the VT to scale it for other sizes. Object IDs are in `src/isobus_ids.h` (generated).

## Before you trust any of this

- Test first on a VT simulator, then on a real display. Expect to adjust layout: font sizes,
  positions and the soft key label wrapping were chosen on paper.
- Check the real VT's font support. The weight uses a 48x64 font; if a terminal lacks it the VT
  substitutes another, which may overflow its box.
- A freshly changed pool is re-uploaded automatically (the stack names the pool by its hash),
  which takes a while on a real VT. Delete the stored pool on the VT if it shows an old layout.
- The code follows the AgIsoStack++ tutorials. The library's ESP32 tutorial targets a classic
  ESP32; confirm the TWAI plugin works on the ESP32-S3.
- The NAME uses manufacturer code 1407, which AgIsoStack++ documents for projects built on it.
  Read its README for the terms, and get your own code before selling units.
- Wiring: CAN transceiver on GPIO14 (TX) / GPIO13 (RX). Do not add 120 ohm termination when
  tapping an existing implement bus.
- Setup edits write to flash from the ISOBUS task; the VT and CAN keep running during the write.
