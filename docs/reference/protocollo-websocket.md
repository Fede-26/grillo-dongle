# Protocollo WebSocket delle app web

Il Web HID, il Web Injector e Web Files servono una pagina e un canale WebSocket che trasporta i comandi. Il server è il componente `websrv` (sopra `esp_http_server`); un solo client alla volta. Le rotte `/cfg` e `/ws`, l'accesso con `p<PIN>` e le risposte `ok`, `no`, `locked` e `busy` sono del componente e valgono per ogni app web; i comandi sono di ciascuna app (Web HID qui sotto, poi [Web Injector](#web-injector) e [Web Files](#web-files)).

## Endpoint

| Percorso | Metodo | Risposta |
| --- | --- | --- |
| `/` | GET | La pagina (HTML compresso gzip, `Cache-Control: no-store`). |
| `/cfg` | GET | `pin` se serve il PIN, `nopin` altrimenti (chiave `<app>.pin = false` o rete marcata `trusted`). La pagina la usa per decidere se mostrare il campo PIN. |
| `/ws` | GET (upgrade WebSocket) | Canale dei comandi. L'handshake è rifiutato (connessione chiusa senza risposta) se l'header `Origin` c'è e non è `http://<IP del dongle>` (con o senza `:80`). Senza `Origin`, come da un client non browser, è accettato. |

## Messaggi

Ogni messaggio è un frame di testo: un carattere di tipo seguito dagli argomenti. I comandi di un client non autenticato sono ignorati in silenzio; l'accesso (`p`) è accettato in qualunque momento.

### Client → dongle

| Messaggio | Significato |
| --- | --- |
| `p<PIN>` | Accesso. Con PIN disattivato l'argomento è ignorato (`p`). |
| `m dx dy` | Sposta il mouse (relativo). Ogni valore è limitato a ±2000. |
| `b maschera` | Stato dei pulsanti del mouse (bit 0 sinistro, 1 destro, 2 centrale). |
| `w dy [dx]` | Rotella. Ogni valore è limitato a ±127. |
| `t testo` | Digita il testo con il layout e l'OS correnti. Un carattere che il layout non ha diventa `?` (vedi `untypeable`). |
| `c mods carattere` | Combinazione: maschera dei modificatori HID più il tasto che produce il carattere (es. Ctrl+C). |
| `k mods usage` | Combinazione con usage HID grezzo: tasti speciali, F-key. `usage` 0 = solo modificatori. |
| `e n` | `n` Backspace di seguito (1..500; fuori da questo intervallo il comando è ignorato). Lo usa il live typing, che spezza le cancellazioni più lunghe. |
| `l nome` | Layout del PC: `it`, `us`, `colemak`. |
| `o nome` | OS del PC: `windows`, `linux`, `mac`. |

I frame sono limitati in dimensione; un frame vuoto o troppo lungo chiude il canale.

### Dongle → client

| Messaggio | Significato |
| --- | --- |
| `ok` | Accesso riuscito. Se un'altra sessione dello stesso indirizzo IP era ancora aperta, il dongle la chiude e la sostituisce. |
| `T<token>` | Subito dopo `ok`: token della sessione per le rotte HTTP delle app che ne hanno (header `X-Grillo-Token` o parametro `t`). Cambia a ogni accesso; le pagine che non lo usano lo ignorano. |
| `no` | PIN errato. |
| `locked` | Troppi tentativi da questo indirizzo: bloccato per 30 secondi dopo 5 PIN errati. |
| `busy` | Un client con un altro indirizzo è già collegato. Arriva prima del controllo del PIN: durante una sessione non si possono provare PIN. |
| `L<layout> <os>` | Layout e OS correnti, inviato dopo l'accesso per allineare i selettori (es. `Lcolemak mac`). |
| `nohost` | Il PC non risponde alla tastiera USB. |
| `hostok` | Il PC risponde di nuovo, dopo un `nohost`. |
| `untypeable n` | `n` caratteri dell'ultimo `t` non erano nel layout e sono stati digitati come `?`. |

