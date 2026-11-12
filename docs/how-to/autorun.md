**English** · [Italiano](../it/how-to/autorun.md)

# Run a script at power-on

With autorun the dongle starts an app (and a script) by itself, after a countdown you can cancel. It is meant for kiosks: plug in the dongle and it starts.

In `config.lua`:

```lua
autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },
```

- `app`: app identifier (`injector`, `webhid`, `usbdisk`).
- `payload`: path of the script relative to `/payloads`, for the injector.
- `delay`: seconds to wait, default 3.

At startup the display shows the app name, the script and a bar: a click cancels and leaves you in the launcher. When the time is up the script starts without the preview card.

With an unknown identifier the launcher shows `autorun: unknown app` and starts normally: check `python3 tools/grillo.py cmd apps` for the valid names.
