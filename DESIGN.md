# Open_Cart: Design Notes

ESP32-S3 + ADS1220 reading a 5-cell grain cart scale (Digi-Star 3.5 DB-30 EZ Mate cells,
31,000 lb each, 1.550 mV/V) through the passive junction box. Outputs, each switchable
at runtime:

1. Wi-Fi access point + web page (weight, tare, calibration, settings)
2. Serial scoreboard output (Agri-Tronix 2906-0409; firmware sends the Digi-Star EZ2000 frame, digit mapping unverified, see "Scoreboard")
3. ISOBUS Virtual Terminal client over CAN (AgIsoStack++)

The hardware sections below are still a design proposal (no PCB has been built from them). The firmware is built
and runs on a bare ESP32-S3 DevKit (no ADS1220, load cell or scoreboard attached). Only the Wi-Fi web page and the
Wi-Fi firmware update have been tried there; nothing involving the ADC, load cells, scoreboard or CAN has been
tested on hardware (see "Firmware" and README.md). Version, build and release workflow: CLAUDE.md.

## Block diagram

```
12 V in (9-14.4 V) -> TVS + reverse protection -+-> OKI-78SR-5/1.5-W36-C (5 V) -> DevKit 5V pin; DevKit's onboard 3V3 -> ESP32-S3, ADS1220 DVDD, RS232 xcvr, CAN xcvr VIO;  5 V net also -> CAN xcvr VCC (up to ~70-80 mA)
                                     +-> 33 ohm / 4.7 uF RC -> ADP7142 low-noise LDO 5 V (EXC, 200 mA) -> bridge excitation + ADS1220 AVDD/REF

Scoreboard display: its own separate, separately fused 12 V rail (J905 pin 5), not part of the tree above. Its ground is tied to this board's ground (needed for the RS-232 data signal).

Junction box (5 cells in parallel) --EXC+/EXC-/SIG+/SIG---> RC filter ---> ADS1220 (AIN1/AIN2, gain 128, ratiometric)
ADS1220 --SPI + DRDY--> ESP32-S3 --UART--> RS232 (or RS485) xcvr --> scoreboard
                                  --TWAI--> CAN xcvr (TCAN1042HV) --> ISOBUS
                                  --Wi-Fi AP--> iPad / phone browser
```

## Bill of materials (starting point)

