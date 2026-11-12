**English** · [Italiano](../it/tutorials/01-prima-installazione.md)

# First install

By the end of this tutorial you will have the firmware on the dongle, a prepared microSD and an example run on a PC. You need a T-Dongle-S3 with LCD, a microSD and a Mac or Linux machine with ESP-IDF v6.1.

## 1. Build and flash

```sh
. ~/esp/esp-idf/export.sh
idf.py build
```

Plug in the dongle while holding the button (download mode), then find the port (`ls /dev/cu.usbmodem*` on macOS) and flash:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash
```

Unplug and replug the dongle without pressing the button: the display shows the launcher with the apps.

## 2. Prepare the microSD

1. Format the card as FAT32 (exFAT is not supported).
2. Copy the contents of the `sd-template/` folder to the root of the card. You should have `/config.lua`, `/payloads/examples/`, `/payloads/lib/` and `/data/`.
3. Eject the card from the computer and insert it into the dongle.

The hidden files macOS creates (`._*` and similar) are not needed: the injector ignores them.

## 3. Set the target PC's layout

Open `/config.lua` on the card and set the keyboard of the PC you will use the dongle on:

```lua
keyboard = { layout = "it", os = "windows" },
```

Available layouts: `us`, `it`, `colemak`. Operating systems: `windows`, `linux`, `mac`. If you get the layout wrong, the typed characters will be those of a different keyboard.

## 4. Run the "Hello" example

1. Plug the dongle into the target PC. The PC recognizes it as a keyboard.
2. In the launcher choose **Injector** (click to scroll, long press to start).
3. Open `examples`, choose **Hello**: a card appears with the script's layout and operating system (Hello declares `colemak · mac`).
4. On the PC open a text field and give it focus.
5. With a long press on the dongle the script starts and types `Hello from Grillo!`.

To stop a running script, hold the button for 3 seconds.

If `U+XXXX not typeable` appears, the chosen layout does not have that character: see [Choose layout and operating system](../how-to/layout-and-os.md).

## Next step

[Your first script](02-first-script.md).
