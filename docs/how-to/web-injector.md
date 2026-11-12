**English** · [Italiano](../it/how-to/web-injector.md)

# Use the Web Injector

The Web Injector runs injector scripts chosen from a web page, from a phone or another computer. The scripts are the same ones in `/payloads` on the microSD and run as in the Injector: same settings, same header, same outcome. The page does not upload or edit scripts.

## Start

1. Plug the dongle into the PC that should run the script.
2. In the launcher choose **Web Injector**. The dongle connects to WiFi (or creates the hotspot): see [Connect the dongle to WiFi](wifi.md).
3. The display shows a QR code, the network name, the IP address and a 6-digit PIN, as in [Web HID](web-hid.md). With the hotspot it starts from the WiFi credentials QR and, as soon as a device connects, the display switches by itself to the address QR; a click toggles between the two.
4. Open the address from the phone (or scan the QR) and enter the PIN.

Hold the button for 3 seconds to exit.

## The page

- **Folders**: the folders of `/payloads`, as on the display. To the right of each script, the effective layout and operating system (from the header, otherwise from `config.lua`).
- **By OS** and **By tag**: the scripts grouped by operating system or by tag (`@tags`), like the Injector's "/By OS" and "/By tag" views.

Tapping a script opens its card (name, description, layout, OS, tags, timeout); **Run** starts it. An invalid header shows the error and cannot be run.

While running, the page shows the name, status (`ui.status`), progress bar (`ui.progress`) and the run history, one entry per line with the time (the dongle's clock if it knows it, otherwise the time since start): start, `sys.log` lines, each new status, dialogs with the answer given (and whether it came from the page), the outcome. Progress bar updates alone are not included. If you are reading further up, the list does not scroll by itself. **Abort** stops the script like the 3-second press. The script's dialogs also appear on the page:

- `ui.menu`: the items as buttons;
- `ui.confirm`: No and Yes, with the default answer highlighted (the script's timeout applies here too);
- `ui.wait_button`: Click, Double and Long, like the button gestures.

Display and button stay active: you can answer from either, and the first answer wins.

At the end the page stays on the run: the outcome (Done, Error with the message, Aborted, Timeout) appears above the history. On the display the outcome stays until **OK** is pressed on the page or the dongle's button is pressed. **Back** returns to the list and **Run again** restarts the same script; both also dismiss the outcome on the display. From the list, **Last run** reopens the last run.

The button at the top right changes the page's look: **Plain** (understated), **Arcade** (pixel art, scan lines) and **Retro** (amber phosphor monitor). The browser remembers the choice.

## If the page closes

The script keeps running. When the page reconnects (by itself, as long as the PIN is valid) it receives the whole run history, including entries written while the page was closed, the current status, any open dialog and, once the run has finished, the outcome. The dongle keeps the last 100 entries, each up to 200 bytes; when it has dropped some, the page says so at the top. The last run's history stays until another one starts or you exit the app.

## Without a PIN

In `config.lua`, `webinjector = { pin = false }`. Anyone on the same network could then start the microSD's scripts on the connected PC. See [Security](../explanation/security.md).