| Block | Part | Notes |
|---|---|---|
| MCU | ESP32-S3-DevKitC-1 N16R8 (used as the final board, with its onboard 3.3 V regulator) | AgIsoStack++ docs target a classic ESP32 (denky32); S3 support is **unconfirmed** |
| ADC | ADS1220 breakout board (20 x 22 mm, RC filtering on AVDD/DVDD) | 24-bit, PGA up to 128, AVDD as reference gives ratiometric reading. See "ADS1220 breakout" below |
| Excitation | ADP7142 low-noise 5 V LDO (200 mA, 40 V max input), fed from the protected 12 V through a 33 ohm / 4.7 uF RC | Five 350 ohm cells in parallel is ~70 ohm, so ~70 mA at 5 V. **Measure the real bridge resistance first.** No pre-regulator: see "Power rails" for LDO heat and the RC's voltage drop |
| 12 V front end | Order: fuse -> reverse-polarity protection -> TVS -> bulk caps -> branches. Fuse: 1.5 A fast-acting, rated 32 V DC or more (automotive mini blade or SMD). TVS: SMBJ15A (SMCJ15A if more surge energy is wanted), unidirectional, after the reverse protection so a reversed battery cannot short through it. Reverse protection (chosen): Vishay SQD50P04-13L_T4GE3 P-FET (TO-252, -40 V, 13 mOhm, ISM/IDM -200 A, AEC-Q101). It is a 2-lead symbol: pin 1 = G, pin 2 = S, tab/pin 3 = D. Drain (tab) to the fused battery side, source to the protected 12 V rail. Gate: 47 kOhm to GND, plus a 12 V Zener (Vishay BZX84C12-E3-18, SOT-23) gate-to-source with cathode on the source/rail and anode on the gate node. Verify the Zener's SOT-23 pin mapping against its datasheet | Load on this rail is about 0.4 A (OKI branch about 0.3 A at 9 V, excitation about 0.07 A). Display is on its own rail. Check fuse I2t against the capacitor inrush (about 222 uF at the OKI input) and the OKI's 0.16 A2s inrush spec. TVS does not cover a full alternator load dump |
| 5 V logic supply | Murata OKI-78SR-5/1.5-W36-C (5 V, 1.5 A, 7-36 V in, 90.5 %, -40 to 85 C) | Feeds the DevKit's 5V pin; the DevKit's onboard 3.3 V regulator supplies the ESP32 and the small 3.3 V loads (ADS1220 DVDD, MAX3232, TCAN1042HV VIO pin; the CAN transceiver's VCC is on the 5 V rail). No separate 3.3 V LDO on our board. Datasheet (5 V model): output 4.8-5.2 V (+/-3 % total band), ripple/noise 50 mVp-p typ / 75 max (20 MHz BW), fixed 500 kHz switching, max capacitive load 300 uF low-ESR, no minimum load, hiccup short-circuit protection. Do **not** use it as the excitation or reference source. 10-22 uF ceramic plus 0.1 uF at its output is fine. **No reverse-polarity protection; datasheet recommends a 2 A fast-blow external fuse.** At its input use 2 x 100 uF low-ESR (35 V or higher; ESR under 700 mohm at 100 kHz), matching the datasheet's reflected-ripple test (15 mA p-p), plus 22 uF and 0.1 uF ceramic at the pins. Check the fast-blow fuse's I2t against the capacitor inrush Keep it away from the ADC inputs |
| CAN | **TI TCAN1042HVDR** (SOIC-8, 2 Mbps CAN FD, "H" = **+/-70 V bus fault protection**, "V" = VIO level shifting). VCC 4.5-5.5 V from the 5 V rail, **VIO (pin 5) 3.0-5.5 V from the DevKit's 3V3**, 0.1 uF at each, 4.7 uF bulk near VCC. Pin 8 = STB with an **internal pull-up: a floating STB puts the chip in standby, so tie STB to GND (about 10 kohm)**. Not the HGV (5 Mbps not needed at 250 kbit/s). Replaces the TCAN332 (see CAN notes for why) | Absolute max on CANH/CANL is +/-70 V, receiver common-mode +/-30 V, 16 kV HBM, 15 kV IEC unpowered / 8 kV powered, 4 kV EFT; TXD dominant timeout; bus is high impedance when unpowered. Dominant supply current up to 70 mA (80 mA high load; 180 mA max in a bus-fault test), on the 5 V rail. TI marks the plain TCAN1042HD tube obsolete, use the HVDR tape-and-reel. No termination unless this node is at the end of the bus (see below). Still add a CAN TVS (see CAN notes). Datasheet SLLES7D |
| RS232 | MAX3232EIDR (TI, SOIC-16, -40 to 85 C, ESD-protected) or MAX3232EE (ADI) / SP3232E | 0.1 uF charge-pump caps at 3.3 V, **1 uF VCC bypass**, 100 kohm pull-ups on DIN1/DIN2, series resistor plus clamp on DOUT1/DOUT2 (see "Scoreboard"). Swap for MAX3485/SP3485 if the scoreboard is RS485. Signal levels on the J905 are **not confirmed** (see "Scoreboard") |
| Protection | ESD/TVS on load cell, scoreboard and CAN lines | Cables run outside the enclosure |
| Input filter | AIN1/AIN2: RF1 = RF2 = 1 kohm 1 % (0603; use 470 ohm if offset drift is a problem). **CDIF = 100 nF C0G/NP0** (50 V, 1206) across AIN1-AIN2. CCM1 = CCM2 = 10 nF C0G (0603) from each input to AGND | **CDIF must be C0G/NP0, not X7R** (datasheet 9.4.1: X7R has voltage-coefficient and microphonic error that matters at gain 128). Keep CDIF >= 10x CCM. Corners: differential about 760 Hz, common-mode about 16 kHz. REFP0/REFN0: no parts, leave floating (VREF = AVDD). 100 nF at AVDD. Leave AIN3/REFN1 floating. Tune later; gain 128 is sensitive to layout |

## Pin map (as in src/config.h, ESP32-S3)

Avoided on the S3: strapping GPIO3/45/46 (GPIO0 is used only as the BOOT/recovery button), flash/PSRAM
GPIO26-37 (octal PSRAM variants), native USB GPIO19/20 and UART0 console GPIO43/44. The ADS1220 uses GPIO10-12 and 15-16 on
SPI2, with SCLK and MOSI swapped from the IOMUX defaults (routed via the GPIO matrix, fine at 1 MHz) so the PCB
traces don't cross.

| Function | GPIO |
|---|---|
| ADS1220 DRDY | 15 |
| ADS1220 DOUT/MISO | 16 |
| ADS1220 SCLK | 11 |
| ADS1220 DIN/MOSI | 12 |
| ADS1220 CS | 10 |
| Scoreboard UART TX | 5 |
| Scoreboard UART RX | 4 |
| Scoreboard UART2 TX (MAX3232 DIN2, J905 pin 4) | 6 |
| Scoreboard UART2 RX (MAX3232 ROUT2, J905 pin 7) | 7 |
| RS485 DE/RE (only if RS485) | 8 |
| TWAI (CAN) TX | 14 |
| TWAI (CAN) RX | 13 |
| Recovery input (BOOT button, forces Wi-Fi page on) | 0 (strapping pin, used only as a pulled-up button) |
| Status LED (DevKit's onboard WS2812; v1.1 boards use GPIO38) | 48 |
| Console / flashing (DevKitC-1 UART port, UART0) | 43 / 44 |

## Power rails

- 12 V in -> reverse-polarity protection, fuse, TVS (SMBJ15A class clamps near 24 V, inside the OKI's 36 V
  maximum). The protected 12 V then splits into two paths (the scoreboard display is on its own separate 12 V rail):
  1. **Logic:** OKI-78SR-5/1.5-W36-C -> 5 V -> ESP32-S3 DevKit 5V pin. The DevKit's onboard 3.3 V regulator
     (3V3 pin) powers the ESP32 and the small external 3.3 V loads: ADS1220 DVDD (under 1 mA), MAX3232
     (about 1 mA plus output load), and the TCAN1042HV's VIO pin (about 0.1-0.3 mA). The CAN transceiver's VCC is on the 5 V rail instead (see below).
     The OKI's 5 V rail loads are the DevKit plus the TCAN1042HV VCC (typically 40-45 mA when transmitting dominant, 70-80 mA max, about 1.5 mA recessive), which adds about 0.05 A to the OKI input branch at 9 V in. Still well inside 1.5 A.
     - The OKI has no reverse-polarity protection and its datasheet recommends a 2 A fast-blow fuse. The shared
       input protection covers reverse polarity, and the 1.5 A fast-acting main fuse is under the OKI's 2 A
       limit, so no separate OKI fuse is needed now that the display is on its own rail. The 500 kHz switching ripple is attenuated about 54 dB
       by the excitation path's 33 ohm / 4.7 uF RC alone, with the ADP7142's PSRR on top of that; the remaining risk is layout. The input range is 9-14.4 V, so the OKI's 7 V minimum has margin.
     - The DevKit's 3.3 V regulator is an AMS1117-3.3 (per the user; about 1 A, dropout about 1.1 V at
       800 mA, figures from memory). It covers the ESP32's Wi-Fi peaks (about 350-500 mA) plus the external 3.3 V
       loads (about 30 mA combined). At 5 V in it dissipates about 0.6 W at a 0.35 A peak (about 0.17 W at
       100 mA), warm in a SOT-223 with little copper, so check temperature in a hot enclosure. It is not a
       low-noise part, so keep it off the excitation/reference path.
     - **Check before building:** the DevKit schematic for how the 5V pin and USB 5 V are isolated, so USB and
       the OKI are never back-feeding each other.
     - Put local decoupling (and a ferrite or RC) on the ADS1220 DVDD tap, since the shared 3V3 rail carries
       Wi-Fi burst noise.
  2. **Excitation:** a separate path, not the OKI. The protected 12 V (after the P-FET) -> 33 ohm / 4.7 uF RC
     -> ADP7142 low-noise 5 V LDO (200 mA). There is no pre-regulator, so the RC resistor and the LDO
     drop the whole 12 V-to-5 V difference between them.
     - **RC capacitor:** 4.7 uF X7R, 50 V, 1206/1210, plus 0.1 uF, both at the LDO input pin. 0.1 uF alone puts
       the corner at about 48 kHz (only about 20 dB at the OKI's 500 kHz); 4.7 uF puts it near 1 kHz (about
       54 dB). Use 50 V because the TVS lets spikes through near 24 V, and a 25 V part keeps much less of its
       capacitance with 12 V across it.
     - **LDO pins and package (ADP7142ARDZ-5.0, SOIC-8 with exposed pad, datasheet Rev. K):** pins 1 AND 2 are
       VOUT, 3 SENSE/ADJ (to VOUT), 4 GND, 5 EN (to VIN, rated 44 V), 6 SS, 7 AND 8 are VIN. Exposed pad is GND
       internally; connect it to the ground plane. Check in the PCB that pads 2, 7 and the exposed pad all
       have nets.
     - **LDO caps:** datasheet needs at least 2.2 uF on the output (ESR <= 0.3 ohm) and at least 1.5 uF effective
       after DC bias, temperature and tolerance. A good 2.2 uF part passes, but use 4.7 uF X7R 16-25 V 0805/1206
       for margin. The datasheet says to match input capacitance to output when the output is above 2.2 uF,
       which the 4.7 uF input cap does (use 10 uF on both if going to 10 uF out).
     - **Soft start:** SS open gives about 380 us to 90 %, which is close to the ADS1220's limit (supplies
       slower than 1 V per 50 us). Fit 10 nF on SS to GND: t = 380 us + 0.6 V x Css / 1.15 uA, about 5.6 ms.
       This also helps DVDD come up before AVDD. Never ground SS directly.
     - **LDO heat (SOIC, datasheet Table 6):** at 14.4 V in (alternator running) and 70 mA, the LDO dissipates
       about 0.5 W after the 33 ohm drop. theta-JA is 89 C/W with 500 mm^2 of copper, 78 C/W with 1000 mm^2,
       63 C/W with 6400 mm^2, so the junction runs 32-45 C above ambient; the 125 C limit is reached at about
       80 C ambient with 500 mm^2 and 86 C with 1000 mm^2. Aim for at least 1000 mm^2 of GND copper on the
       exposed pad with thermal vias to an inner or bottom plane.
     - **Input headroom:** the 33 ohm drops 33 x I. At the 9 V minimum input the LDO sees 9 - 33 x I and needs
       5 V plus its dropout (a few hundred mV, check the datasheet). That caps excitation current at about
       110 mA: 70 mA (five 350 ohm cells) leaves about 6.7 V, fine; 115 mA (eight) leaves about 5.2 V, at the
       edge. For more cells, use 700/1000 ohm cells or a smaller resistor (less ripple filtering).
     - **Heat:** the resistor takes 33 x I^2 and the LDO takes the rest. At 70 mA and 14.4 V in: resistor
       about 0.16 W, LDO about (14.4 - 2.3 - 5) x 0.07 = 0.5 W. Use the LDO package with an exposed pad and
       copper under it, a 0.25 W or larger resistor, and check temperature in a hot enclosure.
  (Scoreboard display: not on this input. It gets its own separate 12 V rail with its own fuse and TVS, sized
  for the display's current and voltage range once the manufacturer answers. Its ground must be common with
  this board's ground for the RS-232 signal.)
- The excitation LDO feeds the bridge excitation and the breakout's AVDD pad from one short, tight node. No
  series resistor in the excitation path.
- ESP32 and the RF receiver run from the OKI/3.3 V path, not from the ADC's LDO.
- Do not rely on a buck plus RC alone: the signal is microvolts and switching ripple does not cancel cleanly in
  a ratiometric reading. On the bench, scope the LDO output (short ground lead), aim for well under 1 mV of
  ripple, then check noise in counts at 20 SPS.

## ADS1220 breakout

- Signal pair on AIN1 (positive) / AIN2 (negative), MUX = 0011b; AIN0 and AIN3 unused and floating; AVDD = 5 V excitation rail (the reference), DVDD = 3.3 V. REFP0/REFN0 unused.
- The board ties AVDD and DVDD together with a resistor. **Remove it**, then confirm AVDD-DVDD reads open
  with the board unpowered. Otherwise 5 V reaches the ESP32's 3.3 V I/O. Check AGND-DGND are tied, and that
  CLK is grounded (internal oscillator).
- Checked on the actual board: the ~0.75 readings from AVDD to the AIN and REF pads were the chip's protection
  diodes (open with the meter leads swapped), so nothing is tied. AVDD-AGND is open. REFP0/REFN0 are free; the
  firmware still uses the internal AVDD reference (`VREF`=analog supply) and leaves them unconnected.
- The board only filters the supplies. The load cell input filter (see BOM) still goes on the main PCB.
- Cabling is Digi-Star's own 5-wire cable and junction box: four bridge wires (+EXC, -EXC, +SIG, -SIG) plus a
  shield (existing 4-pin connector, 20-30 ft run to the junction box). No sense wires, so AVDD is the reference and
  cable drop appears as a gain error (see "Excitation cable drop"). Wire colours vary by model; confirm by continuity. Ground
  the shield at the electronics end only.
- Datasheet (SBAS501D) checks for the main PCB: 47 ohm series resistors on CS, SCLK, DIN, DOUT/DRDY and
  DRDY; 100 nF at AVDD and at DVDD, close to the pins; DVDD should ramp before or with AVDD (supplies
  must ramp monotonic, slower than 1 V per 50 us); leave AIN3/REFN1 unconnected (an internal switch can tie
  it to AVSS); CLK to DGND; CS to the ESP32 (firmware drives it). Use C0G for the differential input
  capacitor, keep input-filter resistors under 1 kohm, and keep any filtering on the excitation light so the
  measurement stays ratiometric.
- Firmware: SPI mode 1 at 1 MHz, registers 00h=0Eh, 01h=04h, 02h=D0h, 03h=00h (decoded against the
  datasheet). `begin()` measures the ADC offset (MUX 1110b, inputs shorted to mid-supply, 20 readings),
  and `read()` subtracts it. This removes the ADC offset (typ +/-4 uV, about 80 lb) but not the load cell
  zero, so tare still applies. Offset drift is 0.08-0.3 uV/degC (about 2-6 lb per degC); call
  `calibrate_offset()` again if you want to re-zero the ADC. Any calibration saved before this change would be
  off by the board offset (none exist yet). Builds clean for the `s3` env, but has never run with an ADS1220 attached. The register setup also enables 50/60 Hz rejection (reg 2 = D0h). If the
  register readback does not match, init fails and the status LED goes solid red (see "Firmware").

## Scoreboard (Agri-Tronix 2906-0409, 4" LED)

Source: reply from the vendor. It is a repeater for a Digi-Star indicator's J905 serial port, so the specs
below are from the **J905 (indicator) side**. We replace the indicator, so our output takes the place of the
indicator's Com 1 TX.

J905 pinout (8-pin AMP). **Corrected** from Digi-Star's J905 connector signal table (the earlier version of
this table, from a secondary source, listed pin 1 as +5 VDC and pin 8 as a second ground; that was wrong):

