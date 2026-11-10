# Architettura

Grillo è un firmware ESP-IDF v6.1 (C, FreeRTOS) per un dispositivo con vincoli stretti: ESP32-S3 senza PSRAM, uno schermo 160x80, un solo pulsante. Le scelte seguenti derivano da questi vincoli.

## Strati

```
apps/         injector, webhid, netscan, usbdisk, launcher        <- una per attività
components/   appfw  ui  gfx  display  input  led  qr     <- interfaccia
              hid  usbdev  net  websrv  storage  config  luart  <- servizi
              devcon  syslog  board  lua                  <- console, log, pin, interprete
main/         avvio, tabella app, console di debug
```

Le app dipendono dai servizi, mai l'una dall'altra. Per questo ogni app può sparire senza toccare le altre.

## Framework ad app

Un'app è un descrittore statico (`app_desc_t`) con una funzione `run()` in un task dedicato. Il gestore (`appmgr`) avvia un'app alla volta e, quando `run()` ritorna, rilascia d'ufficio tutte le risorse condivise: profilo USB, SD prestata, livello LED, rete. Il motivo è che un'app dimentica di rilasciare qualcosa soprattutto quando esce per un errore, e un dongle che resta in un profilo USB sbagliato è difficile da diagnosticare senza schermo grande né tastiera.

L'ingresso è un solo pulsante, ridotto a gesti: click, doppio click, pressione lunga; 3 secondi di pressione chiedono l'uscita, dall'app o dallo script. L'uscita è un evento (`APP_EV_EXIT`) che ogni attesa consegna, anche quando la coda degli eventi è piena, e che i dialoghi comuni rispettano. Consegnarlo però non basta: un task FreeRTOS non si può terminare dall'esterno in modo sicuro, quindi un'app in un calcolo lungo o in una chiamata bloccante esce solo quando torna ad attendere. Per quel caso, se dopo 3 secondi l'app è ancora lì, la barra di stato mostra "Hold again: reboot" e un secondo hold riavvia il dongle. È l'unica uscita davvero garantita.

I testi più lunghi dello spazio disponibile (errori, descrizioni degli script, conferme) scorrono da soli una riga alla volta, perché con un solo pulsante non c'è un modo comodo di scorrere a mano.

## Profili USB

TinyUSB espone o tutto o niente, e riconfigurare la USB richiede di ri-enumerare il dispositivo. Per questo l'app dichiara il profilo che le serve e il sistema lo applica prima di avviarla. Un profilo è una combinazione di interfacce (tastiera, mouse, disco, seriale CDC), non un elenco chiuso: un'app nuova dichiara quella che le serve, e nomi, PID e indicatore della barra di stato ne derivano. Con profilo spento la PHY torna alla USB-Serial-JTAG (log e flash).

Non tutte le combinazioni entrano: l'OTG dell'ESP32-S3 ha pochi endpoint IN, e con tastiera + mouse + disco + CDC il Mac enumerava solo l'HID (provato sul dispositivo). Il limite misurato è di 4 endpoint IN oltre a EP0; una combinazione che lo supera viene rifiutata invece di essere enumerata a metà.

La microSD è un'unica risorsa e il disco è sempre lei, in uno di due modi: prestata all'host in lettura e scrittura (il firmware non la vede più), oppure condivisa in sola lettura (il firmware la tiene montata ma non ci scrive). Il modo è parte del profilo, e il passaggio della SD lo fa il sistema: si stacca l'host, la SD torna al firmware, si svuota il log per non perdere righe, poi la si presta di nuovo. Un'app che cambia profilo a metà (lo fa l'injector con `disk.attach`) passa dalla stessa funzione.

## Configurazione e script

`/config.lua` è uno script Lua che restituisce una tabella; all'avvio è eseguito in una VM sandbox e appiattito in coppie chiave/valore, poi la VM si chiude per liberare RAM. Così le app leggono con `config_str("keyboard.layout", "us")` e il resto della memoria non è occupato da un interprete.

Lo scripting dell'injector usa una VM diversa, per ogni esecuzione: librerie limitate (niente `io`, `os`), limite di memoria di 96 KB, interrompibile in qualunque istruzione. Il limite di memoria è deliberato: con ~250 KB di heap libero un script non deve poter togliere memoria al WiFi o alla USB.

## Memoria

Misure sul dispositivo (a memoria di sessione, non riprodotte in un test automatico): heap libero circa 316 KB a riposo, 252 KB dopo l'uscita da un'app che ha usato la rete, circa 193 KB con il server Web HID attivo; nessuna perdita tra cicli ripetuti di apertura e chiusura.

## Rete

Il gestore WiFi (`net`) è a lease: ogni app che ne ha bisogno lo richiede e lo rilascia; il WiFi resta acceso finché c'è almeno un lease. Ogni lease appartiene al task che l'ha preso: all'uscita di un'app il sistema rilascia solo quelli del suo task, così un servizio in un task proprio non perderebbe la connessione. L'attesa della connessione si interrompe quando l'utente chiede l'uscita, invece di durare fino alla scadenza.

Il server delle pagine web (`websrv`) e il QR code (`qr`) sono componenti a parte, non codice del Web HID: autenticazione con PIN, blocco per indirizzo dopo 5 errori, una sola sessione con subentro dallo stesso dispositivo, pagina compressa. Servono così anche alle app web future (file browser, injector web) senza ripetere la parte che riguarda la sicurezza. In modalità automatica cerca le reti, sceglie la migliore tra quelle note in `config.lua`, e se non ne trova crea un hotspot. Il risparmio energetico del WiFi è disattivato: attiva, la pagina web del Web HID impiegava secondi a rispondere.

## Test

La logica pura (tabelle dei layout, gesti del pulsante, parsing di NBNS e mDNS, ricerca OUI, CSV di Netscan) gira sul PC: `make -C test/host`. Il resto si verifica sul dispositivo con la console di debug e gli screenshot (`tools/grillo.py shot`).