## Note

- Il PIN è di 6 cifre, generato a ogni avvio dell'app e mostrato sul display; non c'è sulle reti marcate `trusted` in `config.lua`. L'indirizzo IP del client è mostrato sul display (abbreviato).
- Il canale non è cifrato: la protezione sta nel PIN e nella rete scelta. Vedi [Sicurezza](../explanation/sicurezza.md).

## Web Injector

Stesse rotte, stesso accesso e stesse risposte `ok`, `no`, `locked`, `busy`. Dal client arrivano comandi di testo (un carattere più gli argomenti); il dongle risponde con oggetti JSON, un messaggio per oggetto, con il tipo nel campo `t`. I percorsi sono relativi a `/payloads`: percorsi assoluti e `..` ricevono `{"t":"error","text":"Bad path"}`.

### Client → dongle

| Messaggio | Significato |
| --- | --- |
| `l<cartella>` | Elenco di una cartella (`l` da solo = `/payloads`): un `entry` per voce, poi `end`. |
| `s` | Tutti gli script (fino a 3 livelli), per le viste per OS e per tag: `entry` con `path`, poi `end`. |
| `m<script>` | Scheda dello script: `meta`. |
| `r<script>` | Esegue lo script. Con un altro script in corso: `error`. |
| `a<id>:<n>` | Risposta al dialogo `id`: indice della voce (`menu`), 0/1 per no/sì (`confirm`), 0/1/2 per click/doppio/lungo (`button`), 0 per l'esito (`done`). Un `id` che non è quello del dialogo aperto è ignorato: il dialogo è già stato chiuso dal pulsante. |
| `x` | Interrompe lo script in corso (come 3 secondi di pressione). |

### Dongle → client

