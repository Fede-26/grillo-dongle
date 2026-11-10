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
  keyboard = { layout = "it", os = "windows", key_delay = 8, hold = 5, jitter = 0, line_delay = 0, start_delay = 1000 },

  -- Password digitabili da kb.type_secret("nome"): lo script non le legge
  -- mai come stringa. Credenziali in chiaro sulla SD: rischio accettato.
  -- secrets = { admin = "...", sudo = "..." },  -- sudo: usata da pcdisk con mount = "sudo"

  -- Come lib/pcdisk.lua monta la SD sul PC Linux: "auto" (automount) o "sudo" (mount a mano)
  -- pcdisk = { mount = "auto" },

  -- rotation: 0 oppure 180 (90 e 270 non supportati)
  ui = { brightness = 80, rotation = 0 },

  -- pin = false: il Web HID non chiede il PIN. Chiunque sulla stessa rete
  -- (o sull'hotspot) potrebbe comandare il PC: usalo solo su reti fidate.
  webhid = { pin = true },
  led = { brightness = 30 },

  -- Fuso orario POSIX (default Europe/Rome, con l'ora legale) per i log, sys.date()
  -- e i nomi dei CSV di Netscan; server NTP usato a ogni collegamento a una rete
  -- timezone = "CET-1CEST,M3.5.0,M10.5.0/3",
  -- ntp = "pool.ntp.org",
  -- Netscan: host del controllo "Web"
  -- netscan = { probe_host = "connectivitycheck.gstatic.com" },
}
