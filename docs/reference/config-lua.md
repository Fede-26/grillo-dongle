**English** · [Italiano](../it/reference/config-lua.md)

# config.lua

`/config.lua` on the microSD is a Lua script that returns a table. All keys are optional: if the file is missing or contains an error, the defaults apply (the error, with file and line, is in the log and on the debug console).

The file is run in a sandboxed VM, then flattened into key/value pairs (`keyboard.layout`, `wifi.1.ssid`) and the VM is closed. It is read again, with brightness, rotation and LED reapplied:

- at startup;
- on exit from USB Disk, because it may have been modified from the PC;
- with `config reload` from the [debug console](debug-console.md), only while the launcher is open.

An error at startup is shown on the display (long text scrolls); later, a warning appears in the status bar. In either case the configuration stays empty and the defaults apply.

The template is `sd-template/config.lua`: it lists all the keys, commented out, with their default value. To change one, remove the leading `--`.

## Keys

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `wifi` | list of `{ssid, pass, trusted}` | empty | Known networks. Order does not matter: with several networks visible you choose from the display, otherwise the strongest wins (see [WiFi](../how-to/wifi.md)). WPA2/WPA3-Personal only: an entry without `pass` is ignored. `trusted = true` (default `false`) marks the network as trusted: web apps do not ask for the PIN (see [Trusted networks](../how-to/wifi.md#trusted-networks-no-pin)). |
| `hotspot.ssid` | text | `Grillo-XXXX` | Name of the hotspot created if no known network is reachable (XXXX = last digits of the MAC). |
| `hotspot.pass` | text | `grillo-dongle` | Hotspot password. If missing or shorter than 8 characters, the default is used. |
| `hotspot.trusted` | boolean | `false` | `true`: on the hotspot web apps do not ask for the PIN. Makes sense only with your own `hotspot.pass`: with the default one, anyone who knows the firmware gets in. |
| `autorun.app` | app id | none | App to start at power-on. See [Autorun](../how-to/autorun.md). |
| `autorun.payload` | path | none | Script to pass to the app (e.g. `kiosk/setup.lua`). |
| `autorun.delay` | seconds | 3 | Length of the cancelable countdown. |
| `keyboard.layout` | `us`/`it`/`colemak` | `us` | Default layout of the PC keyboard. |
| `keyboard.os` | `windows`/`linux`/`mac` | `windows` | Default OS of the PC. |
| `keyboard.key_delay` | ms, 0..60000 | 8 | Pause between keys. |
| `keyboard.hold` | ms, 0..60000 | 5 | Duration of each key press. |
| `keyboard.jitter` | ms, 0..60000 | 0 | Random variation of the pause. |
| `keyboard.line_delay` | ms, 0..60000 | 0 | Extra pause after each `\n`. |
| `keyboard.start_delay` | ms, 0..60000 | 1000 | Wait before starting a script (injector only). |
| `secrets` | table name → text | empty | Passwords for `kb.type_secret` (`secrets.sudo` is the one used by `pcdisk` with `mount = "sudo"`). See [Passwords](../how-to/passwords.md). |
| `pcdisk.mount` | `auto`/`sudo` | `auto` | How the `pcdisk` library mounts the SD on the Linux PC. See [Lua API](lua-api.md#pcdisk-library). |
| `ui.brightness` | 0..100 | 80 | Display brightness. |
| `ui.rotation` | 0 or 180 | 0 | Display rotation (90 and 270 are not supported). |
| `webhid.pin` | boolean | `true` | `false`: the Web HID does not ask for the PIN on any network. With `true` the PIN is asked only on networks not marked `trusted`. See [Security](../explanation/security.md). |
| `webinjector.pin` | boolean | `true` | Like `webhid.pin`, for the Web Injector. |
| `webfiles.pin` | boolean | `true` | Like `webhid.pin`, for Web Files. |
| `timezone` | POSIX string | `CET-1CEST,M3.5.0,M10.5.0/3` | Time zone (the default is Europe/Rome, with daylight saving time) for the dates in `/logs/grillo.log`, `sys.date()` and the Netscan file names. |
| `ntp` | host | `pool.ntp.org` | NTP server the dongle gets the time from when it is connected to a network as a client. |
| `netscan.probe_host` | host | `connectivitycheck.gstatic.com` | Host contacted by the "Web" check of Network info (must answer 204 to `/generate_204`). |
| `led.brightness` | 0..100 | 30 | LED brightness. |

The `keyboard.*` settings are the defaults for injector and Web HID scripts; a [script header](script-header.md) overrides them for that script. An out-of-range value is clamped to the nearest limit.

## Example

```lua
return {
  wifi = {
    { ssid = "Home", pass = "password", trusted = true },
    { ssid = "Office", pass = "other-password" },
  },
  hotspot = { pass = "changeme123" },
  autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },
  keyboard = { layout = "it", os = "windows", key_delay = 8 },
  secrets = { admin = "..." },
  ui = { brightness = 80, rotation = 180 },
  webhid = { pin = true },
}
```
