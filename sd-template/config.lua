-- Grillo configuration. This file is a Lua script that must return a
-- table. Every key is optional: each commented-out line below shows the
-- default value, used when the key is missing. To change a value, remove the
-- leading "--" and edit it.
-- Full reference: docs/reference/config-lua.md

return {
  -- Known WiFi networks (WPA2/WPA3-Personal only: an entry without pass is ignored).
  -- Order does not matter: with several networks in range you pick one on the display.
  -- trusted = true: safe network, the web apps do not ask for the PIN (default false).
  -- Default: no networks.
  wifi = {
    -- { ssid = "Home", pass = "password", trusted = true },
    -- { ssid = "Office", pass = "other-password" },
  },

  -- Hotspot created when no known network is reachable. Default ssid:
  -- "Grillo-XXXX" (last digits of the MAC). A pass shorter than 8 characters
  -- is replaced by the default one. trusted = true: no PIN on the hotspot,
  -- use only with a pass of your own.
  -- hotspot = { ssid = "Grillo-XXXX", pass = "grillo-dongle", trusted = false },

  -- App started at power-up after a countdown that can be canceled
  -- (seconds). Default: no app, the launcher opens.
  -- autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },

  -- PC keyboard: defaults for injector scripts and Web HID
  -- (a script header overrides them). layout: "us", "it" or
  -- "colemak"; os: "windows", "linux" or "mac"; timings in ms (0..60000).
  -- start_delay applies to the injector only.
  -- keyboard = {
  --   layout = "us",
  --   os = "windows",
  --   key_delay = 8,
  --   hold = 5,
  --   jitter = 0,
  --   line_delay = 0,
  --   start_delay = 1000,
  -- },

  -- Passwords typed by kb.type_secret("name"): the script never reads them
  -- as a string. Plaintext credentials on the SD: accepted risk.
  -- sudo is the one used by the pcdisk library with mount = "sudo".
  -- Default: none.
  -- secrets = { admin = "...", sudo = "..." },

  -- How lib/pcdisk.lua mounts the SD on a Linux PC: "auto" (automount) or
  -- "sudo" (manual mount).
  -- pcdisk = { mount = "auto" },

  -- Display: brightness 0..100, rotation 0 or 180 (90 and 270 not supported).
  -- ui = { brightness = 80, rotation = 0 },

  -- LED: brightness 0..100.
  -- led = { brightness = 30 },

  -- Web app PIN. true: PIN asked on networks not marked trusted.
  -- false: no PIN on any network, anyone on the same network (or
  -- on the hotspot) can use the page.
  -- webhid = { pin = true },
  -- webinjector = { pin = true },
  -- webfiles = { pin = true },

  -- POSIX time zone (Europe/Rome, with daylight saving time) for logs, sys.date()
  -- and Netscan CSV names; NTP server used on every connection to a network.
  -- timezone = "CET-1CEST,M3.5.0,M10.5.0/3",
  -- ntp = "pool.ntp.org",

  -- Netscan: host for the "Web" check (must answer 204 to /generate_204).
  -- netscan = { probe_host = "connectivitycheck.gstatic.com" },
}
