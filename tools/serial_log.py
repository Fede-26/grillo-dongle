#!/usr/bin/env python3
"""Resetta il dongle e stampa i log USB-Serial-JTAG per N secondi.

Alternativa non interattiva a `idf.py monitor`. Uso:
    python3 tools/serial_log.py /dev/cu.usbmodemXXXX [secondi]
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
# DTR deve restare basso: su USB-Serial-JTAG DTR alto tira giù GPIO0 e al
# reset il chip entrerebbe in download mode.
s.dtr = False
s.rts = False
s.open()
s.rts = True
time.sleep(0.1)
s.rts = False
s.close()
# Dopo il reset la porta USB scompare e riappare: riapriamo.
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
