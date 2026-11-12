# Sicurezza e rischi accettati

Grillo è uno strumento personale per installare e configurare PC. Un dispositivo che digita sul computer a cui è collegato è, per costruzione, potente: questa pagina dice dove sono i confini e quali rischi sono stati accettati di proposito.

## Cosa può fare il dongle

Una volta collegato, il PC lo tratta come una tastiera e un mouse fidati. Può eseguire qualunque azione che un utente può fare con la tastiera, e non esiste alcuna conferma sul PC. Usalo solo su computer che hai il diritto di configurare.

## Rischi accettati

- **Password in chiaro sulla microSD.** `secrets` in `config.lua` non è cifrato. `kb.type_secret` evita solo che uno script le legga come stringa. Chi ha la scheda le legge. Usalo per credenziali di installazione che accetti di esporre così.
- **Web HID senza PIN.** `webhid.pin = false` lascia che chiunque sulla stessa rete (o sull'hotspot) comandi il PC. Il PIN è comunque di 6 cifre, mostrato sul display, con blocco di 30 secondi dopo 5 errori dallo stesso indirizzo IP (il blocco è per indirizzo: un altro host non può tenere fuori il proprietario sbagliando apposta). Il canale WebSocket non è cifrato (niente HTTPS): chi osserva la rete locale vede i comandi e il PIN. Per questo l'accesso è pensato per reti fidate.
- **Hotspot con password di default.** Se non imposti `hotspot.pass` vale `grillo-dongle`, nota a chiunque legga questa documentazione.
- **Web Injector senza PIN.** `webinjector.pin = false` lascia che chiunque sulla stessa rete avvii gli script della microSD sul PC collegato. Con il PIN valgono le stesse regole del Web HID (blocco per indirizzo, un solo client, niente HTTPS). La pagina sceglie solo script già sulla SD: percorsi assoluti e `..` sono rifiutati, e non si possono caricare né modificare script dalla rete.
- **Web Files vede tutta la microSD.** Con il PIN chi è sulla pagina legge, modifica ed elimina qualunque file, log e `config.lua` compresi; senza PIN (`webfiles.pin = false`) lo può fare chiunque sulla stessa rete. Le password di `config.lua` (`pass`, `password` e la tabella `secrets`) arrivano alla pagina nascoste da segnaposto, e solo il pulsante **Show secrets** le manda in chiaro: il canale non è cifrato, quindi chi osserva la rete le vedrebbe. Download, upload e salvataggi passano da rotte HTTP che richiedono il token dato dopo il PIN: l'indirizzo IP del client non basterebbe, perché dietro un NAT più dispositivi lo condividono. I percorsi con `\`, `:` o un nome che finisce con `.` o spazio (`..` compreso) sono rifiutati. Un `config.lua` salvato con un errore lascia il dongle con i valori predefiniti (niente reti note, hotspot con la password di default) fino alla correzione; la pagina controlla la sintassi prima di salvare.
- **Reti sicure senza PIN.** Una rete con `trusted = true` in `config.lua` (o l'hotspot con `hotspot.trusted = true`) fa saltare il PIN a tutte le app web: chiunque sia su quella rete ha gli stessi poteri di chi ha il PIN. La scelta si basa sul nome della rete: un access point che si presenta con lo stesso SSID e la stessa password è indistinguibile. Il dongle si collega solo a reti WPA2/WPA3-Personal, quindi serve comunque la password.
- **Controllo dell'origine sul WebSocket.** I WebSocket non seguono la same-origin policy del browser: senza un controllo, una pagina qualunque aperta in un browser sulla stessa rete potrebbe collegarsi a `ws://<IP del dongle>/ws` e, senza PIN, agire. `websrv` rifiuta l'handshake se l'header `Origin` non è `http://<IP del dongle>`. Il confronto è con l'indirizzo locale del socket e non con l'header `Host`, che con un attacco di DNS rebinding è quello del sito ostile. Un client che non manda `Origin` (uno script, non un browser) è accettato: chi lo usa è già sulla rete e non ha bisogno di un sito ponte.
- **Un solo client** per volta sul Web HID: chi si collega per primo lo tiene. Un altro indirizzo riceve `busy` prima ancora del controllo del PIN, quindi durante una sessione non si possono provare PIN. Lo stesso indirizzo con il PIN giusto subentra alla sessione (serve al telefono che esce dallo standby): chi condivide l'IP del client, per esempio dietro lo stesso NAT, e conosce il PIN può prenderne il posto.
- **Console di debug attiva nelle build di sviluppo.** Con `CONFIG_GRILLO_USB_DEBUG_CDC=y` (il default) la porta seriale CDC accetta i comandi della [console](../reference/console-debug.md), compresi `cat` e `ls`. Chi controlla il PC a cui il dongle è collegato può quindi leggere `config.lua`, password di `secrets` comprese. Le release pubblicate su GitHub (`.github/workflows/release.yml`) compilano con `sdkconfig.release`, che disattiva l'opzione: chi flasha una release non ha la console.

## Cosa limita uno script Lua

La VM dell'injector non ha accesso diretto al filesystem (`io`, `os`, `dofile`, `loadfile` assenti), legge solo `/payloads` e `/data` con `fs.*`, ha un limite di memoria di 96 KB, rifiuta il bytecode precompilato e si interrompe con la pressione di 3 secondi. Non limita ciò che lo script digita sul PC.

## Netscan

Netscan emette traffico sulla rete a cui è collegato: richieste ARP a ogni indirizzo della /24, query NBNS (UDP 137) e mDNS (UDP 5353) agli host trovati, un ping al gateway, una query DNS e una `GET /generate_204` a `connectivitycheck.gstatic.com` (cambiabile con `netscan.probe_host`). Su reti di altri usalo con il loro consenso: lo sweep può comparire nei log di un firewall o di un sistema di rilevamento. Non fa scansioni di porte.

SSID e nomi host li sceglie chi è sulla rete: sul display vengono ripuliti (solo ASCII stampabile) e nei CSV in `/scans/` i valori non numerici che iniziano con `=`, `+`, `-` o `@` hanno un apice davanti, così non diventano formule aprendoli con un foglio di calcolo. I CSV contengono i MAC e i nomi degli host della rete scansionata: trattali come dati del cliente.

## Il canale di ritorno

Il canale `host` permette al PC di rispondere al dongle sulla seriale CDC. È limitato di proposito: il dongle accetta una sola forma di riga (`@G<nonce>:<codice>`), con il nonce che lo script ha atteso, e scarta tutto il resto; lo script riceve solo un codice di uscita numerico. Lo script può ricevere un piccolo valore di inventario (`host.get`, `host.log`) o una riga scritta dall'utente sulla tastiera del PC (`host.input`): al massimo 256 byte, con tutto ciò che non è lettera (accentate comprese, in UTF-8), cifra, `. _ : -` o spazio sostituito da `_`, così il valore non porta con sé virgolette, `;` o `$` se viene ridigitato in una shell o scritto in un CSV. Il valore resta nella VM dello script, che non ha rete, e nei log in `/logs/host/`: il firmware compone il nome del file da un id ripulito, con tetto di 64 file e 256 KB ciascuno. Non esiste un modo per scaricare file o l'output completo di un comando. Il comando che scrive la risposta è digitato dallo script, quindi dipende da ciò che lo script fa già sul PC: il canale non aggiunge permessi. Prima del canale `host.shell` (e `pcdisk.run`) avvia `powershell -NoProfile` o `exec sh`: senza profilo né file rc, alias e funzioni definiti dall'utente non cambiano i comandi digitati dal dongle.

## Segnalare un problema

Il repository è privato. Segnala problemi direttamente a chi lo gestisce.
