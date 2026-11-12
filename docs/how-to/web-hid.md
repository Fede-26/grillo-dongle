**English** · [Italiano](../it/how-to/web-hid.md)

# Use Web HID

Web HID turns the dongle into a keyboard and touchpad driven from a web page, from a phone or another computer.

## Start

1. Plug the dongle into the PC to control.
2. In the launcher choose **Web HID**. The dongle connects to WiFi (or creates the hotspot): see [Connect the dongle to WiFi](wifi.md).
3. The display shows a QR code, the network name, the IP address and a 6-digit PIN.
4. On the phone or computer, join the same network (or the `Grillo-XXXX` hotspot) and open the address, or scan the QR. With the hotspot there are two QR codes: the first one shown contains the credentials of the dongle's WiFi; as soon as a device joins the hotspot, the display switches by itself to the QR with the page's address. A click toggles between the two QR codes, for example to connect a second phone. On a known network there is only the address QR.
5. Enter the PIN.

Hold the button for 3 seconds to exit.

## The page

Two tabs, **Touchpad** and **Keys**.

- **Touchpad**: one finger moves the mouse, a short tap clicks, two fingers scroll (wheel), a short two-finger tap is a right click. Below, the three buttons Left, Mid, Right can be held down (to drag).
- **Keys**:
  - at the top, the layout and operating system selectors: they tell the dongle how the PC is configured; with OS = macOS the modifiers show as Ctrl, Opt, Shift, Cmd;
  - the "Type here" box is live typing: what you write reaches the PC immediately, deletions included;
  - the modifiers (Ctrl, Alt/Opt, Shift, Win/Cmd) are toggled on and apply to the next key;
  - special keys (Esc, Tab, Enter, Backspace, arrows, …) and the F1–F12 panel;
  - "Paste & type" opens an area where you paste a long text to type in one go.

If the PC does not respond, the page shows "PC not ready": check the cable and that the PC has recognized the keyboard. The warning disappears at the first successful command.

A character the chosen layout does not have is typed as `?` and the page flags it: this way the text on the PC stays as long as the one on the page, and live typing keeps deleting the right characters.

If the phone goes to standby or loses WiFi, the page retries connecting by itself as long as the PIN is valid. The dongle accepts the new connection from the same address even if the old one is not yet seen as closed, and replaces it.

A long pasted text is stopped only by exiting the app (3-second press): closing the page does not stop the chunks already sent.

The button at the top right changes the page's look: **Plain** (understated), **Arcade** (pixel art, scan lines) and **Retro** (amber phosphor monitor). The browser remembers the choice.

## Without a PIN

On a trusted network you can turn off the PIN: in `config.lua`, `webhid = { pin = false }`. Anyone on the same network could then control the PC. See [Security](../explanation/security.md).

After 5 wrong PINs from the same address, that address is blocked for 30 seconds; the others are not. One device at a time: a second device gets "Another client is connected" until the first one disconnects.

The protocol is described in [WebSocket protocol](../reference/websocket-protocol.md).
