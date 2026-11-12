**English** · [Italiano](../it/explanation/architettura.md)

# Architecture

Grillo is an ESP-IDF v6.1 firmware (C, FreeRTOS) for a tightly constrained device: an ESP32-S3 without PSRAM, a 160x80 screen, a single button. The choices below follow from these constraints.

## Layers

```
apps/         injector, webinjector, webhid, webfiles, netscan, usbdisk, launcher  <- one per activity
components/   injector_core                               <- script execution
              appfw  ui  gfx  display  input  led  qr  webui  webtheme  <- interface
              hid  usbdev  net  websrv  storage  config  luart  <- services
              devcon  syslog  clock  board  lua           <- console, log, time, pins, interpreter
main/         startup, app table, debug console
```

Apps depend on services, never on each other. That is why any app can be removed without touching the others.

## App framework

An app is a static descriptor (`app_desc_t`) with a `run()` function in a dedicated task. The manager (`appmgr`) starts one app at a time and, when `run()` returns, releases all shared resources automatically: USB profile, lent SD card, LED layer, network. The reason is that an app forgets to release something mostly when it exits on an error, and a dongle stuck in the wrong USB profile is hard to diagnose without a large screen or a keyboard.

Input is a single button, reduced to gestures: click, double click, long press; a 3-second press requests exit, from the app or from the script. A long press is a release between 0.6 and 1.5 seconds: from 0.6 seconds the LED blinks and the line below the status bar turns white, and on release the status bar flashes. From 1.5 seconds the line becomes the red exit bar, and releasing before 3 seconds cancels without doing anything. Exit is an event (`APP_EV_EXIT`) that every wait delivers, even when the event queue is full, and that the common dialogs honor. Delivering it is not enough, though: a FreeRTOS task cannot be safely terminated from outside, so an app in a long computation or a blocking call exits only when it goes back to waiting. For that case, if the app is still there after 3 seconds, the status bar shows "Hold again: reboot" and a second hold reboots the dongle. It is the only truly guaranteed exit.

Texts longer than the available space (errors, script descriptions, confirmations) scroll by themselves one line at a time, because with a single button there is no convenient way to scroll manually.

## USB profiles

TinyUSB exposes all or nothing, and reconfiguring USB requires re-enumerating the device. That is why the app declares the profile it needs and the system applies it before starting the app. A profile is a combination of interfaces (keyboard, mouse, disk, CDC serial), not a closed list: a new app declares the one it needs, and names, PID and status bar indicator are derived from it. With the profile off, the PHY goes back to USB-Serial-JTAG (log and flashing).

Not every combination fits: the ESP32-S3 OTG has few IN endpoints, and with keyboard + mouse + disk + CDC the Mac enumerated only the HID (tested on the device). The measured limit is 4 IN endpoints besides EP0; a combination that exceeds it is rejected instead of being half enumerated.

The microSD is a single resource and the disk is always the microSD, in one of two modes: lent to the host for reading and writing (the firmware no longer sees it), or shared read-only (the firmware keeps it mounted but does not write to it). The mode is part of the profile, and the system handles the SD handover: the host is detached, the SD returns to the firmware, the log is flushed so no lines are lost, then the SD is lent again. An app that changes profile midway (the injector does with `disk.attach`) goes through the same function.

## Configuration and scripts

`/config.lua` is a Lua script that returns a table; at startup it runs in a sandboxed VM and is flattened into key/value pairs, then the VM is closed to free RAM. This way apps read with `config_str("keyboard.layout", "us")` and the rest of memory is not taken up by an interpreter.

Injector scripting uses a different VM, one per run: limited libraries (no `io`, `os`), a 96 KB memory limit, interruptible at any instruction. The memory limit is deliberate: with ~250 KB of free heap, a script must not be able to take memory away from WiFi or USB.

## Memory

Measurements on the device (from session notes, not reproduced in an automated test): free heap about 316 KB at idle, 252 KB after exiting an app that used the network, about 193 KB with the Web HID server running; no leaks across repeated open and close cycles.

## Network

The WiFi manager (`net`) is lease-based: every app that needs it requests it and releases it; WiFi stays on as long as there is at least one lease. Each lease belongs to the task that took it: when an app exits, the system releases only the leases of its task, so a service in its own task would not lose the connection. The wait for the connection is interrupted when the user requests exit, instead of lasting until the timeout.

