**English** · [Italiano](../it/reference/lua-api.md)

# Injector Lua API

Reference for the functions available in the `.lua` scripts run by the injector. For the header format see [Script header](script-header.md).

## Environment

The script runs in a sandboxed Lua 5.4 VM, one script at a time.

- Standard libraries available: `base`, `coroutine`, `table`, `string`, `math`, `utf8`. Missing: `io`, `os`, `package`, `debug`; `dofile` and `loadfile` are removed.
- `print(...)` writes to the log (info level), not to the display.
- Maximum VM memory: 96 KB. Beyond the limit the script ends with an out-of-memory error.
- You can stop it by holding the button for 3 seconds: the next call to a dongle function (or 1000 Lua instructions) raises the `aborted` error, and an open `ui.menu` or `ui.confirm` closes. The same applies to `@timeout`. After the stop the display shows "Aborted" with the point where the script stopped; you stay in the injector.
- Only textual source code is accepted, never precompiled bytecode: `load()` also accepts only text, and `string.dump` does not exist.
- Nested calls that go through C (`pcall` inside `pcall`, `string.gsub` and `table.sort` callbacks, metamethods, `require`) have a stack limit: beyond it, the script ends with `C stack overflow (too many nested calls)` instead of hanging the dongle. With nested `pcall`s the limit is about 24 levels (measured on 2026-11-10). Pure Lua recursion does not have this limit, only the memory one.
- Before running, the dongle waits for the PC to mount the keyboard, then waits `start_delay`.

An unhandled Lua error ends the script and is shown on the display with file name and line; if the message is long it scrolls by itself. The message also goes to the log on the microSD (`/logs/grillo.log`).

## `kb`: keyboard

### `kb.type(text [, vars])`

Types a UTF-8 string with the current layout and OS. In `text`, `${name}` is replaced by the value of `vars.name` (if the table is missing, by the global variable `name`). An undefined variable is an error: typing `${user}` literally is never what you wanted. `\n` presses Enter, `\t` presses Tab.

Errors: `U+XXXX not typeable with layout NAME` if the character does not exist in the layout and there is no fallback for the OS; `USB host not responding` if the PC does not respond.

```lua
kb.type("Hello ${who}!\n", {who = "Grillo"})
```

### `kb.type_secret(name)`

Types the string `secrets.name` from `config.lua` without ever passing it through a Lua string: the script cannot read it as a value. Error if the key does not exist, or `secret 'name' has a character not typeable with layout NAME` (the message does not say which character, so as not to reveal it). See [Type a password](../how-to/passwords.md).

### `kb.tap(key [, n])`

Presses and releases a key `n` times (default 1). `key` is a name from the table below or a single UTF-8 character of the current layout (`"r"`, `"€"`). An uppercase letter indicates the key, not Shift.

### `kb.press(key, ...)`

Presses up to 6 keys plus the modifiers together, then releases everything. For key combinations.

```lua
kb.press("CTRL", "ALT", "DELETE")
kb.press("GUI", "r")        -- Win+R on Windows, Cmd+R on macOS
```

Recognized key names (case-insensitive):

