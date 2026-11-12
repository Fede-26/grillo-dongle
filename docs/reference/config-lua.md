# config.lua

`/config.lua` sulla microSD è uno script Lua che restituisce una tabella. Tutte le chiavi sono facoltative: se il file manca o contiene un errore valgono i default (l'errore, con file e riga, è nel log e sulla console di debug).

Il file è eseguito in una VM sandbox, poi appiattito in coppie chiave/valore (`keyboard.layout`, `wifi.1.ssid`) e la VM è chiusa. Viene riletto, con luminosità, rotazione e LED riapplicati:

- all'avvio;
- all'uscita da USB Disk, perché dal PC lo si può aver modificato;
- con `config reload` dalla [console di debug](console-debug.md), solo mentre è aperto il launcher.

Un errore all'avvio è mostrato sul display (il testo lungo scorre); in seguito compare un avviso nella barra di stato. In ogni caso la configurazione resta vuota e valgono i default.

Il modello è `sd-template/config.lua`.

## Chiavi

| Chiave | Tipo | Default | Significato |
| --- | --- | --- | --- |
| `wifi` | elenco di `{ssid, pass, trusted}` | vuoto | Reti note. L'ordine non conta: con più reti visibili si sceglie dal display, altrimenti vale la più forte (vedi [WiFi](../how-to/wifi.md)). Solo WPA2/WPA3-Personal: una voce senza `pass` è ignorata. `trusted = true` (default `false`) marca la rete come sicura: le app web non chiedono il PIN (vedi [Reti sicure](../how-to/wifi.md#reti-sicure-niente-pin)). |
| `hotspot.ssid` | testo | `Grillo-XXXX` | Nome dell'hotspot creato se nessuna rete nota è raggiungibile (XXXX = ultime cifre del MAC). |
| `hotspot.pass` | testo | `grillo-dongle` | Password dell'hotspot. Se manca o è più corta di 8 caratteri si usa il default. |
| `hotspot.trusted` | booleano | `false` | `true`: sull'hotspot le app web non chiedono il PIN. Ha senso solo con una `hotspot.pass` tua: con quella di default chiunque conosca il firmware entra. |
| `autorun.app` | id app | nessuna | App da avviare all'accensione. Vedi [Autorun](../how-to/autorun.md). |
| `autorun.payload` | percorso | nessuno | Script da passare all'app (es. `kiosk/setup.lua`). |
| `autorun.delay` | secondi | 3 | Durata del conto alla rovescia annullabile. |
| `keyboard.layout` | `us`/`it`/`colemak` | `us` | Layout di default della tastiera del PC. |
| `keyboard.os` | `windows`/`linux`/`mac` | `windows` | OS di default del PC. |
| `keyboard.key_delay` | ms, 0..60000 | 8 | Pausa tra i tasti. |
| `keyboard.hold` | ms, 0..60000 | 5 | Durata della pressione di ogni tasto. |
| `keyboard.jitter` | ms, 0..60000 | 0 | Variazione casuale della pausa. |
| `keyboard.line_delay` | ms, 0..60000 | 0 | Pausa in più dopo ogni `\n`. |
| `keyboard.start_delay` | ms, 0..60000 | 1000 | Attesa prima di far partire uno script (solo injector). |
| `secrets` | tabella nome → testo | vuota | Password per `kb.type_secret` (`secrets.sudo` è quella usata da `pcdisk` con `mount = "sudo"`). Vedi [Password](../how-to/password.md). |
| `pcdisk.mount` | `auto`/`sudo` | `auto` | Come la libreria `pcdisk` monta la SD sul PC Linux. Vedi [API Lua](lua-api.md#libreria-pcdisk). |
| `ui.brightness` | 0..100 | 80 | Luminosità del display. |
| `ui.rotation` | 0 o 180 | 0 | Rotazione del display (90 e 270 non sono supportati). |
| `webhid.pin` | booleano | `true` | `false`: il Web HID non chiede il PIN su nessuna rete. Con `true` il PIN è chiesto solo sulle reti non marcate `trusted`. Vedi [Sicurezza](../explanation/sicurezza.md). |
| `webinjector.pin` | booleano | `true` | Come `webhid.pin`, per il Web Injector. |
| `webfiles.pin` | booleano | `true` | Come `webhid.pin`, per Web Files. |
| `timezone` | stringa POSIX | `CET-1CEST,M3.5.0,M10.5.0/3` | Fuso orario (il default è Europe/Rome, con l'ora legale) per le date di `/logs/grillo.log`, `sys.date()` e i nomi dei file di Netscan. |
| `ntp` | host | `pool.ntp.org` | Server NTP da cui il dongle prende l'ora quando è collegato a una rete come client. |
| `netscan.probe_host` | host | `connectivitycheck.gstatic.com` | Host contattato dal controllo "Web" di Network info (deve rispondere 204 a `/generate_204`). |
| `led.brightness` | 0..100 | 30 | Luminosità del LED. |

Le impostazioni `keyboard.*` sono i default degli script dell'injector e del Web HID; un'[intestazione di script](intestazione-script.md) le sovrascrive per quello script. Un valore fuori dall'intervallo è riportato al limite più vicino.

## Esempio

```lua
return {
  wifi = {
    { ssid = "Casa", pass = "password", trusted = true },
    { ssid = "Ufficio", pass = "altra-password" },
  },
  hotspot = { pass = "cambiami123" },
  autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },
  keyboard = { layout = "it", os = "windows", key_delay = 8 },
  secrets = { admin = "..." },
  ui = { brightness = 80, rotation = 180 },
  webhid = { pin = true },
}
```
