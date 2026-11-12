#!/usr/bin/env python3
"""Resets the dongle and prints the USB-Serial-JTAG logs for N seconds.

Non-interactive alternative to `idf.py monitor`. Usage:
    python3 tools/serial_log.py /dev/cu.usbmodemXXXX [seconds]
"""
import sys
import time

import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
# DTR must stay low: on USB-Serial-JTAG a high DTR pulls GPIO0 down and on
# reset the chip would enter download mode.
s.dtr = False
s.rts = False
s.open()
s.rts = True
time.sleep(0.1)
s.rts = False
s.close()
# After the reset the USB port disappears and reappears: reopen it.
end = time.time() + secs
while time.time() < end:
    try:
        s.open()
        break
    except serial.SerialException:
        time.sleep(0.1)
while time.time() < end:
    try:
        d = s.read(4096)
    except serial.SerialException:
        time.sleep(0.1)
        continue
    if d:
        sys.stdout.write(d.decode(errors="replace"))
        sys.stdout.flush()
