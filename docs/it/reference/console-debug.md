[English](../../reference/debug-console.md) · **Italiano**

# Console di debug

`components/devcon` espone una console a righe sulla porta seriale del dongle, usata da `tools/grillo.py`. Serve a sviluppare: pilotare il pulsante, vedere lo schermo, leggere file della microSD.

Quando TinyUSB prende la PHY (profili HID e disco) la USB-Serial-JTAG non è più disponibile. Con l'opzione `CONFIG_GRILLO_USB_DEBUG_CDC=y` (default, menu "Grillo USB") la console e i log di ESP-IDF passano per una porta CDC-ACM che il dongle espone con ogni profilo. Un build di release senza quell'opzione non ha la console mentre l'app usa HID o MSC.

```sh
python3 tools/grillo.py cmd state          # esegue un comando
python3 tools/grillo.py shot schermo.png   # screenshot 4x
python3 tools/grillo.py log 10             # log per 10 secondi
```

Opzioni: `--port /dev/cu.usbmodemXXX`, `-q` (nasconde le righe di log). Il client apre la porta senza toccare DTR/RTS, così il dongle non si resetta.

## Comandi

| Comando | Effetto |
| --- | --- |
| `state` | App corrente, profilo USB, SD, heap. |
| `apps` | Elenca le app. |
| `launch <id> [arg]` | Avvia un'app (es. `launch injector`). |
| `exit` | Esce dall'app corrente, come la pressione di 3 secondi. Se l'app non risponde, un secondo `btn hold` riavvia il dongle. |
| `btn click\|double\|long\|hold\|armed` | Simula un gesto del pulsante. `armed` mostra solo il segnale della pressione lunga (linea bianca), senza evento; la linea resta finché un `btn long` o un `btn hold` non la toglie. |
| `shot` | Scarica lo schermo (RGB565 in base64). |
| `ls [percorso]` | Elenca una cartella. |
| `cat <percorso>` | Stampa un file (massimo 16 KB). |
| `config reload \| get <chiave>` | Ricarica `config.lua` (solo dal launcher: con un'app aperta risponde `exit the app first`; gli errori compaiono sul display e in `state`) o legge una chiave. |
| `log <tag\|*> <livello>` | Imposta il livello di log (`none`, `error`, `warn`, `info`, `debug`, `verbose`). |
| `usb none\|hid\|hiddisk\|msc` | Cambia il profilo USB. |
| `reboot` | Riavvia il dongle. |

Per flashare un dongle in uso, esci prima dall'app (`grillo.py cmd exit`), poi `idf.py -p <porta> flash`.
