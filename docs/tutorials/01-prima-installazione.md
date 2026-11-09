# Prima installazione

Alla fine di questo tutorial avrai il firmware sul dongle, una microSD preparata e un esempio eseguito su un PC. Serve un T-Dongle-S3 con LCD, una microSD e un Mac o Linux con ESP-IDF v6.1.

## 1. Compila e flasha

```sh
. ~/esp/esp-idf/export.sh
idf.py build
```

Collega il dongle tenendo premuto il pulsante (download mode), poi trova la porta (`ls /dev/cu.usbmodem*` su macOS) e flasha:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash
```

Scollega e ricollega il dongle senza premere il pulsante: sul display compare il launcher con le app.

## 2. Prepara la microSD

1. Formatta la scheda in FAT32 (exFAT non è supportato).
2. Copia il contenuto della cartella `sd-template/` nella radice della scheda. Dovresti avere `/config.lua`, `/payloads/examples/`, `/payloads/lib/` e `/data/`.
3. Espelli la scheda dal computer e inseriscila nel dongle.

Non servono i file nascosti che macOS crea (`._*` e simili): l'injector li ignora.

## 3. Imposta il layout del PC di destinazione

Apri `/config.lua` sulla scheda e imposta la tastiera del PC su cui userai il dongle:

```lua
keyboard = { layout = "it", os = "windows" },
```

Layout disponibili: `us`, `it`, `colemak`. Sistemi operativi: `windows`, `linux`, `mac`. Se sbagli il layout, i caratteri digitati saranno quelli di un'altra tastiera.

## 4. Esegui l'esempio "Hello"

1. Collega il dongle al PC di destinazione. Il PC lo riconosce come tastiera.
2. Nel launcher scegli **Injector** (click per scorrere, pressione lunga per avviare).
3. Apri `examples`, scegli **Hello**: compare una scheda con layout e sistema operativo dello script (Hello dichiara `colemak · mac`).
4. Sul PC apri un campo di testo e dagli il focus.
5. Con una pressione lunga sul dongle lo script parte e digita `Hello from Grillo!`.

Per interrompere uno script in corso, tieni premuto il pulsante 3 secondi.

Se compare `U+XXXX not typeable`, il layout scelto non ha quel carattere: vedi [Scegliere layout e sistema operativo](../how-to/layout-e-os.md).

## Prossimo passo

[Il tuo primo script](02-primo-script.md).
