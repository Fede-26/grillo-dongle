**English** · [Italiano](../it/how-to/layout-e-os.md)

# Choose layout and operating system

The dongle sends key positions, not characters: the PC interprets them with its own layout. To type `@` or `è` correctly, the dongle must know which layout and which operating system the PC has.

## For one script

In the header:

```lua
--- @layout it
--- @os windows
```

Or in the code, to switch midway through the script: `kb.layout("us")`, `kb.os("mac")`.

## For all scripts

Default in `config.lua`:

```lua
keyboard = { layout = "it", os = "windows" },
```

## For Web HID

With the two selectors on the page.

## Which one to choose

| The PC has | Layout | OS |
| --- | --- | --- |
| Italian keyboard (Windows, Linux) | `it` | `windows` or `linux` |
| Italian keyboard on Mac ("Italian - Pro") | `it` | `mac` |
| US keyboard | `us` | any |
| Colemak layout | `colemak` | the PC's |

The OS matters even with the same layout: the AltGr/Option levels, the dead keys and the fallbacks change. For details see [Layouts, operating systems and dead keys](../explanation/layouts-and-dead-keys.md).

## If a character does not come out

- `U+XXXX not typeable with layout NAME`: the character does not exist in the layout and there is no fallback for that operating system. On macOS there are no fallbacks.
- One character comes out in place of another (e.g. `<` and `\` swapped on Mac): the Mac recognized the dongle as a different keyboard type than expected. See `tools/layouts/README.md` (`mac_iso` option).
