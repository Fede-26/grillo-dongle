**English** · [Italiano](../it/tutorials/02-primo-script.md)

# Your first script

In this tutorial you write a Lua script that opens Notepad on Windows and types some text into it, showing progress on the dongle's display.

Prerequisites: [First install](01-first-install.md) completed.

## 1. Create the file

On the microSD create `/payloads/notepad.lua`:

```lua
--- @name Notepad
--- @desc Opens Notepad and writes a line.
--- @layout it
--- @os windows

ui.status("Opening Notepad")
kb.press("GUI", "r")          -- Win+R
sys.sleep(500)
kb.type("notepad\n")
sys.sleep(1500)               -- waits for the window to open

ui.status("Typing")
kb.type("Ciao dal grillo, oggi è ${giorno}.\n", {giorno = "lunedì"})
ui.status("Done")
```

The first `--- @...` lines are the [header](../reference/script-header.md): they tell the injector how to show the script and which keyboard to type with.

## 2. Run it

1. Insert the card into the dongle and plug it into the PC.
2. Injector → **Notepad** → long press to start.
3. The PC opens Notepad and the dongle types. `è` and `ì` come out right because the layout is `it`.

## 3. What you used

- `kb.press("GUI", "r")` presses the keys together: [`kb.press`](../reference/lua-api.md#kbpresskey-).
- `kb.type` writes text; `${giorno}` is replaced by the value from the table.
- `sys.sleep(ms)` waits: the pauses are needed because the PC is slower than the dongle.
- `ui.status` updates the text on the display.

## 4. If something goes wrong

- Notepad does not open: increase the pauses; or set `--- @start_delay 3000` in the header to give the PC more time after plugging in.
- The characters are wrong: the layout or operating system in the header does not match the PC. See [Choose layout and operating system](../how-to/layout-and-os.md).
- Error with line and file on the display: read the message; the full log is in `/logs/grillo.log` on the card.

All functions are listed in the [Lua API](../reference/lua-api.md).
