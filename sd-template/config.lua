-- Configurazione di Grillo. Questo file è uno script Lua che deve
-- restituire una tabella. Tutte le chiavi sono facoltative: ogni riga
-- commentata qui sotto mostra il valore predefinito, usato quando la chiave
-- manca. Per cambiare un valore togli il "--" iniziale e modificalo.
-- Riferimento completo: docs/reference/config-lua.md

return {
  -- Reti WiFi note (solo WPA2/WPA3-Personal: una voce senza pass è ignorata).
  -- L'ordine non conta: con più reti visibili si sceglie dal display.
  -- trusted = true: rete sicura, le app web non chiedono il PIN (default false).
  -- Predefinito: nessuna rete.
  wifi = {
    -- { ssid = "Casa", pass = "password", trusted = true },
    -- { ssid = "Ufficio", pass = "altra-password" },
  },

  -- Hotspot creato se nessuna rete nota è raggiungibile. ssid predefinito:
  -- "Grillo-XXXX" (ultime cifre del MAC). Una pass più corta di 8 caratteri
  -- è sostituita da quella predefinita. trusted = true: niente PIN sull'hotspot,
  -- da usare solo con una pass tua.
  -- hotspot = { ssid = "Grillo-XXXX", pass = "grillo-dongle", trusted = false },

  -- App avviata all'accensione dopo un conto alla rovescia annullabile
  -- (secondi). Predefinito: nessuna app, si apre il launcher.
  -- autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },

  -- Tastiera del PC: default degli script dell'injector e del Web HID
  -- (un'intestazione di script li sovrascrive). layout: "us", "it" o
  -- "colemak"; os: "windows", "linux" o "mac"; tempi in ms (0..60000).
  -- start_delay vale solo per l'injector.
  -- keyboard = {
  --   layout = "us",
  --   os = "windows",
  --   key_delay = 8,
  --   hold = 5,
  --   jitter = 0,
  --   line_delay = 0,
  --   start_delay = 1000,
  -- },

  -- Password digitabili da kb.type_secret("nome"): lo script non le legge
  -- mai come stringa. Credenziali in chiaro sulla SD: rischio accettato.
  -- sudo è quella usata dalla libreria pcdisk con mount = "sudo".
  -- Predefinito: nessuna.
  -- secrets = { admin = "...", sudo = "..." },

  -- Come lib/pcdisk.lua monta la SD sul PC Linux: "auto" (automount) o
  -- "sudo" (mount a mano).
  -- pcdisk = { mount = "auto" },

  -- Display: luminosità 0..100, rotazione 0 o 180 (90 e 270 non supportati).
  -- ui = { brightness = 80, rotation = 0 },

  -- LED: luminosità 0..100.
  -- led = { brightness = 30 },

  -- PIN delle app web. true: PIN chiesto sulle reti non marcate trusted.
  -- false: niente PIN su nessuna rete, chiunque sulla stessa rete (o
  -- sull'hotspot) usa la pagina.
  -- webhid = { pin = true },
  -- webinjector = { pin = true },
  -- webfiles = { pin = true },

  -- Fuso orario POSIX (Europe/Rome, con l'ora legale) per i log, sys.date()
  -- e i nomi dei CSV di Netscan; server NTP usato a ogni collegamento a una rete.
  -- timezone = "CET-1CEST,M3.5.0,M10.5.0/3",
  -- ntp = "pool.ntp.org",

  -- Netscan: host del controllo "Web" (deve rispondere 204 a /generate_204).
  -- netscan = { probe_host = "connectivitycheck.gstatic.com" },
}
