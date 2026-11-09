# Architettura

Grillo è un firmware ESP-IDF v6.1 (C, FreeRTOS) per un dispositivo con vincoli stretti: ESP32-S3 senza PSRAM, uno schermo 160x80, un solo pulsante. Le scelte seguenti derivano da questi vincoli.

## Strati

```
apps/         injector, webhid, netscan, usbdisk, launcher        <- una per attività
components/   appfw  ui  gfx  display  input  led         <- interfaccia
              hid  usbdev  net  storage  config  luart    <- servizi
main/         avvio, tabella app, console di debug
```

Le app dipendono dai servizi, mai l'una dall'altra. Per questo ogni app può sparire senza toccare le altre.

## Framework ad app

Un'app è un descrittore statico (`app_desc_t`) con una funzione `run()` in un task dedicato. Il gestore (`appmgr`) avvia un'app alla volta e, quando `run()` ritorna, rilascia d'ufficio tutte le risorse condivise: profilo USB, SD prestata, livello LED, rete. Il motivo è che un'app dimentica di rilasciare qualcosa soprattutto quando esce per un errore, e un dongle che resta in un profilo USB sbagliato è difficile da diagnosticare senza schermo grande né tastiera.

L'ingresso è un solo pulsante, ridotto a gesti: click, doppio click, pressione lunga; 3 secondi di pressione escono sempre, dall'app o dallo script. L'uscita è un evento (`APP_EV_EXIT`) che ogni attesa bloccante consegna, così nessuna app può ignorarla.

## Profili USB

TinyUSB espone o tutto o niente, e riconfigurare la USB richiede di ri-enumerare il dispositivo. Per questo l'app dichiara il profilo che le serve (HID, HID + disco, solo disco) e il sistema lo applica prima di avviarla. Con profilo spento la PHY torna alla USB-Serial-JTAG (log e flash).

La microSD è un'unica risorsa: o la usa il firmware (FAT su `/sd`), o è prestata all'host come disco. Il passaggio smonta e rimonta, e prima si svuota il log per non perdere righe.

## Configurazione e script

`/config.lua` è uno script Lua che restituisce una tabella; all'avvio è eseguito in una VM sandbox e appiattito in coppie chiave/valore, poi la VM si chiude per liberare RAM. Così le app leggono con `config_str("keyboard.layout", "us")` e il resto della memoria non è occupato da un interprete.

Lo scripting dell'injector usa una VM diversa, per ogni esecuzione: librerie limitate (niente `io`, `os`), limite di memoria di 96 KB, interrompibile in qualunque istruzione. Il limite di memoria è deliberato: con ~250 KB di heap libero un script non deve poter togliere memoria al WiFi o alla USB.

## Memoria

Misure sul dispositivo (a memoria di sessione, non riprodotte in un test automatico): heap libero circa 316 KB a riposo, 252 KB dopo l'uscita da un'app che ha usato la rete, circa 193 KB con il server Web HID attivo; nessuna perdita tra cicli ripetuti di apertura e chiusura.

## Rete

Il gestore WiFi (`net`) è a lease: ogni app che ne ha bisogno lo richiede e lo rilascia; il WiFi resta acceso finché c'è almeno un lease. In modalità automatica cerca le reti, sceglie la migliore tra quelle note in `config.lua`, e se non ne trova crea un hotspot. Il risparmio energetico del WiFi è disattivato: attiva, la pagina web del Web HID impiegava secondi a rispondere.

## Test

La logica pura (tabelle dei layout, gesti del pulsante, parsing di NBNS e mDNS, ricerca OUI, CSV di Netscan) gira sul PC: `make -C test/host`. Il resto si verifica sul dispositivo con la console di debug e gli screenshot (`tools/grillo.py shot`).