| Group | Names |
| --- | --- |
| Left modifiers | `CTRL`/`CONTROL`, `SHIFT`, `ALT`/`OPTION`, `GUI`/`WIN`/`CMD`/`COMMAND`/`SUPER` |
| Right modifiers | `RCTRL`, `RSHIFT`, `RALT`/`ALTGR`, `RGUI` |
| Control | `ENTER`/`RETURN`, `ESC`/`ESCAPE`, `BACKSPACE`, `TAB`, `SPACE`, `CAPSLOCK`, `MENU`/`APP` |
| Navigation | `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `INSERT`, `DELETE`/`DEL`, `UP`, `DOWN`, `LEFT`, `RIGHT` |
| Other | `PRINTSCREEN`, `SCROLLLOCK`, `PAUSE`/`BREAK`, `NUMLOCK` |
| Function | `F1` … `F24` |

### `kb.layout([name])` and `kb.os([name])`

With an argument they set, without one they read. They return the current value. Layout: `us`, `it`, `colemak`. OS: `windows`, `linux`, `mac` (`win` and `macos` are accepted as input).

### Timing: `kb.speed`, `kb.hold`, `kb.jitter`, `kb.line_delay`

With an argument (milliseconds, 0..60000) they set, without one they read. They apply to the rest of the script.

| Function | Meaning | Default |
| --- | --- | --- |
| `kb.speed` | pause between one key and the next | 8 |
| `kb.hold` | duration of each key press | 5 |
| `kb.jitter` | random ± ms on the pause | 0 |
| `kb.line_delay` | extra ms after each `\n` | 0 |

The defaults can come from `config.lua` (`keyboard` section) or from the script header.

## `mouse`

Not available while the USB disk is attached (`disk.attach`): error `no mouse while the disk is attached`.

- `mouse.move(dx, dy)`: relative movement.
- `mouse.click([button])`: `"left"` (default), `"right"`, `"middle"`.
- `mouse.scroll(n)`: wheel, `n` from -127 to 127.

## `ui`: display and button

- `ui.status(text)`: status in the run screen, on two lines; longer text scrolls by itself.
- `ui.progress(value [, max])`: progress bar, max 100 if omitted.
- `ui.menu(title, items)`: list menu (1 to 32 strings); click = down, double click = up, long press = choose. Returns the chosen index (from 1). If the user exits (3 s hold) the script ends with `aborted`.
- `ui.confirm(text [, default_yes [, timeout_ms]])`: yes/no question, returns a boolean. Default `true`. With `timeout_ms` (up to 3600000) the display shows the remaining seconds and, if there is no gesture before the deadline, the answer is the default one: useful with autorun, where nobody might press anything. A click disables the deadline.
- `ui.wait_button()`: waits for a gesture and returns `"click"`, `"double"` or `"long"`.

## `sys`

- `sys.sleep(ms)`: interruptible pause.
- `sys.wait_host([ms])`: waits for the PC to mount the keyboard; `true`, or `false` on timeout (default: 24 hours, in practice until you exit with a 3-second press).
- `sys.log(text)`: writes to the log, also on the microSD (`/logs/grillo.log`, tag `script`). When the time is known, the line in the file starts with the local date and time.
- `sys.serial()`: the dongle's USB serial number (12 hex digits, the MAC); identifies its serial port on the PC.
- `sys.time()`: UTC seconds since 1970, or `nil` if the time is not known. The dongle has no clock battery: the time is set by NTP (when an app connects to a network) or by `host.clock()`, and stays valid until power-off.
- `sys.date([format])`: local time (`timezone` from `config.lua`) formatted like `strftime`; default `"%Y-%m-%d %H:%M:%S"`. `nil` if the time is not known; `""` if the result exceeds 63 characters.
- `sys.set_time(seconds)`: sets the time (UTC seconds since 1970). Error for a value before 2023. Usually you use `host.clock()`.

Examples: `payloads/examples/basics/clock.lua` (known time, formats, elapsed time) and `payloads/examples/host/clock-from-pc.lua` (time from the PC with `host.clock()`).

## `led`

- `led.rgb(r, g, b)`: fixed color, components 0..255.
- `led.effect(name [, color [, period_ms]])`: `"off"`, `"solid"`, `"pulse"`, `"blink"`, `"rainbow"`. Color as an integer `0xRRGGBB` (default: the app's accent color), period default 1000.
- `led.off()`.

## `fs`: reading files (read-only)

Only under `/payloads` and `/data` on the microSD; any other path, or one containing `..`, is an error.

- `fs.read(path)`: file contents as a string, or `nil, "cannot open"`. Files over 16 KB: error.
- `fs.lines(path)`: iterator over the lines (without `\r\n`). A line over 255 characters arrives split into several consecutive pieces, without error. Error if the file cannot be opened.
- `fs.list(path)`: table of names (hidden files excluded), or `nil, "cannot open"`.
- `fs.exists(path)`: boolean.

## `disk`: the microSD as a disk on the PC

While the disk is attached, the dongle exposes keyboard and disk together (HID+MSC profile) and the microSD is read-only for the PC. The dongle re-enumerates, so `disk.attach` waits for the PC to remount the keyboard.

- `disk.attach([timeout_ms])`: default 15000. Error if the SD is not mounted.
- `disk.wait_eject([timeout_ms])`: default 120000. Returns `"eject"` if the PC ejected the disk, `"click"` if the user pressed the button, `nil` on timeout.
- `disk.detach()`: goes back to the keyboard-only profile.

If the script ends with the disk still attached, the dongle detaches it anyway. The commands to copy and eject on the PC are typed by the script with `kb.type`: see [Copy files to the PC](../how-to/copy-files.md).

## `host`: reply from the PC (primitives)

The PC can reply to the dongle with a line `@G<nonce>:<code>` written to the CDC serial port. The dongle accepts only that line, with the expected nonce: from the PC come "finished" and a number, never the output of the commands. Every other line is discarded. Usually you use the `host` library below, not these primitives.

- `host.expect(nonce)`: expects a reply with this nonce (1..15 letters or digits). It must be called **before** typing the command, otherwise a fast reply would be lost.
- `host.wait([timeout_ms [, on_click]])`: waits for the reply (default 60000 ms). Returns the exit code and the received text (max 256 bytes; the dongle replaces with `_` everything that is not a letter, accented ones included, a digit, `. _ : -` or a space), or `nil` on timeout. With `on_click` true, a button click returns `false` and the wait stays armed: you can call `host.wait` again for the same reply. Error `call host.expect() first` if `expect` is missing.
- `host.append(id, date, label, value)`: appends a line `date,script,label,value` to `/logs/host/<id>.csv` on the SD. `id` and `label` are sanitized (only `A-Z a-z 0-9 _ -`, 24 characters); at most 64 files and 256 KB per file. With the disk attached (`disk.attach`) the line waits in RAM (at most 16) and is written at `disk.detach`.

## `host` library

In `sd-template/payloads/lib/host.lua`. Types a command in the PC terminal and tells when it has finished and with which exit code.

```lua
local host = require("host")
host.shell("powershell")      -- or "sh"; once, with the terminal in the foreground
local code = host.run([[Start-Process notepad -Wait]])
if code ~= 0 then error("notepad: " .. tostring(code)) end
```

- `host.shell(type)`: `"powershell"` or `"sh"`. First starts the shell in the foreground terminal: types `powershell -NoProfile` (also works from `cmd.exe`; from PowerShell it is a nested session) or `exec sh` (POSIX sh in place of fish, zsh, bash), with no profile or rc files, so the user's aliases and functions do not change the commands. Then types, once, the code that finds the dongle's serial port from its serial number and checks the channel; error if the PC does not respond. The terminal stays in the started shell. The shell is started once per script, even if both `host` and `pcdisk` ask for it.
- `host.run(command [, {timeout = ms}])`: types the command followed by a tail that sends the exit code. Returns the code, or `nil` on timeout (default 60000 ms).
- `host.sync([options])`: waits for the shell to have run everything typed before.
- `host.get(expression [, {timeout}])`: evaluates the expression in the PC shell and returns the result as a string (max 256 sanitized bytes), or `nil`. Multiple lines are joined with spaces.
- `host.machine([expression])`: sets the machine id for the logs and the PC's date. Without an argument it uses the MAC of the first physical network adapter (in address order): it belongs to the hardware, so it differs even between PCs cloned with Clonezilla, while `MachineGuid` and the host name are copied with the disk. Returns the id.
- `host.clock([{timeout}])`: reads the UTC time from the PC (`[DateTimeOffset]::UtcNow` in PowerShell, `date +%s` in sh), sets the dongle's clock and returns `sys.date()`. From then on the lines of `/logs/grillo.log` and `sys.date()` have the date. Error if the PC does not respond.
- `host.input(prompt [, {timeout, status}])`: text written by the user on the PC keyboard. The dongle types a command that shows `prompt` and reads a line (`Read-Host` in PowerShell, `read` in sh), shows `status` (default "Type on the PC, then click") and stops. The user types and presses the dongle's button: the dongle presses Enter for them and resumes. Enter on the keyboard works too. Returns the text (sanitized as in `host.get`: accented letters stay in UTF-8, symbols like `,` `;` `"` `$` become `_`), or `nil` on timeout (default 600000 ms). In sh the arrow keys do not move the cursor and would end up in the text; Backspace works.
- `host.log(label, expression)`: like `host.get`, then appends the line to `/logs/host/<id>.csv` and returns the value. Without `host.machine` the file is `unknown.csv`.

