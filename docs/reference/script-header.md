**English** · [Italiano](../it/reference/intestazione-script.md)

# Script header

Every `.lua` script in `/payloads` can start with a comment block that describes it and sets its parameters. The injector reads it without running the script, to show it in the list and apply the settings before starting.

```lua
--- @name Install kiosk
--- @desc Sets up the browser in kiosk mode.
--- @tags kiosk, setup
--- @layout it
--- @os windows
--- @key_delay 12
```

## Rules

- The header is the initial block of lines starting with `--`: it ends at the first line that is not a comment. Blank lines in the block are allowed.
- A line is a directive only if it starts with `--- @` (three dashes, space, `@`). Other comments are ignored.
- Format: `--- @key value`. An unknown key, an invalid or out-of-range numeric value, an unknown `@layout` or `@os` make the script unable to start, with the line number in the message.
- A missing parameter takes the value from `config.lua` (`keyboard` section), which in turn has a built-in default.

## Keys

| Key | Value | Effect |
| --- | --- | --- |
| `name` | text | Title in the list. If missing: file name without `.lua`. |
| `desc` | text (max 159 bytes: an accented letter counts as 2) | Description shown in the preview. If it does not fit in 3 lines it scrolls by itself one line at a time, with a thin bar on the right; a click advances it immediately. |
| `tags` | words separated by commas or spaces | Groups in the injector's "by tag" view. |
| `layout` | `us`, `it`, `colemak` | Keyboard layout of the target PC. |
| `os` | `windows`, `linux`, `mac` (also `win`, `macos`) | Operating system of the PC: selects AltGr/Option levels, dead keys and fallbacks. |
| `key_delay` | ms, 0..60000 | Pause between keys (default 8). |
| `hold` | ms, 0..60000 | Duration of each key press (default 5). |
| `jitter` | ms, 0..60000 | ± random ms on the pause (default 0). |
| `line_delay` | ms, 0..60000 | Extra pause after each `\n` (default 0). |
| `start_delay` | ms, 0..60000 | Wait after the PC has mounted the keyboard, before starting (default 1000). |
| `timeout` | seconds, 0..86400 | Stops the script if it runs longer, even during a `ui.menu` or a `ui.confirm`. |
| `led` | `effect [RRGGBB]` | LED effect during the run: `off`, `solid`, `pulse`, `blink`, `rainbow`, with an optional hex color. |

## Injector views

The list can be browsed by folder, by operating system or by tag. In the rows, the layout is abbreviated (`cmk` for Colemak) and separated from the operating system by `/`. The folder view follows folders at any depth; the OS and tag views look for scripts up to 3 levels below `/payloads`. The `lib` folder and hidden or AppleDouble files (`._*`) are skipped.

Each view shows at most 48 entries, in the order the card returns them before alphabetical sorting: beyond that, a warning appears and you should split the scripts into subfolders.

Before starting a script, the injector always shows a card with name, layout and OS: long press to start, double click to go back. With autorun the card does not appear.
