# Grillo

Firmware modulare ad app per il LilyGO T-Dongle-S3 (variante con LCD, senza PSRAM).

Il nome viene dal grillo parlante: un piccolo aiutante da tenere in tasca.

Le app previste sono:

- un keystroke injector che esegue script Lua presi dalla microSD, pensato per l'installazione dei kiosk;
- una tastiera e un mouse controllabili da pagina web;
- uno scanner di rete;
- la microSD esposta come disco USB.

> Stato: bring-up hardware completato. Display, pulsante, LED e SD funzionano. Il framework ad app è in sviluppo.

## Requisiti

- ESP-IDF v6.1 (installato in `~/esp/esp-idf`)
- microSD formattata FAT32

## Build e flash

```sh
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
python3 tools/serial_log.py /dev/cu.usbmodemXXXX 10   # log non interattivo
```

Per entrare in download mode, tieni premuto il pulsante mentre inserisci il dongle. Quando il firmware usa la USB come dispositivo HID o MSC, la porta seriale di ESP-IDF non è disponibile, quindi questo passaggio diventa necessario.

## VS Code

IntelliSense legge `build/compile_commands.json`. Esegui `idf.py build` (oppure `idf.py reconfigure`) almeno una volta: dopo, gli include si risolvono.