| Pin | Signal | Wiring on our board |
|---|---|---|
| 1 | 20 mA current loop (+) | **No connection.** The display is RS-232, so the loop is not used. Never put 5 V on this pin |
| 2 | Com #1 Out (Tx), DDL & computer | **Our scoreboard data output.** MAX3232 DOUT1 (pin 14), fed by GPIO5 |
| 3 | Com #1 In (Rx), DDL & computer | MAX3232 RIN1 (pin 13), to GPIO4 via ROUT1 (pin 12). Receive-only, safe to wire |
| 4 | Com #1 Out (Tx), printer | MAX3232 DOUT2 (pin 7), fed by GPIO6 via DIN2 (pin 10). Idle (mark level) unless firmware sends on it |
| 5 | +12 VDC | Display's own separately fused 12 V rail |
| 6 | Gnd (any Com device) | Board ground |
| 7 | Com #2 In (Rx) | MAX3232 RIN2 (pin 8), to GPIO7 via ROUT2 (pin 9) |
| 8 | 20 mA loop (-), is GND | Board ground |

MAX3232 wiring (3.3 V, 0.1 uF charge-pump caps; GPIO from `config.h`): GPIO5 -> DIN1 (11) -> DOUT1 (14) -> J905 2;
J905 3 -> RIN1 (13) -> ROUT1 (12) -> GPIO4; GPIO6 -> DIN2 (10) -> DOUT2 (7) -> J905 4;
J905 7 -> RIN2 (8) -> ROUT2 (9) -> GPIO7. Data lines only: the J905 has no RTS/CTS. Only channel 1 TX carries
the scoreboard data; UART2 is opened by the firmware and left idle.

