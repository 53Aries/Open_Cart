"""Capture and decode the scoreboard output of a Digi-Star indicator (e.g. EZ2000).

Run with PlatformIO's Python (it already has pyserial):
    ~/.platformio/penv/Scripts/python.exe scripts/sniff_scoreboard.py COM5 --scan
    ~/.platformio/penv/Scripts/python.exe scripts/sniff_scoreboard.py COM5 --baud 1200

The port is opened as 8N1 on purpose. A 7E1 character and an 8N1 character are both 10 bits long,
so reading 7E1 as 8N1 shows the parity bit as bit 7 instead of letting the driver drop or mangle it.
The script then checks whether that bit gives even or odd parity, which tells us the real format.

While capturing, type a note and press Enter (e.g. "display 1230 steady") to put it in the log next to
the frames. Ctrl+C stops. Everything goes to scoreboard_capture_<time>.txt (plus a raw .bin).
"""
import argparse
import sys
import time
from datetime import datetime

import serial

try:
    import msvcrt  # Windows: non-blocking keyboard for notes
except ImportError:
    msvcrt = None

STX, CR, LF = 0x02, 0x0D, 0x0A
NAMES = {0x02: "<stx>", 0x03: "<etx>", 0x0D: "<cr>", 0x0A: "<lf>", 0x20: "_"}


def parity_ok(b, even):
    ones = bin(b & 0x7F).count("1") + (b >> 7)
    return (ones % 2 == 0) if even else (ones % 2 == 1)


def show(frame):
    """Readable form: 7-bit ASCII with control chars named and spaces shown as '_'."""
    out = []
    for b in frame:
        c = b & 0x7F
        out.append(NAMES.get(c, chr(c) if 0x20 < c < 0x7F else f"<{c:02x}>"))
    return "".join(out)


def split_frames(buf):
    """Cut the stream into frames that end at <cr> (7-bit). Returns (frames, leftover)."""
    frames, start = [], 0
    for i, b in enumerate(buf):
        if (b & 0x7F) == CR:
            frames.append(bytes(buf[start:i + 1]))
            start = i + 1
    rest = buf[start:]
    if len(rest) > 64:  # no <cr> for a long time: dump it as one chunk so nothing is hidden
        frames.append(bytes(rest))
        rest = bytearray()
    return frames, bytearray(rest)


def scan(port):
    print("Scanning. Leave the indicator showing a steady weight.\n")
    for baud in (1200, 2400, 4800, 9600, 19200):
        with serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1, timeout=0.2) as s:
            s.reset_input_buffer()
            data = bytearray()
            t_end = time.time() + 3.0
            while time.time() < t_end:
                data += s.read(256)
        n = len(data)
        if not n:
            print(f"{baud:>6}: no bytes")
            continue
        ev = sum(parity_ok(b, True) for b in data)
        od = n - ev
        hi = sum(b >> 7 for b in data)
        stx = sum((b & 0x7F) == STX for b in data)
        cr = sum((b & 0x7F) == CR for b in data)
        print(f"{baud:>6}: {n:4d} bytes  stx={stx} cr={cr}  even-parity ok {ev}/{n}  odd ok {od}/{n}  "
              f"bit7 set {hi}/{n}")
        print(f"        sample: {show(data[:40])}")
    print("\nThe right baud has many stx/cr and readable digits. Parity: 100 % even -> 7E1, 100 % odd -> 7O1,"
          "\nbit7 never set and mixed parity -> probably 8N1 (or 7N2).")


def capture(port, baud, show_all):
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    txt_name, bin_name = f"scoreboard_capture_{stamp}.txt", f"scoreboard_capture_{stamp}.bin"
    log = open(txt_name, "w", encoding="utf-8")
    raw = open(bin_name, "wb")

    def emit(line):
        print(line)
        log.write(line + "\n")
        log.flush()

    emit(f"# {port} {baud} baud, opened 8N1 (bit 7 = parity bit if the line is 7E1/7O1)")
    emit("# columns: time | hex | text (_ = space) | parity of every byte | repeat count")
    s = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1, timeout=0.05)
    s.reset_input_buffer()
    buf, last, repeats, note = bytearray(), None, 0, ""
    t0 = time.time()
    try:
        while True:
            chunk = s.read(256)
            if chunk:
                raw.write(chunk)
                buf += chunk
                frames, buf = split_frames(buf)
                for f in frames:
                    if f == last and not show_all:
                        repeats += 1
                        continue
                    if repeats:
                        emit(f"{'':>9} (previous frame repeated {repeats} more times)")
                    repeats, last = 0, f
                    par = ("E" if all(parity_ok(b, True) for b in f)
                           else "O" if all(parity_ok(b, False) for b in f) else "?")
                    emit(f"{time.time() - t0:8.2f}s {f.hex(' '):<40} {show(f):<24} par={par}")
            if msvcrt and msvcrt.kbhit():
                ch = msvcrt.getwche()
                if ch in "\r\n":
                    print()
                    emit(f"{time.time() - t0:8.2f}s ### NOTE: {note}")
                    note = ""
                elif ch == "\b":
                    note = note[:-1]
                else:
                    note += ch
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        raw.close()
        log.close()
        print(f"\nSaved {txt_name} and {bin_name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="e.g. COM5")
    ap.add_argument("--baud", type=int, default=1200)
    ap.add_argument("--scan", action="store_true", help="try common baud rates for 3 s each and report")
    ap.add_argument("--all", action="store_true", help="print every frame, not only changes")
    a = ap.parse_args()
    if a.scan:
        scan(a.port)
    else:
        capture(a.port, a.baud, a.all)


if __name__ == "__main__":
    sys.exit(main())