| `t` | Campi | Significato |
| --- | --- | --- |
| `hello` | `phase`: `idle`, `run`, `done`; `dropped` (voci della cronologia scartate) | Dopo ogni accesso. Seguono la cronologia della run in corso o dell'ultima (`log`), l'ultimo `state` con uno script in corso, il dialogo aperto e l'esito (`result`). |
| `entry` | `name`, `title`, `dir`; per gli script `layout`, `os`, `tags` (effettivi); con `s` anche `path` | Una voce dell'elenco. |
| `end` | `scan`, `full` (elenco troncato a 48 voci) | Fine dell'elenco. |
| `meta` | `path`, `name`, `desc`, `layout`, `os`, `tags`, `timeout` (-1 se assente), `error` | Intestazione dello script. |
| `state` | `name`, `status`, `p`, `max` (0 = niente barra), `wait` (attesa dell'host USB) | Stato dello script cambiato. |
| `log` | `k`, `at`, `text` | Voce della cronologia. `k`: `start` (nome dello script), `log` (riga di `sys.log`), `status` (stato nuovo), `ask` (dialogo aperto), `answer` (risposta, con "(from the page)" se è arrivata dalla pagina), `done` (esito). `at`: ora del dongle (`HH:MM:SS`) o, senza ora valida, tempo dall'avvio (`+m:ss`). Testo tagliato a 200 byte; il dongle tiene le ultime 100 voci. |
| `menu` | `id`, `title`, `items` | `ui.menu` aperto. |
| `confirm` | `id`, `title`, `text`, `yes` (risposta predefinita) | `ui.confirm` aperto. |
| `button` | `id` | `ui.wait_button` in attesa. |
| `done` | `id`, `name`, `path`, `ok`, `aborted`, `timeout`, `error` | Esito: resta aperto sul display finché qualcuno risponde. `path` è lo script, per eseguirlo di nuovo. |
| `result` | come `done`, senza `id` | Esito dell'ultima run, rimandato a ogni accesso finché non ne parte un'altra. |
| `close` | `id` | Dialogo chiuso, da qualunque parte sia arrivata la risposta. |
| `idle` | | Esito chiuso: si può avviare un altro script. |
| `error` | `text` | Comando rifiutato. |

I testi non UTF-8 validi (nomi di file, testi degli script) arrivano con `?` al posto dei byte non validi: il browser chiuderebbe il canale.

## Web Files

Stesse rotte, stesso accesso e stesse risposte. I percorsi sono relativi alla radice della microSD e iniziano con `/` (`/`, `/payloads/a.lua`); percorsi senza `/` iniziale, con `\` o `:`, o con un nome che finisce con `.` o spazio (`..` compreso: FatFs toglie punti e spazi finali) ricevono `{"t":"error","text":"Bad path"}`. Il cestino (`/.trash`, con qualunque combinazione di maiuscole: la FAT non le distingue) si gestisce solo con i comandi del cestino.

### Client → dongle

| Messaggio | Significato |
| --- | --- |
| `l<cartella>` | Elenco: un `entry` per voce (al massimo 1000), poi `end`. |
| `f` | Spazio della scheda: `space`. |
| `k<cartella>` | Nuova cartella. |
| `n<file>` | Nuovo file vuoto. |
| `v<da>\t<a>` | Rinomina o sposta (anche cartelle). La destinazione non deve esistere. |
| `c<da>\t<a>` | Copia un file. |
| `d<percorso>` | Sposta nel cestino. |
| `D<percorso>` | Elimina definitivamente, cartelle comprese con il contenuto. |
| `L` | Elenco del cestino: un `trash` per elemento, poi `end`. |
| `R<id>` | Ripristina l'elemento `id` del cestino. |
| `E<id>` | Elimina definitivamente l'elemento `id` del cestino. |
| `X` | Svuota il cestino. |

Ogni comando ha una sola risposta finale (`end`, `space`, `ok` o `error`) e il dongle li esegue nell'ordine di arrivo: la pagina abbina le risposte ai comandi in coda.

### Dongle → client

| `t` | Campi | Significato |
| --- | --- | --- |
| `entry` | `name`, `dir`, `size` (byte), `mtime` (secondi dal 1970, UTC) | Una voce della cartella. |
| `trash` | `id`, `origin` (percorso d'origine), `date` (vuota se l'ora non era nota), `dir`, `size` | Un elemento del cestino. |
| `end` | `full` (elenco troncato) | Fine dell'elenco. |
| `space` | `total`, `free` (byte) | Spazio della scheda. |
| `ok` | | Comando eseguito. |
| `error` | `text` | Comando rifiutato o fallito. |

### Rotte HTTP

Richiedono il token del messaggio `T<token>`, nell'header `X-Grillo-Token` o nel parametro `t`; senza, `403`. Il percorso è nel parametro `path`, codificato come una query string.

| Rotta | Metodo | Significato |
| --- | --- | --- |
| `/api/file?path=&reveal=` | GET | Testo di un file per l'editor, fino a 64 KB (`413` oltre, `404` se manca o è una cartella). `/config.lua` arriva con i segreti sostituiti da `"<secret:N>"`, in chiaro solo con `reveal=1`. |
| `/api/file?path=&overwrite=` | PUT | Il corpo è il contenuto del file (salvataggio e upload, di qualunque dimensione). Se il file esiste serve `overwrite=1`, altrimenti `409`. Per `/config.lua` i segnaposto `"<secret:N>"` tornano ai valori del file attuale; un N che il file non ha riceve `400` e il file non cambia. Risposta `ok`. |
| `/api/download?path=&t=` | GET | Il file come allegato (`Content-Disposition` con il nome in UTF-8, RFC 5987), con `Content-Length`: il browser mostra totale e tempo rimanente. Se la lettura si interrompe la connessione è chiusa prima della fine, e il browser segna il download come fallito. |
| `/api/check?path=` | POST | Il corpo è il testo di uno script (fino a 64 KB), anche non salvato: lo compila senza eseguirlo. Risposta `{"t":"check","ok":...,"error":...,"line":...}`; per gli script in `/payloads` (esclusa `lib`) anche `header` con l'errore dell'intestazione, vuoto se è valida. |