MAX3232E datasheet checks (SLLS664E):
- **Part:** order MAX3232EIDR (SOIC-16, -40 to 85 C, active). The non-R "EID" is obsolete; the EC parts only reach 0 to 70 C.
- **VCC bypass: 1 uF minimum** (plus 0.1 uF if wanted) between pins 16 and 15. The D/DB/PW packages need it to meet
  the IEC 61000-4-2 rating (+/-8 kV contact, +/-15 kV air). Charge-pump caps: all four 0.1 uF at 3.3 V +/-0.3 V
  (0.47 uF for C2-C4 if the 3 V to 5.5 V range is wanted). Keep C1/C2 traces short.
- **DIN1 and DIN2 must never float.** The ESP32 GPIO5/6 are high impedance during reset and boot, so add a 100 kohm
  pull-up to 3V3 on each. High DIN gives the idle (mark) level of about -5.4 V on DOUT, the correct quiet line.
- **Unpowered:** the chip may be connected to an active RS-232 device (display rail live, MAX3232 off).
- **Fault ratings:** DOUT absolute max +/-13.2 V; V+ max 7 V, V- -7 V to +0.3 V; RIN +/-25 V (about 5 kohm
  pulldown, so pins 3 and 7 survive 12 V or 14.4 V). TI only states protection against shorts to ground
  (IOS +/-35 mA typ, +/-60 mA max); behaviour with a driver output forced above the rails is not specified.
