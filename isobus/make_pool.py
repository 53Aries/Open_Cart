#!/usr/bin/env python3
# Open_Cart
"""Generate the Open_Cart ISOBUS Virtual Terminal object pool.

    python isobus/make_pool.py

Writes isobus/scale.iop (embedded into the firmware by src/CMakeLists.txt) and
src/isobus_ids.h (object IDs used by src/isobus_vt.cpp). Re-run after editing, then rebuild
the s3_isobus environment.

The pool uses only VT version 3 object types, so any Gen3/Gen4/Gen5 terminal can load it. It is
designed for a 480x480 data mask with 60x60 soft keys; the firmware asks the VT to scale it to
fit other sizes. The pool is checked by a small parser at the end of this script (layout only; it
has not been loaded on a real VT).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_IOP = os.path.join(HERE, "scale.iop")
OUT_HDR = os.path.join(HERE, "..", "src", "isobus_ids.h")

DM = 480            # data mask width/height the pool is drawn for
SK = 60             # soft key designator size
NONE = 0xFFFF

# VT colour indexes
BLACK, WHITE, GREEN, TEAL, MAROON, PURPLE, OLIVE, SILVER, GREY, BLUE, LIME, CYAN, RED, MAGENTA, YELLOW, NAVY = range(16)
# Font sizes (index into the VT font table)
F6x8, F8x8, F8x12, F12x16, F16x16, F16x24, F24x32, F32x32, F32x48, F48x64 = range(10)
# Justification bytes: bits 0-1 horizontal (0 left, 1 centre, 2 right), bits 2-3 vertical (0 top, 1 middle, 2 bottom)
LEFT, CENTRE, RIGHT = 0, 1, 2
MIDDLE = 4

# Object type numbers
T_WS, T_DM, T_CONT, T_SKM, T_KEY, T_BTN, T_INSTR, T_INNUM, T_OUTSTR, T_OUTNUM = 0, 1, 3, 4, 5, 6, 8, 9, 11, 12
T_FONT = 23

objs = []        # (id, type, bytes)
ids = {}         # exported name -> id
_next = [100]


def new_id(name=None):
    i = _next[0]
    _next[0] += 1
    if name:
        assert name not in ids, name
        ids[name] = i
    return i


def add(oid, otype, body):
    objs.append((oid, otype, struct.pack("<HB", oid, otype) + body))
    return oid


_fonts = {}


def font(size, colour=BLACK, bold=False):
    key = (size, colour, bold)
    if key not in _fonts:
        _fonts[key] = add(new_id(), T_FONT, struct.pack("<BBBBB", colour, size, 0, 1 if bold else 0, 0))
    return _fonts[key]


def children(kids):
    return b"".join(struct.pack("<Hhh", c, x, y) for c, x, y in kids)


def out_string(name, w, h, size, text="", cap=None, bg=WHITE, colour=BLACK, just=LEFT, wrap=False, bold=False, transparent=False):
    """Output String. `cap` is the maximum text length the firmware may send later."""
    cap = max(cap or 0, len(text))
    value = text.ljust(cap).encode("latin-1")
    opts = (1 if transparent else 0) | (2 if wrap else 0)
    body = struct.pack("<HHBHBHBH", w, h, bg, font(size, colour, bold), opts, NONE, just, len(value)) + value + b"\x00"
    return add(new_id(name), T_OUTSTR, body)


def out_number(name, w, h, size, value=0, offset=0, scale=1.0, decimals=0, bg=WHITE, colour=BLACK, just=RIGHT):
    body = (struct.pack("<HHBHBH", w, h, bg, font(size, colour), 0, NONE)
            + struct.pack("<Ii", value & 0xFFFFFFFF, offset)
            + struct.pack("<f", scale)
            + struct.pack("<BBBB", decimals, 0, just, 0))
    return add(new_id(name), T_OUTNUM, body)


def in_number(name, w, h, size, value, vmin, vmax, scale=1.0, decimals=0, offset=0, bg=WHITE):
    body = (struct.pack("<HHBHBH", w, h, bg, font(size, BLACK), 0, NONE)
            + struct.pack("<IIIi", value, vmin, vmax, offset)
            + struct.pack("<f", scale)
            + struct.pack("<BBBBB", decimals, 0, RIGHT | MIDDLE, 1, 0))   # decimals, format, justification, options2 (enabled), macros
    return add(new_id(name), T_INNUM, body)


def in_string(name, w, h, size, cap):
    value = b" " * cap
    body = (struct.pack("<HHBHHBHB", w, h, WHITE, font(size, BLACK), NONE, 0, NONE, LEFT | MIDDLE)
            + struct.pack("<B", len(value)) + value + struct.pack("<BB", 1, 0))   # enabled, macros
    return add(new_id(name), T_INSTR, body)


def key(name, code, label):
    lab = out_string(None, SK, SK, F8x12, label, bg=SILVER, just=CENTRE | MIDDLE, wrap=True, transparent=True)
    return add(new_id(name), T_KEY, struct.pack("<BBBB", SILVER, code, 1, 0) + children([(lab, 0, 0)]))


def soft_key_mask(name, key_ids):
    return add(new_id(name), T_SKM, struct.pack("<BBB", SILVER, len(key_ids), 0) + b"".join(struct.pack("<H", k) for k in key_ids))


def data_mask(name, skm, kids):
    return add(new_id(name), T_DM, struct.pack("<BHBB", WHITE, skm, len(kids), 0) + children(kids))


# ---------------------------------------------------------------- soft keys ----
KEYS = {}
for code, (nm, label) in enumerate([
        ("K_TARE", "Tare"), ("K_UNTARE", "Clear tare"), ("K_UNLOAD", "Unload"),
        ("K_NEWER", "Newer"), ("K_OLDER", "Older"), ("K_ROLL", "Roll over"),
        ("K_UP", "Up"), ("K_DOWN", "Down"), ("K_USE", "Use"), ("K_DELETE", "Delete"),
        ("K_SETPT", "Set point"), ("K_CLRCAL", "Clear cal"), ("K_CMPCAL", "Apply cmp"),
        ("K_PREVTAB", "Prev tab"), ("K_NEXTTAB", "Next tab"), ("K_ZERO", "ZERO")], start=1):
    KEYS[nm] = key(nm, code, label)

# ------------------------------------------------------------------- masks ----
TABS = [("HOME", "Home"), ("UNL", "Unloads"), ("FLD", "Fields"), ("CAL", "Calibrate"), ("SET", "Setup")]
TAB_W = DM // len(TABS)
TAB_H = 44
CONTENT_Y = TAB_H + 8


def tab_bar(active):
    kids = []
    for n, (tag, label) in enumerate(TABS):
        on = tag == active
        lab = out_string(None, TAB_W - 4, TAB_H - 4, F12x16, label, bg=GREEN if on else SILVER,
                         colour=WHITE if on else BLACK, just=CENTRE | MIDDLE, bold=on, transparent=True)
        btn = add(new_id("TAB_%s_ON_%s" % (tag, active)), T_BTN,
                  struct.pack("<HHBBBBBB", TAB_W - 2, TAB_H, GREEN if on else SILVER, BLACK, 20 + n, 0, 1, 0)
                  + children([(lab, 2, 2)]))
        kids.append((btn, n * TAB_W + 1, 0))
    return kids


def label(text, x, y, w, size=F16x24, **kw):
    return (out_string(None, w, {F16x24: 28, F12x16: 20, F8x12: 16, F24x32: 36}.get(size, 28), size, text, **kw), x, y)


def dyn(name, x, y, w, size=F16x24, cap=40, **kw):
    h = {F16x24: 28, F12x16: 20, F8x12: 16, F24x32: 36, F32x48: 52}[size]
    return (out_string(name, w, kw.pop("h", h), size, "", cap=cap, **kw), x, y)


# Home
home = tab_bar("HOME")
home.append((out_number("H_WEIGHT", 340, 64, F48x64, value=100000, offset=-100000), 20, CONTENT_Y + 4))
home.append(label("lb", 372, CONTENT_Y + 28, 60, F24x32))
home.append(dyn("H_STATUS", 0, CONTENT_Y + 80, DM, F16x24, just=CENTRE))
home.append(dyn("H_UNLOAD", 0, CONTENT_Y + 116, DM, F24x32, just=CENTRE, bold=True))
home.append(dyn("H_FIELD", 10, CONTENT_Y + 166, DM - 20, F16x24, cap=60))
home.append(dyn("H_FTOT", 10, CONTENT_Y + 198, DM - 20, F16x24, cap=60))
home.append(dyn("H_TARE", 10, CONTENT_Y + 240, DM - 20, F16x24, cap=60))
home.append(dyn("H_CLOCK", 10, CONTENT_Y + 352, DM - 20, F16x24, cap=60))
home.append((out_string("H_WARN", DM - 20, 56, F12x16, "", cap=120, colour=RED, wrap=True, bold=True), 10, CONTENT_Y + 290))

# Unloads
PAGE_ROWS = 14
unl = tab_bar("UNL")
unl.append(dyn("U_HDR", 10, CONTENT_Y, DM - 20, F16x24, cap=60))
for i in range(PAGE_ROWS):
    unl.append(dyn("U_ROW%d" % i, 10, CONTENT_Y + 34 + i * 20, DM - 20, F8x12, cap=70))
unl.append(dyn("U_FOOT", 10, CONTENT_Y + 34 + PAGE_ROWS * 20 + 6, DM - 20, F12x16, cap=60))
unl.append((out_string("U_MSG", DM - 20, 40, F12x16, "", cap=120, colour=RED, wrap=True, bold=True), 10, CONTENT_Y + 34 + PAGE_ROWS * 20 + 30))

# Fields
FIELD_ROWS = 9
fld = tab_bar("FLD")
fld.append(dyn("F_CUR", 10, CONTENT_Y, DM - 20, F16x24, cap=60, bold=True))
for i in range(FIELD_ROWS):
    fld.append(dyn("F_ROW%d" % i, 10, CONTENT_Y + 36 + i * 30, DM - 20, F16x24, cap=40))
fld.append(dyn("F_MSG", 10, CONTENT_Y + 36 + FIELD_ROWS * 30 + 4, DM - 20, F12x16, cap=60, colour=RED))
fld.append(label("Add a field (touch the box):", 10, CONTENT_Y + 36 + FIELD_ROWS * 30 + 28, DM - 20, F12x16))
fld.append((in_string("F_NEW", DM - 20, 34, F16x24, 31), 10, CONTENT_Y + 36 + FIELD_ROWS * 30 + 50))

# Calibrate
PT_ROWS = 8
cal = tab_bar("CAL")
cal.append(dyn("C_GROSS", 10, CONTENT_Y, DM - 20, F16x24, cap=60))
cal.append(dyn("C_RAW", 10, CONTENT_Y + 30, DM - 20, F16x24, cap=60))
cal.append(label("Known weight (lb):", 10, CONTENT_Y + 62, 250, F16x24))
cal.append((in_number("C_KNOWN", 200, 36, F16x24, 0, 0, 9999999, scale=0.1, decimals=1), 270, CONTENT_Y + 58))
cal.append(label("Cart said (lb):", 10, CONTENT_Y + 100, 250, F16x24))
cal.append((in_number("C_CART", 200, 36, F16x24, 0, 0, 9999999, scale=0.1, decimals=1), 270, CONTENT_Y + 96))
cal.append(label("Certified scale (lb):", 10, CONTENT_Y + 138, 250, F16x24))
cal.append((in_number("C_CERT", 200, 36, F16x24, 0, 0, 9999999, scale=0.1, decimals=1), 270, CONTENT_Y + 134))
cal.append(label("Zero first, then Set point with a known weight on, or enter cart + certified and press Apply cmp.", 10, CONTENT_Y + 174, DM - 20, F8x12))
cal.append(label("Calibration points:", 10, CONTENT_Y + 192, DM - 20, F12x16, bold=True))
for i in range(PT_ROWS):
    cal.append(dyn("C_PT%d" % i, 10, CONTENT_Y + 214 + i * 20, DM - 20, F12x16, cap=60))
cal.append((out_string("C_MSG", DM - 20, 40, F12x16, "", cap=120, colour=RED, wrap=True, bold=True), 10, CONTENT_Y + 214 + PT_ROWS * 20 + 2))

# Setup: label + input number rows
SETUP = [
    # name, label, vmin, vmax, scale, decimals, default raw
    ("S_INC", "Display rounding (lb)", 1, 10000, 0.1, 1, 100),
    ("S_WIN", "Filter samples (1-64)", 1, 64, 1.0, 0, 20),
    ("S_CELLS", "Number of load cells", 1, 12, 1.0, 0, 5),
    ("S_CAP", "Capacity per cell (lb)", 100, 1000000, 1.0, 0, 31000),
    ("S_MV", "Cell output (mV/V)", 500, 5000, 0.001, 3, 1550),
    ("S_UDROP", "Unload start drop (lb)", 10, 20000, 1.0, 0, 100),
    ("S_UQUIET", "Unload ends after (s)", 10, 90, 0.1, 1, 50),
    ("S_UMIN", "Ignore unloads under (lb)", 0, 100000, 1.0, 0, 250),
    ("S_UAUTO", "Auto unload (1=on 0=off)", 0, 1, 1.0, 0, 1),
]
st = tab_bar("SET")
ROW_H = 38
for i, (nm, text, lo, hi, sc, dec, dflt) in enumerate(SETUP):
    y = CONTENT_Y + i * ROW_H
    st.append(label(text, 10, y + 4, 290, F16x24))
    st.append((in_number(nm, 160, 36, F16x24, dflt, lo, hi, scale=sc, decimals=dec), 310, y))
st.append(dyn("S_INFO", 10, CONTENT_Y + len(SETUP) * ROW_H + 4, DM - 20, F12x16, cap=80))
st.append(dyn("S_MSG", 10, CONTENT_Y + len(SETUP) * ROW_H + 28, DM - 20, F12x16, cap=80, colour=RED))
st.append(label("Wi-Fi, outputs and firmware update: use the web page.", 10, CONTENT_Y + len(SETUP) * ROW_H + 52, DM - 20, F8x12))

NAV = ["K_PREVTAB", "K_NEXTTAB"]
masks = [
    ("HOME", home, ["K_TARE", "K_UNTARE", "K_ZERO", "K_UNLOAD"] + NAV),
    ("UNL", unl, ["K_NEWER", "K_OLDER", "K_ROLL"] + NAV),
    ("FLD", fld, ["K_UP", "K_DOWN", "K_USE", "K_DELETE"] + NAV),
    ("CAL", cal, ["K_SETPT", "K_CMPCAL", "K_CLRCAL", "K_TARE"] + NAV),   # max 6 keys: Clear tare lives on Home
    ("SET", st, NAV),
]
for tag, kids, ks in masks:
    assert len(ks) <= 6, tag
    skm = soft_key_mask("SKM_" + tag, [KEYS[k] for k in ks])
    data_mask("MASK_" + tag, skm, kids)

# Working set designator + the working set itself
des = out_string("WS_DESIGNATOR", 32, 32, F6x8, "Scale", bg=WHITE, just=CENTRE | MIDDLE)
ws_body = (struct.pack("<BBHBBB", WHITE, 1, ids["MASK_HOME"], 1, 0, 1) + children([(des, 0, 0)]) + b"en")
add(new_id("WORKING_SET"), T_WS, ws_body)


# -------------------------------------------------------------- checking ----
def check(pool):
    """Walk the pool with the same field layout AgIsoStack++ parses; fail loudly on a mismatch."""
    p, seen, refs = 0, set(), []

    def u16(o): return struct.unpack_from("<H", pool, o)[0]
    while p < len(pool):
        oid, ot = u16(p), pool[p + 2]
        assert oid not in seen, "duplicate id %d" % oid
        seen.add(oid)
        b = p + 3
        if ot == T_WS:
            n, m, l = pool[b + 4], pool[b + 5], pool[b + 6]
            size = 7 + n * 6 + m * 2 + l * 2
            refs += [u16(b + 7 + i * 6) for i in range(n)] + [u16(b + 2)]
        elif ot == T_DM:
            n, m = pool[b + 3], pool[b + 4]
            size = 5 + n * 6 + m * 2
            refs += [u16(b + 5 + i * 6) for i in range(n)] + [u16(b + 1)]
        elif ot == T_SKM:
            n, m = pool[b + 1], pool[b + 2]
            size = 3 + n * 2 + m * 2
            refs += [u16(b + 3 + i * 2) for i in range(n)]
        elif ot == T_KEY:
            n, m = pool[b + 2], pool[b + 3]
            size = 4 + n * 6 + m * 2
            refs += [u16(b + 4 + i * 6) for i in range(n)]
        elif ot == T_BTN:
            n, m = pool[b + 8], pool[b + 9]
            size = 10 + n * 6 + m * 2
            refs += [u16(b + 10 + i * 6) for i in range(n)]
        elif ot == T_OUTSTR:
            ln, m = u16(b + 11), pool[b + 13 + u16(b + 11)]
            size = 14 + ln + m * 2
            refs += [u16(b + 5), u16(b + 8)]
        elif ot == T_OUTNUM:
            size = 26
            refs += [u16(b + 5), u16(b + 8)]
        elif ot == T_INNUM:
            size = 35
            refs += [u16(b + 5), u16(b + 8)]
        elif ot == T_INSTR:
            ln = pool[b + 13]
            size = 14 + ln + 2
            refs += [u16(b + 5), u16(b + 7), u16(b + 10)]
        elif ot == T_FONT:
            size = 5
        else:
            raise AssertionError("unknown type %d" % ot)
        p = b + size
    assert p == len(pool), "pool length mismatch (%d vs %d)" % (p, len(pool))
    missing = {r for r in refs if r != NONE and r not in seen}
    assert not missing, "dangling references: %s" % sorted(missing)
    return len(seen)


# The working set object must be first in the pool.
objs.sort(key=lambda o: 0 if o[1] == T_WS else 1)
pool = b"".join(o[2] for o in objs)
count = check(pool)
with open(OUT_IOP, "wb") as f:
    f.write(pool)

# ------------------------------------------------------------- C++ header ----
lines = ["// Open_Cart", "#pragma once", "// GENERATED by isobus/make_pool.py. Do not edit; re-run the script.", "#include <cstdint>", "",
         "namespace vtid {"]
for nm in sorted(ids, key=lambda k: ids[k]):
    lines.append("constexpr std::uint16_t %s = %d;" % (nm, ids[nm]))
lines += ["constexpr int kUnloadRows = %d;" % PAGE_ROWS, "constexpr int kFieldRows = %d;" % FIELD_ROWS,
          "constexpr int kCalRows = %d;" % PT_ROWS, "constexpr int kDataMaskPx = %d;" % DM, "constexpr int kSoftKeyPx = %d;" % SK,
          "}  // namespace vtid", ""]
with open(OUT_HDR, "w", newline="\n") as f:
    f.write("\n".join(lines))

print("wrote %s: %d objects, %d bytes" % (OUT_IOP, count, len(pool)))
