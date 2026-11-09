-- Configurazione di Grillo. Questo file è uno script Lua che deve
-- restituire una tabella. Tutte le chiavi sono facoltative.

return {
  -- Reti WiFi note, in ordine di preferenza (solo WPA2/WPA3-Personal)
  wifi = {
    -- { ssid = "Ufficio", pass = "password" },
  },

  -- Hotspot creato dal dongle se nessuna rete nota è raggiungibile.
  -- ssid omesso = "Grillo-XXXX" (ultime cifre del MAC).
  hotspot = { pass = "cambiami123" },

  -- App avviata automaticamente all'accensione, con countdown annullabile.
  -- autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },

  -- Default per gli script dell'injector (sovrascrivibili dall'header)
  keyboard = { layout = "it", os = "windows", key_delay = 8, hold = 5 },

  ui = { brightness = 80 },
  led = { brightness = 30 },
}