```lua
host.shell("powershell")
host.machine()
host.log("serial", [[(Get-CimInstance Win32_BIOS).SerialNumber]])
host.log("uuid", [[(Get-CimInstance Win32_ComputerSystemProduct).UUID]])
```

Never retype a value read from the PC into a shell command without checking it: it can contain whatever the person who configured the machine chose.

The command must be blocking: with `Start-Process` without `-Wait`, or `open` on macOS, the code arrives as soon as the program starts. Do not write `${` in the command. The port is found like this: on Windows from the parent device of the COM port (which contains the serial number), on Linux in `/dev/serial/by-id/*<serial>*`, on macOS in `/dev/cu.usbmodem<serial><n>`. It also works with several dongles connected. On Windows and Linux it has not been verified on a real PC; the dongle side of the channel has (reply with the right nonce accepted, with a wrong nonce discarded).

## `pcdisk` library

It is not in the firmware but in `sd-template/payloads/lib/pcdisk.lua`: `require("pcdisk")` after copying it to the SD.

- `pcdisk.run(body [, {timeout = ms}])`: starts PowerShell or sh in the terminal open on the PC (like `host.shell`), attaches the SD, types a line that finds the disk, runs `body` and ejects; then waits for the eject (default 180000 ms) and detaches. Returns `"eject"` or `"click"`, error if the time runs out.
- `body` is a line of PowerShell (`kb.os()` = `windows`) or POSIX shell (`linux`, `mac`); `$d` is the SD root without a trailing slash. Do not use `${` in the body.
- The disk is recognized by the `.grillo` file in the root. Linux support (udisks automount paths) is not verified.
- `options.mount`: `"auto"` (default) uses the system's automatic mounting; `"sudo"` (Linux only) mounts the SD manually at `/mnt/grillo`, read-only, and unmounts and ejects it at the end. The default can be set in `config.lua` with `pcdisk = { mount = "sudo" }`.
- With `mount = "sudo"` the password is taken from `secrets.sudo` in `config.lua` (another name with `options.secret`) and typed with `kb.type_secret`. First the dongle runs `sudo -k; sudo -v`, so the prompt always appears. The volume is recognized by the `GRILLO-SD` label, not by `.grillo`, which cannot be read before mounting. If the password is wrong, the following commands end up in the sudo prompt: check `secrets.sudo`. Not verified on a real distribution.

## `cfg(key [, default])`

Reads a value from `config.lua` as a string (e.g. `cfg("keyboard.layout")`). Returns `default` if the key is missing. `secrets.*` keys cannot be read from here.

## `require(name)`

Loads `/payloads/lib/name.lua` only once (subsequent calls return the same value). `name` can contain only letters, digits, `_` and `-`. Two modules that require each other give the error `require loop on module 'name'`.

```lua
local common = require("common")
common.pause("Press when ready")
```
