**English** · [Italiano](../it/reference/console-debug.md)

# Debug console

`components/devcon` exposes a line-based console on the dongle's serial port, used by `tools/grillo.py`. It is for development: driving the button, seeing the screen, reading files from the microSD.

When TinyUSB takes the PHY (HID and disk profiles) the USB-Serial-JTAG is no longer available. With the `CONFIG_GRILLO_USB_DEBUG_CDC=y` option (default, "Grillo USB" menu) the console and the ESP-IDF logs go through a CDC-ACM port that the dongle exposes with every profile. A release build without that option has no console while the app uses HID or MSC.

```sh
python3 tools/grillo.py cmd state          # runs a command
python3 tools/grillo.py shot screen.png    # 4x screenshot
python3 tools/grillo.py log 10             # log for 10 seconds
```

Options: `--port /dev/cu.usbmodemXXX`, `-q` (hides log lines). The client opens the port without touching DTR/RTS, so the dongle does not reset.

## Commands

| Command | Effect |
| --- | --- |
| `state` | Current app, USB profile, SD, heap. |
| `apps` | Lists the apps. |
| `launch <id> [arg]` | Starts an app (e.g. `launch injector`). |
| `exit` | Exits the current app, like the 3-second press. If the app does not respond, a second `btn hold` reboots the dongle. |
| `btn click\|double\|long\|hold\|armed` | Simulates a button gesture. `armed` only shows the long-press signal (white line), with no event; the line stays until a `btn long` or a `btn hold` removes it. |
| `shot` | Downloads the screen (RGB565 in base64). |
| `ls [path]` | Lists a folder. |
| `cat <path>` | Prints a file (16 KB maximum). |
| `config reload \| get <key>` | Reloads `config.lua` (only from the launcher: with an app open it answers `exit the app first`; errors appear on the display and in `state`) or reads a key. |
| `log <tag\|*> <level>` | Sets the log level (`none`, `error`, `warn`, `info`, `debug`, `verbose`). |
| `usb none\|hid\|hiddisk\|msc` | Changes the USB profile. |
| `reboot` | Reboots the dongle. |

To flash a dongle in use, first exit the app (`grillo.py cmd exit`), then `idf.py -p <port> flash`.
