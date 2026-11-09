# config.lua

`/config.lua` sulla microSD è uno script Lua che restituisce una tabella. Tutte le chiavi sono facoltative: se il file manca o contiene un errore valgono i default (l'errore, con file e riga, è nel log e sulla console di debug).

Il file è eseguito una volta all'avvio in una VM sandbox, poi appiattito in coppie chiave/valore (`keyboard.layout`, `wifi.1.ssid`) e la VM è chiusa. Le modifiche valgono al riavvio del dongle, oppure dopo `config reload` dalla [console di debug](console-debug.md).

Il modello è `sd-template/config.lua`.

## Chiavi

| Chiave | Tipo | Default | Significato |
| --- | --- | --- | --- |
| `wifi` | elenco di `{ssid, pass}` | vuoto | Reti note, in ordine di preferenza. Solo WPA2/WPA3-Personal. |
| `hotspot.ssid` | testo | `Grillo-XXXX` | Nome dell'hotspot creato se nessuna rete nota è raggiungibile (XXXX = ultime cifre del MAC). |
| `hotspot.pass` | testo | `grillo-dongle` | Password dell'hotspot. Se manca o è più corta di 8 caratteri si usa il default. |
| `autorun.app` | id app | nessuna | App da avviare all'accensione. Vedi [Autorun](../how-to/autorun.md). |
| `autorun.payload` | percorso | nessuno | Script da passare all'app (es. `kiosk/setup.lua`). |
| `autorun.delay` | secondi | 3 | Durata del conto alla rovescia annullabile. |
| `keyboard.layout` | `us`/`it`/`colemak` | `us` | Layout di default della tastiera del PC. |
| `keyboard.os` | `windows`/`linux`/`mac` | `windows` | OS di default del PC. |
| `keyboard.key_delay` | ms | 8 | Pausa tra i tasti. |
| `keyboard.hold` | ms | 5 | Durata della pressione di ogni tasto. |
| `keyboard.jitter` | ms | 0 | Variazione casuale della pausa. |
| `keyboard.line_delay` | ms | 0 | Pausa in più dopo ogni `\n`. |
| `keyboard.start_delay` | ms | 1000 | Attesa prima di far partire uno script. |
| `secrets` | tabella nome → testo | vuota | Password per `kb.type_secret`. Vedi [Password](../how-to/password.md). |
| `ui.brightness` | 0..100 | 80 | Luminosità del display. |
| `ui.rotation` | 0 o 180 | 0 | Rotazione del display (90 e 270 non sono supportati). |
| `webhid.pin` | booleano | `true` | `false`: il Web HID non chiede il PIN. Vedi [Sicurezza](../explanation/sicurezza.md). |
| `led.brightness` | 0..100 | 30 | Luminosità del LED. |

Le impostazioni `keyboard.*` sono i default degli script dell'injector e del Web HID; un'[intestazione di script](intestazione-script.md) le sovrascrive per quello script.

## Esempio

```lua
return {
  wifi = {
    { ssid = "Ufficio", pass = "password" },
    { ssid = "Laboratorio", pass = "altra-password" },
  },
  hotspot = { pass = "cambiami123" },
  autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },
  keyboard = { layout = "it", os = "windows", key_delay = 8 },
  secrets = { admin = "..." },
  ui = { brightness = 80, rotation = 180 },
  webhid = { pin = true },
}
```