- **Short to 12 V on DOUT (pins 2 and 4; pin 4 is next to the +12 V pin 5):** 12 V is inside the +/-13.2 V
  rating but a charging 14.4 V is not, and the effect on V+/V- is not characterised. Protection: a 100 to 330 ohm
  series resistor (0805 or 1206, at least 0.25 W) in each DOUT line, then a TVS or Zener clamp AFTER the resistor
  (chip side), not at the connector. A connector-side clamp would sit across the hard 12 V display rail in this
  fault. The resistor alone limits current but not pin voltage, so populate the clamp; fit the footprint at
  least. With a 5 kohm load, 330 ohm costs about 6 % of the swing (receivers need only +/-3 V).
- Optional: 47 to 100 ohm series resistors between the MAX3232 logic pins and the ESP32 GPIOs.
- J905 pin 1 (loop +): left unconnected, with an optional unpopulated pad. Pin 8 (loop -) is tied to ground.

Serial settings (set on the indicator, so ours must match what the scoreboard expects):
- Baud: 1200, 2400, 4800 or 9600
- Format: 7E1 or 8N1
- Firmware default is now **1200 baud, 7E1** (see below). If nothing shows: try 9600 baud, then odd parity
  (the vendor lists odd parity here, though 7E1 above says even), then 8N1.
- Published Digi-Star manuals (older EZII / StockWeigh 600, not this exact indicator) agree on **7E1 (even
  parity)**: 1200 baud standard, 9600 baud when Scoreboard Mode SCOREM = 4. They say the settings are not
  adjustable on the scale, so our output must match exactly. No handshaking, no XON/XOFF. Even parity is
  therefore more likely than the vendor's "odd".
- Same manuals: RS-232 levels (about +/-8 V) on the scoreboard output, plus a separate 20 mA port on the EZII.
  Supports the MAX3232 choice, but unconfirmed for the J905 on the indicator in use.
- StockWeigh 600 scoreboard methods: 1-6 send numeric display data (once/s up to on every weight change);
  7, 8, 10 send longer status/EID strings. The remote display most likely uses one of the numeric modes.
- **Scoreboard data format** (Digi-Star EZ2000/EZ2000V manual, section 5.6, pages 5.1-5.6 read directly):
  `<stx>ABBBCD<cr>`, 8 bytes in total, sent at a rate set by Scoreboard Mode (SCOREM).
  - `<stx>` = ASCII 2, `<cr>` = ASCII 13.
  - A = minus sign, SPACE, or a digit.
  - B (three characters) = digit or SPACE.
  - C = digit, SPACE, or a minus sign meaning a TR (tare/zero) command is active.
  - D = digit or a minus sign meaning motion is active.
  - Read closely, the manual already fixes the mapping: B cannot be `-`, so a minus sign sits only in A (the
    far left); D is always a character, so the weight is right-justified with leading spaces; C and D are
    ordinary weight digits unless their flag is active. Confirmed by the capture below.
  - Serial: 7 data bits, even parity, 1 stop bit; 1200 baud standard, 9600 baud when SCOREM = 4. The ESP32
    UART supports 7E1 in hardware, so the parity bit needs no software handling.
