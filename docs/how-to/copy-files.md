**English** · [Italiano](../it/how-to/copiare-file.md)

# Copy files to the PC with the USB disk

A script can make the microSD appear as a disk on the PC (read-only), type the commands that copy the files, and then disconnect it. The PC must have a shell reachable from the keyboard.

Use the `pcdisk` library (`sd-template/payloads/lib/pcdisk.lua`, to be copied to `/payloads/lib` on the SD). It does the repetitive steps: attaches the disk, finds the drive on the PC from the `.grillo` file, runs your command, ejects, waits for the eject and detaches.

```lua
--- @name Copy installer
--- @layout it
--- @os windows

ui.status("Opening the shell")
kb.press("GUI", "r")
sys.sleep(500)
kb.type("powershell\n")
sys.sleep(1500)

local pcdisk = require("pcdisk")
pcdisk.run([[Copy-Item "$d\data\setup.exe" C:\Temp -Force]])
```

`$d` is the root of the SD without a trailing slash (`E:`). The body is a single line: chain multiple commands with `;`, because commands typed while another is running are queued and the dongle does not know when they finish. An example is in `sd-template/payloads/copy-installer.lua`.

Without the library you can use `disk.attach`, `disk.wait_eject` and `disk.detach` directly ([Lua API](../reference/lua-api.md#disk-the-microsd-as-a-disk-on-the-pc)).

## Things to know

- While the disk is attached the mouse does not work (`mouse.*` raises an error) and the firmware does not write to the card: the logs are flushed beforehand.
- `disk.wait_eject` also ends with a click on the button, useful if the eject is not detected.
- The commands to copy and eject depend on the PC: the dongle does not know them, you type them with `kb.type`.
- `pcdisk.run` first starts the right shell in the foreground terminal (`powershell -NoProfile` on Windows, `exec sh` on Linux and macOS), so the body works even if `cmd.exe` or fish is open. Opening the terminal is still up to the script, as in the example above.
- If the script ends or is interrupted with the disk still attached, the dongle detaches it.