The web page server (`websrv`), the QR code (`qr`), the connection screen (`webui`) and the page theme (`webtheme`) are separate components, not Web HID code: PIN authentication, per-address lockout after 5 failures, a single session with takeover from the same device, compressed page. All web apps (Web HID, Web Injector, Web Files) use them without repeating the security-related part; `webjson` builds the JSON messages of Web Injector and Web Files. In automatic mode it scans for networks, picks the best among those known in `config.lua`, and if it finds none it creates a hotspot. WiFi power saving is disabled: when enabled, the Web HID page took seconds to respond.

## Web pages

The pages live in flash, compressed, together with the common theme: with the hotspot the phone has no internet, so nothing can come from outside, and the page works even without a microSD (Web HID does not require one). The theme has three modes, chosen from the page and remembered by the browser: plain (understated), arcade (pixels, scan lines, small animations) and retro (amber phosphor monitor). Fonts: Silkscreen for titles and graphic elements, Iosevka without ligatures for logs, the system font for text; the two embedded fonts are reduced to the characters used by `tools/subset_fonts.py` (2.9 and 9.7 KB) and are licensed under SIL OFL 1.1 (texts in `components/webtheme/web/fonts/`). The theme changes only the look: the page layout rules, for phones with an on-screen keyboard and for desktop, remain those of the page.

## Injector and Web Injector

The two apps run the same scripts with the same code: `injector_core` contains the payload list and headers, the Lua engine, the run screen and the outcome. Only the way the script is chosen changes, from the display or from a web page. The core calls optional front-end hooks (status, log, opening and closing dialogs, outcome): the Injector does not use them, the Web Injector forwards them to the phone. The phone's answers reach the app as an `APP_EV_ANSWER` event, which the common dialogs treat as a button choice: this way display and page stay active together without duplicated dialogs, and the first answer wins. The script runs in the app task and messages to the page are sent with `websrv_send`, which delivers them from the server task only if the authenticated client is still the same. The Web Injector also keeps the run history (the last 100 entries, already in JSON) and the outcome, and resends them on every login under the same lock as the hooks: a new entry arrives only after the rest, so the page sees neither duplicates nor gaps.

## Web Files

All Web Files work runs in the server task: the app task only draws the screen. Short operations (listing, folders, rename, trash) go through the WebSocket; file contents go through HTTP routes with the session token, because a whole file does not fit comfortably in a WebSocket message and a download must be able to start from a link. An uploaded or saved file goes first to `<name>.part` and replaces the old one only at the end; every write holds the card lock (`storage_begin_write`), so lending the card to the host via the console waits for it to finish. The trash is one numbered folder per item in `/.trash`, with the original path alongside: deleting is a rename, instant even for a whole folder. The passwords in `config.lua` are hidden by parsing the text as Lua (comments, strings with escapes, long strings), not with text searches, and put back in place on save (`apps/webfiles/secrets.c`, with tests on the PC). The script check compiles the text in a VM without running it; the Lua parser is recursive, so the server task has a 16 KB stack like the Injector's. Web Files does not use USB: the card stays with the firmware.

## Time

The dongle has no clock battery: at power-on the time is unknown. SNTP sets it every time any app connects to a network as a client (not with the hotspot, which has no internet), or a script does with `host.clock()`, which reads it from the PC over the return channel. From the PC the UTC time is taken and shown with the time zone from `config.lua` (`timezone`, default Europe/Rome with daylight saving time): this way a PC with the wrong time zone does not shift the dates. The time survives software reboots and is lost when power is removed. While it is unknown, the lines of `/logs/grillo.log` only have the milliseconds since boot, `sys.time()` and `sys.date()` return `nil` and the Netscan CSVs have a sequence number instead of the date.

## Tests

The pure logic (layout tables, button gestures, NBNS and mDNS parsing, DHCP DISCOVER and OFFER, OUI lookup, Netscan CSV, `config.lua` secrets for Web Files) runs on the PC: `make -C test/host`. The rest is verified on the device with the debug console and screenshots (`tools/grillo.py shot`).
