**English** · [Italiano](../it/how-to/aggiungere-unapp.md)

# Add an app

An app is a component in `apps/` that exports an `app_desc_t` descriptor. The contract is in `components/appfw/include/app.h`, summarized in [App C API](../reference/app-api.md).

1. Create `apps/name/` with `CMakeLists.txt`, `name.c` and `include/name.h`. Take `apps/usbdisk` as a minimal example.
2. Declare the descriptor: `id` (used by `autorun.app`), `name`, 12x12 icon, accent color, requirements (`APP_REQ_SD`, `APP_REQ_NET`, …), USB profile, stack size, `run` function. The accent color also identifies the app on the LED: pick one far from those already in use (injector orange `0xFF8A20`, Web HID blue `0x4070FF`, Netscan cyan `0x20D0D0`, USB disk magenta `0xE040B0`) and from the system ones (Grillo green `0x7CFC00` and `UI_OK`, red `UI_ERR`, amber `UI_WARN`).
3. Add the app to `main/app_list.c` and the component to `PRIV_REQUIRES` in `main/CMakeLists.txt`.
4. In `run(ctx)`: draw with the functions in `ui.h`, wait for events with `app_next_event`, exit when `app_should_exit(ctx)` or `APP_EV_EXIT`. Do not go long without waiting for events (long computations, network calls without a timeout): the exit request arrives only there, and otherwise the user is left with a reboot. For the network use `app_net_acquire`, which lets the user choose the network when there is more than one.

The USB profile is the combination of interfaces the app needs (see [App C API](../reference/app-api.md#descriptor)): if none of the ready-made profiles fits, compose the `USB_F_*` mask. To change it midway use `app_usb_apply`, which also moves the SD.

For an app driven from a web page use the components `websrv` (server, PIN, one session at a time) and `qr` (QR codes on the display, also for the hotspot credentials), `webui` (the screen with QR, address and PIN) and `webtheme` (common page theme): `apps/webhid`, `apps/webinjector` and `apps/webfiles` are the examples. The page must be compressed at build time as in `apps/webinjector/CMakeLists.txt`, with a file name different from the other apps' (the embedded symbol derives from the name), and registered with `websrv_add_page`; then `webtheme_register()`.

When `run` returns, the system automatically releases the USB profile, lent SD, LED level and network: there is no need to do it by hand.
