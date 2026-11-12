**English** · [Italiano](../it/how-to/flash-una-release.md)

# Flash a release without building

The [releases](../../.github/workflows/release.yml) published on GitHub contain `grillo-vX.Y.Z-merged.bin`: bootloader, partition table and app already merged into a single image, ready to be written starting at address 0x0. ESP-IDF is not needed, only `esptool`.

```sh
pip install esptool
```

Plug in the dongle while holding the button (download mode), then find the port and flash:

```sh
esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX write_flash 0x0 grillo-vX.Y.Z-merged.bin
```

Unplug and replug the dongle without pressing the button: the display shows the launcher.

To prepare the microSD, continue from [First install](../tutorials/01-first-install.md#2-prepare-the-microsd).

Release images are built with `sdkconfig.release`: no [debug console](../reference/debug-console.md) on the serial port (see [Security](../explanation/security.md)). To develop or to have the console, build from source as in the [README](../../README.md#quick-start).

To update an already flashed dongle: unplug it, replug it while holding the button (download mode) and repeat the `write_flash` command.