- EZ2000 wiring (J904, not the J905 on newer indicators): scoreboard RS-232 out = pin 4, ground = pin 7;
  20 mA current loop (+) = pin 1, loop (-) = pin 8. RS-232 levels are about +/-8 V. A separate 20 mA loop
  output exists on that model, so a 20 mA scoreboard input would need a different interface.
- Agri-Tronix lists the 2906-0409 as a "4-inch 6-digit remote display" (12 V DC, 20.25 x 6.48 x 1.25 in,
  35 ft standard cable, current draw not published). Part 2906-0405 is the J903 (RD/scoreboard) port version;
  make sure the display in hand is the J905 version. Agri-Tronix phone: (317) 738-4474.
- Sources: [Agri-Tronix remote display](https://www.agritronixcorp.com/4-6-digit-remote-display.html),
  [EZ400 manual J905 pinout](https://www.manualsdir.com/manuals/763995/digi-star-ez400.html?page=8),
  [StockWeigh 600 scoreboard methods](https://www.manualsdir.com/manuals/764124/digi-star-stockweigh-600.html?page=22),
  [Arduino forum quoting the EZII manual](https://forum.arduino.cc/t/digi-star-scoreboard-read/566015),
  [EZ2000 technical manual](https://manualzz.com/doc/28493021/technical-manual-ez-2000---digi-star).

**Captured from a real EZ2000 (Nov 2026)** (non-V; it has the optional serial port, J904). The EZ2000 and EZ2000V
share one manual and one scoreboard format. SCOREM = 4, USB-RS232 cable on J904, 3 minutes, 918 frames:
- J904 **pin 4** = scoreboard output (data seen). Pin 2 idles at RS-232 mark (-8.4 V) too but sent nothing.
  Both idle at **-8.4 V**, so the output is true RS-232 levels. Ground = pin 7.
- **9600 baud, 7E1** (every one of 7344 bytes had correct even parity). SCOREM 4 = 9600; other modes 1200.
- Every frame is exactly 8 bytes: `02` + 6 chars + `0D`. About 9 frames/s, repeated while the weight is steady.
  Output pauses for 1-2 s while the indicator shows a message (key presses) or is in setup.
- Examples: `<stx>-16050<cr>` (negative: `-` in A, 5 digits), `<stx>    50<cr>` (right-justified, spaces).
  This indicator steps by 50 lb. One stray frame `-16318` appeared during a weight change.
- Not seen: motion `-` in D (did not appear while the weight changed), TR `-` in C, a 6-digit positive weight.
- The firmware's `scoreboard_format()` produces exactly these frames.

Still unknown:
- Whether the Agri-Tronix 2906-0409 (built for the newer J905 indicators) accepts the EZ2000 frame, and at which
  baud (try 1200, then 9600). On the J905 the same data goes out on pin 2, which is where our board sends it.
- Whether the display needs the motion flag; the EZ2000 capture never sent it. If the display misbehaves while
  the weight moves, turn the flag off (`SB_SEND_MOTION_FLAG` in `scoreboard.cpp`).
- Signal levels on the J905 pin 2: **treated as RS-232 (decision)**, now backed by the -8.4 V idle level on the
  EZ2000. Build with the MAX3232, TX channel only.
- Current draw on the +12 V pin (needed for fuse and supply sizing).

Power: pin 5 carries +12 V to the scoreboard. Supply it from its own separate, separately fused 12 V rail
(not from the ESP32 board and not from the main input stage), and tie the display's ground (pins 6 and 8) to
this board's ground so the RS-232 signal has a common reference. Pin 1 is the 20 mA loop (+), not a supply.

## Signal budget

- Excitation 5 V, 1.550 mV/V: full scale = 7.75 mV at 155,000 lb total capacity
  (assuming equal cells averaged by the junction box).
- 1 lb is about 50 nV and 10 lb about 0.5 uV at full capacity. ADS1220 at gain 128, 20 SPS
  is adequate for 10-20 lb display steps. Datasheet (SBAS501D) Table 7-1: gain 128, 20 SPS,
  normal mode = 0.09 uV rms (0.41 uV p-p), i.e. about 1.8 lb rms / 8 lb p-p raw per reading
  at 50 nV/lb (noise changes only marginally with an external reference). Averaging 10
  readings (0.5 s) gives about 0.6 lb rms. No external gain stage needed (gain 128 is the
  PGA maximum; accuracy is limited by cells/mounting at 30-75 lb). Plan on firmware
  averaging, a clean 5 V excitation/reference path and careful input-filter layout. Run an
  internal offset calibration at power-up (MUX = 1110b, inputs shorted to mid-supply). The HX711 is borderline. ADS1220 kept as the ADC choice (ADS1232 would be the
  alternative; no reason to switch).
- Cell part number 407109 (Digi-Star, per the user) is unverified. Confirm the 31,000 lb
  rating is per cell and that there are five cells (four cells = 124,000 lb total).
- At gain 128 and 5 V reference the ADC full scale is about +/-39 mV, which leaves
  headroom over the 7.75 mV signal.
- Absolute accuracy is limited by the cells and mounting, not the ADC. Typical cell
  error of 0.02-0.05% of full scale is 30-75 lb on 155,000 lb. Calibrate on site with
  known weights across the range actually used.

## Excitation cable drop

The cable from our box to the junction box (the existing Digi-Star 4-pin cable, roughly 20-30 ft) carries the full
bridge current (~70 mA for five 350 ohm cells in parallel, ~70 ohm). The cell pigtails carry only one cell's share
(~14 mA), so their drop is negligible.

- AVDD is the ADC reference and there are no sense wires (4-conductor cable), so the drop is a **gain error**: readings
  come out low by R_loop / (R_bridge + R_loop). A span calibration with known weights removes the fixed part.
- What remains is copper tempco (about 0.39 %/degC), so the gain error drifts with cable temperature.
- Estimates for a 30 ft run (60 ft loop), 70 ohm bridge:

  | Gauge | Loop R | Gain error | Drift over 40 degC | At 155,000 lb |
  |---|---|---|---|---|
  | 22 AWG | ~0.97 ohm | 1.4 % | ~0.22 % | ~340 lb |
  | 20 AWG | ~0.61 ohm | 0.86 % | ~0.14 % | ~210 lb |
  | 18 AWG | ~0.38 ohm | 0.54 % | ~0.085 % | ~130 lb |

  A 700 ohm cell (140 ohm bridge) halves every figure. The cable gauge is not yet known.
- Decision: keep the existing cable, no sense wires. Measure the loop resistance (short EXC+/EXC- at the junction box,
  read at our connector), then calibrate on the installed cable. Expect about 0.1-0.2 % of reading drift over a
  40 degC swing, comparable to any 4-wire indicator on the same cable.
- Keep the EXC pair and the SIG pair as separate twisted pairs, and do not swap pairs at the junction box.
- Optional, only if drift matters in testing: measure excitation current with a small shunt in the EXC- return and
  compute R_loop = V_box / I - R_bridge to correct for cable temperature. Needs a known, stable R_bridge.
- Do not raise the excitation voltage to compensate: the ADC references the same 5 V, so it gains nothing.

## Firmware

Behaviour as implemented in `src/` (the hardware sections above describe the board; this describes what runs on it).

**Tasks.** `sample_task` polls DRDY every 4 ms tick and feeds each ADS1220 sample (20 SPS) into the filter; `unload_task` feeds the unload
detector at 10 Hz; `led_task` drives the status LED; the web server, scoreboard and ISOBUS tasks run only if enabled.
`app_main` marks the new firmware valid once everything is up (rollback is enabled, see below).

**Scale logic (`scale.cpp`).** Trimmed mean over a 1-64 sample window (default 20, about 1 s), piecewise-linear
calibration from up to 8 points (raw counts to pounds), tare, a zero function that shifts the calibration zero and
clears the tare, display rounding (default 10 lb) and a motion flag (spread in the window above 15 lb by default).
Without a calibration it uses a nominal slope worked out from the load cell settings (cell count, capacity,
mV/V, 5 V excitation) and the reading is marked approximate. Load cell layout is its own NVS blob so changing other
settings never wipes calibration.

**Unload detection (`unload.cpp`, `unload_log.cpp`).** Weight is sampled at 10 Hz. An unload starts when the weight
drops 100 lb (default) below the highest weight of the last ~10 s, and ends after 5 s (default) without a further
drop, or if the weight rises again; unloads under 250 lb are not logged. Each unload is stored with start, end,
total, time and the current field name in a ring of 1500 records in the `unloads` NVS partition. The scale has no RTC,
so the web page sends its clock. When the ring is full the page makes the user choose before the oldest records are
overwritten. Fields (up to 50 saved names) keep a running total; history exports as CSV. All thresholds are on the page.

**Status LED.** Red solid: ADC failed to start. Red blinking: no new sample for 2 s. Orange: filter still filling.
Cyan: running but uncalibrated or approximate. Green: calibrated. Green or cyan blinks off while the cart is in motion.

**Wi-Fi web page (`web_server.cpp`).** Access point `Open_Cart-xxxx`, WPA2, default password in `config.h`
(change it), channel 6, up to 4 clients, full TX power, power save off. Page at http://192.168.4.1/ or
http://opencart.local/ (mDNS). Tabs: Home (weight, tare, zero), Unloads, Fields, Settings (calibration, certified
scale comparison, load cells, outputs, AP name and password, firmware update, unload detection). It can be turned off
in settings; holding the BOOT button (GPIO0) while powering up forces it on for that boot.

**Scoreboard (`scoreboard.cpp`).** Off by default. UART1 at 1200 baud 7E1 (settings), one frame every 250 ms:
`<stx>` + six characters + `<cr>`, the EZ2000 layout. The weight is sent right-justified in the six characters, a
leading `-` marks a negative weight, and the last character becomes `-` while in motion. The tare flag is never
sent. The layout matches a capture from a real EZ2000 (see "Scoreboard"); not yet tried on the Agri-Tronix display.
UART2 is opened and left idle.
RS-485 mode (DE on GPIO8) exists in firmware but is not part of the board design.

**ISOBUS (`isobus_vt.cpp`).** Separate build (`pio run -e s3_isobus`), off by default, TWAI at 250 kbit/s accepting all
frames, AgIsoStack++ VT client with five tabs. Built but never run against a VT or the S3 TWAI plugin.

**Flash layout (`partitions.csv`, 16 MB).** nvs 24 KB, phy_init, `ota_0` and `ota_1` 3 MB each, `unloads` 256 KB
(NVS), otadata. Two app slots let the Wi-Fi page install firmware; `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` returns to
the previous slot if a new image crashes before `app_main` finishes. Settings and history survive updates.

**Wi-Fi firmware update.** The Settings tab uploads an app image (`firmware.bin`, never `firmware.factory.bin`) renamed
`open_cart_v<version>.bin`. The server checks the file name, that the size fits a slot, the image magic, that the app
description carries this project's name (`Open_Cart`), and the image hash, writes the other slot, switches the boot
partition and restarts. Renaming the project in `CMakeLists.txt` therefore means one USB flash before Wi-Fi updates
work again. The restart runs in its own 4 KB task (2 KB was too small and the scale never rebooted).

**Versioning and releases.** `version.txt` is bumped on every build by `scripts/version.py` and shown on the page as
"Running version" (the build date inside the image is not reliable). Releases are GitHub Releases made with
`scripts/release.py`; binaries are not committed. Details in CLAUDE.md.

## Things to confirm on the real hardware before committing

1. Resistance across the junction box excitation pair (sets excitation current).
   Also the cable loop resistance and gauge: short EXC+ to EXC- at the junction box end and read the resistance at
   the box connector (see "Excitation cable drop").
2. Junction box output connector pinout (the label colour table on the cell is unreadable).
3. That the junction box is passive (no amplifier or power requirement).
4. Scoreboard (Digi-Star J905, 2906-0409): pinout, power pins, framing and the weight string are now known
   (EZ2000 capture, see "Scoreboard"). Still open: that the display accepts it and at which baud, and current draw.
5. Whether the ESP32-S3 works with AgIsoStack++'s TWAI plugin and your pool size.

## CAN / ISOBUS notes

- 250 kbit/s, 29-bit IDs. The stack handles address claim and transport protocol.
- Transceiver decision: **TI TCAN1042HVDR on the PCB**, VCC = 5 V, VIO = 3V3 (straight to the ESP32 TWAI pins).
  SOIC-8; pinout: 1 TXD, 2 GND, 3 VCC, 4 RXD, 5 VIO, 6 CANL, 7 CANH, 8 STB. Not pin-compatible with the
  SN65HVD230/TCAN332 on pin 5 (VIO) and pin 8 (STB, tie low). Reason for the change: the earlier choice, the
  TCAN332 (SLLSEQ7F), has an absolute maximum of only **+/-14 V** on CANH/CANL, below a 14.4 V charging-voltage
  short from the ISOBUS power pins and far below a 24 V fault. The TCAN1042H family is rated **+/-70 V** on
  the bus pins (receiver common-mode +/-30 V), so a short of CANH or CANL to 12-14.4 V (or 24 V) is survivable.
  During such a fault communication stops but the chip should recover once it clears (confirm on the bench).
- Protection: add a CAN-rated dual TVS (PESD1CAN / PESD2CAN / NUP2105L class, about 24 V standoff) from CANH
  and CANL to GND at the connector, with short, heavy ground returns, optionally a common-mode choke and
  split termination only if this node is a bus end. The TVS is for ESD/EFT/surge only: with a 24 V standoff it
  does not conduct on a sustained 12-14.4 V fault, so the transceiver's own +/-70 V rating is what covers it.
  Also check the connector pinout: no 12 V pin next to CANH or CANL.
- TCAN1042 behavior notes: TXD dominant timeout (min 1.2 ms, so min data rate about 9 kbit/s, no issue at
  250 kbit/s); TXD has an internal pull-up; STB pull-up means a floating STB = standby.
- SN65HVD230 breakout boards (bench/prototype use only, **not fault tolerant: do not connect to a live tractor bus**) have a 120 ohm termination jumper (leave it open on the
  tractor bus) and no TVS or split termination; the PCB should add the TVS.
- Do not add 120 ohm termination if you tap an existing implement bus; tractor and
  implement ends are already terminated. Only terminate if this node is a bus end.
- Check ISOBUS power pins and current limits for the connector you use before
  drawing power from it.
- Start with a VT simulator before a real Gen 4 / Gen 5 display. Displays can be
  picky about object pools.

## Bring-up order (suggested)

1. 5 V excitation + ADS1220 on a bench with a single load cell or a bridge simulator.
   Confirm stable counts, check noise (std dev in counts) at 20 SPS.
2. Wi-Fi page: tare, two-point calibration with known weights.
3. Add junction box and all five cells. Re-measure excitation current.
   Then connect the real Digi-Star cable and repeat the loop-resistance measurement (see "Excitation cable drop").
   Do the two-point span calibration (step 2) with the final cable installed, not on the bench.
4. Scoreboard UART: connect the display (J905 pinout, see "Scoreboard"), try 1200 baud, then 9600.
5. CAN / VT: simulator first, then the real display.

## Safety and legal notes

- This is a hobby/prototype design. It is not legal-for-trade and not approved for
  any commercial weighing.
- Do not drive scoreboard or ISOBUS power pins from the ESP32 board.
