# Architettura

Grillo è un firmware ESP-IDF v6.1 (C, FreeRTOS) per un dispositivo con vincoli stretti: ESP32-S3 senza PSRAM, uno schermo 160x80, un solo pulsante. Le scelte seguenti derivano da questi vincoli.

## Strati

```
apps/         injector, webinjector, webhid, webfiles, netscan, usbdisk, launcher  <- una per attività
components/   injector_core                               <- esecuzione degli script
              appfw  ui  gfx  display  input  led  qr  webui  webtheme  <- interfaccia
              hid  usbdev  net  websrv  storage  config  luart  <- servizi
              devcon  syslog  clock  board  lua           <- console, log, ora, pin, interprete
main/         avvio, tabella app, console di debug
```

Le app dipendono dai servizi, mai l'una dall'altra. Per questo ogni app può sparire senza toccare le altre.

## Framework ad app

Un'app è un descrittore statico (`app_desc_t`) con una funzione `run()` in un task dedicato. Il gestore (`appmgr`) avvia un'app alla volta e, quando `run()` ritorna, rilascia d'ufficio tutte le risorse condivise: profilo USB, SD prestata, livello LED, rete. Il motivo è che un'app dimentica di rilasciare qualcosa soprattutto quando esce per un errore, e un dongle che resta in un profilo USB sbagliato è difficile da diagnosticare senza schermo grande né tastiera.

L'ingresso è un solo pulsante, ridotto a gesti: click, doppio click, pressione lunga; 3 secondi di pressione chiedono l'uscita, dall'app o dallo script. La pressione lunga è un rilascio fra 0,6 e 1,5 secondi: da 0,6 secondi il LED lampeggia e la linea sotto la barra di stato diventa bianca, al rilascio la barra di stato lampeggia. Da 1,5 secondi la linea diventa la barra rossa dell'uscita, e rilasciare prima dei 3 secondi annulla senza fare nulla. L'uscita è un evento (`APP_EV_EXIT`) che ogni attesa consegna, anche quando la coda degli eventi è piena, e che i dialoghi comuni rispettano. Consegnarlo però non basta: un task FreeRTOS non si può terminare dall'esterno in modo sicuro, quindi un'app in un calcolo lungo o in una chiamata bloccante esce solo quando torna ad attendere. Per quel caso, se dopo 3 secondi l'app è ancora lì, la barra di stato mostra "Hold again: reboot" e un secondo hold riavvia il dongle. È l'unica uscita davvero garantita.

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

Il server delle pagine web (`websrv`), il QR code (`qr`), la schermata di connessione (`webui`) e il tema delle pagine (`webtheme`) sono componenti a parte, non codice del Web HID: autenticazione con PIN, blocco per indirizzo dopo 5 errori, una sola sessione con subentro dallo stesso dispositivo, pagina compressa. Li usano tutte le app web (Web HID, Web Injector, Web Files) senza ripetere la parte che riguarda la sicurezza; `webjson` costruisce i messaggi JSON del Web Injector e di Web Files. In modalità automatica cerca le reti, sceglie la migliore tra quelle note in `config.lua`, e se non ne trova crea un hotspot. Il risparmio energetico del WiFi è disattivato: attiva, la pagina web del Web HID impiegava secondi a rispondere.

## Pagine web

Le pagine stanno nella flash, compresse, insieme al tema comune: con l'hotspot il telefono non ha internet, quindi niente può venire da fuori, e la pagina funziona anche senza microSD (il Web HID non la richiede). Il tema ha tre modalità, scelte dalla pagina e ricordate dal browser: plain (sobria), arcade (pixel, righe di scansione, piccole animazioni) e retro (monitor a fosfori ambra). Font: Silkscreen per titoli ed elementi grafici, Iosevka senza ligature per i log, quello di sistema per il testo; i due incorporati sono ridotti ai caratteri usati da `tools/subset_fonts.py` (2,9 e 9,7 KB) e hanno licenza SIL OFL 1.1 (testi in `components/webtheme/web/fonts/`). Il tema cambia solo l'aspetto: le regole di disposizione della pagina, per telefono con tastiera a comparsa e per desktop, restano quelle della pagina.

## Injector e Web Injector

Le due app eseguono gli stessi script con lo stesso codice: `injector_core` contiene l'elenco e le intestazioni dei payload, l'engine Lua, la schermata di esecuzione e l'esito. Cambia solo come si sceglie lo script, dal display o da una pagina web. Il nucleo chiama agganci facoltativi del front-end (stato, log, apertura e chiusura dei dialoghi, esito): l'Injector non li usa, il Web Injector li inoltra al telefono. Le risposte del telefono arrivano all'app come evento `APP_EV_ANSWER`, che i dialoghi comuni trattano come una scelta col pulsante: così display e pagina restano attivi insieme senza dialoghi duplicati, e vale la prima risposta. Lo script gira nel task dell'app e i messaggi alla pagina partono con `websrv_send`, che li consegna dal task del server solo se il client autenticato è ancora lo stesso. Il Web Injector tiene anche la cronologia della run (le ultime 100 voci, già in JSON) e l'esito, e li rimanda a ogni accesso sotto lo stesso lock degli agganci: una voce nuova arriva solo dopo il resto, quindi la pagina non vede doppioni né buchi.

## Web Files

Tutto il lavoro di Web Files gira nel task del server: il task dell'app disegna solo la schermata. Le operazioni brevi (elenco, cartelle, rinomina, cestino) passano dal WebSocket; il contenuto dei file da rotte HTTP con il token della sessione, perché un file intero non entra comodamente in un messaggio WebSocket e il download deve poter partire da un link. Un file caricato o salvato va prima in `<nome>.part` e sostituisce il vecchio solo alla fine; ogni scrittura tiene il lock della scheda (`storage_begin_write`), così un prestito all'host via console aspetta la fine. Il cestino è una cartella numerata per elemento in `/.trash`, con il percorso d'origine accanto: eliminare è un rename, istantaneo anche per una cartella intera. Le password di `config.lua` sono nascoste con un'analisi del testo come Lua (commenti, stringhe con escape, stringhe lunghe), non con ricerche testuali, e tornate al loro posto al salvataggio (`apps/webfiles/secrets.c`, con test sul PC). Il controllo degli script compila il testo in una VM senza eseguirlo; il parser Lua è ricorsivo, quindi il task del server ha 16 KB di stack come quello dell'Injector. Web Files non usa la USB: la scheda resta al firmware.

## Ora

Il dongle non ha una batteria per l'orologio: all'accensione l'ora è ignota. La imposta SNTP ogni volta che una qualunque app si collega a una rete come client (non con l'hotspot, che non ha internet), oppure uno script con `host.clock()`, che la legge dal PC con il canale di ritorno. Dal PC si prende l'ora UTC e la si mostra con il fuso di `config.lua` (`timezone`, default Europe/Rome con l'ora legale): così un PC con il fuso sbagliato non sposta le date. L'ora resta valida ai riavvii software e si perde togliendo l'alimentazione. Finché è ignota, le righe di `/logs/grillo.log` hanno solo i millisecondi dall'avvio, `sys.time()` e `sys.date()` restituiscono `nil` e i CSV di Netscan hanno un numero progressivo al posto della data.

## Test

La logica pura (tabelle dei layout, gesti del pulsante, parsing di NBNS e mDNS, ricerca OUI, CSV di Netscan, segreti di `config.lua` per Web Files) gira sul PC: `make -C test/host`. Il resto si verifica sul dispositivo con la console di debug e gli screenshot (`tools/grillo.py shot`).
