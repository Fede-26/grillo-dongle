# API Lua dell'injector

Riferimento delle funzioni disponibili negli script `.lua` eseguiti dall'injector. Per il formato dell'intestazione vedi [Intestazione degli script](intestazione-script.md).

## Ambiente

Lo script gira in una VM Lua 5.4 sandbox, un solo script alla volta.

- Librerie standard presenti: `base`, `coroutine`, `table`, `string`, `math`, `utf8`. Mancano `io`, `os`, `package`, `debug`; `dofile` e `loadfile` sono rimossi.
- `print(...)` scrive nel log (livello info), non sul display.
- Memoria massima della VM: 96 KB. Oltre il limite lo script termina con un errore di memoria.
- Si può interrompere tenendo premuto il pulsante 3 secondi: la prossima chiamata a una funzione del dongle (o 1000 istruzioni Lua) solleva l'errore `aborted`, e un `ui.menu` o `ui.confirm` aperto si chiude. Lo stesso vale per `@timeout`. Dopo l'interruzione il display mostra "Aborted" con il punto in cui lo script si è fermato; si resta nell'injector.
- È accettato solo codice sorgente testuale, mai bytecode precompilato: anche `load()` accetta solo testo, e `string.dump` non esiste.
- Le chiamate annidate che passano dal C (`pcall` dentro `pcall`, callback di `string.gsub` e `table.sort`, metamethod, `require`) hanno un limite di stack: oltre, lo script termina con `C stack overflow (too many nested calls)` invece di bloccare il dongle. Con `pcall` annidati il limite è circa 24 livelli (misurato il 2026-11-10). La ricorsione Lua pura non ha questo limite, solo quello della memoria.
- Prima dell'esecuzione il dongle attende che il PC abbia montato la tastiera, poi attende `start_delay`.

Un errore Lua non gestito termina lo script e viene mostrato sul display con nome file e riga; se il messaggio è lungo scorre da solo. Il messaggio va anche nel log sulla microSD (`/logs/grillo.log`).

## `kb`: tastiera

### `kb.type(testo [, variabili])`

Digita una stringa UTF-8 con il layout e l'OS correnti. In `testo`, `${nome}` è sostituito dal valore di `variabili.nome` (se la tabella manca, dalla variabile globale `nome`). Una variabile non definita è un errore: digitare `${user}` alla lettera non è mai ciò che si voleva. `\n` preme Invio, `\t` preme Tab.

Errori: `U+XXXX not typeable with layout NOME` se il carattere non esiste nel layout e non c'è un ripiego per l'OS; `USB host not responding` se il PC non risponde.

```lua
kb.type("Hello ${who}!\n", {who = "Grillo"})
```

### `kb.type_secret(nome)`

Digita la stringa `secrets.nome` di `config.lua` senza mai passarla per una stringa Lua: lo script non può leggerla come valore. Errore se la chiave non esiste, oppure `secret 'nome' has a character not typeable with layout NOME` (il messaggio non dice quale carattere, per non rivelarlo). Vedi [Digitare una password](../how-to/password.md).

### `kb.tap(tasto [, n])`

Preme e rilascia un tasto `n` volte (default 1). `tasto` è un nome della tabella sotto oppure un singolo carattere UTF-8 del layout corrente (`"r"`, `"€"`). Una lettera maiuscola indica il tasto, non lo Shift.

### `kb.press(tasto, ...)`

Preme insieme fino a 6 tasti più i modificatori, poi rilascia tutto. Per le combinazioni.

```lua
kb.press("CTRL", "ALT", "DELETE")
kb.press("GUI", "r")        -- Win+R su Windows, Cmd+R su macOS
```

Nomi di tasto riconosciuti (maiuscole indifferenti):

| Gruppo | Nomi |
| --- | --- |
| Modificatori sinistri | `CTRL`/`CONTROL`, `SHIFT`, `ALT`/`OPTION`, `GUI`/`WIN`/`CMD`/`COMMAND`/`SUPER` |
| Modificatori destri | `RCTRL`, `RSHIFT`, `RALT`/`ALTGR`, `RGUI` |
| Controllo | `ENTER`/`RETURN`, `ESC`/`ESCAPE`, `BACKSPACE`, `TAB`, `SPACE`, `CAPSLOCK`, `MENU`/`APP` |
| Navigazione | `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `INSERT`, `DELETE`/`DEL`, `UP`, `DOWN`, `LEFT`, `RIGHT` |
| Altri | `PRINTSCREEN`, `SCROLLLOCK`, `PAUSE`/`BREAK`, `NUMLOCK` |
| Funzione | `F1` … `F24` |

### `kb.layout([nome])` e `kb.os([nome])`

Con argomento impostano, senza leggono. Restituiscono il valore corrente. Layout: `us`, `it`, `colemak`. OS: `windows`, `linux`, `mac` (`win` e `macos` sono accettati in ingresso).

### Tempi: `kb.speed`, `kb.hold`, `kb.jitter`, `kb.line_delay`

Con argomento (millisecondi, 0..60000) impostano, senza leggono. Valgono per il resto dello script.

| Funzione | Significato | Default |
| --- | --- | --- |
| `kb.speed` | pausa tra un tasto e il successivo | 8 |
| `kb.hold` | durata della pressione di ogni tasto | 5 |
| `kb.jitter` | ± ms casuali sulla pausa | 0 |
| `kb.line_delay` | ms in più dopo ogni `\n` | 0 |

I default possono venire da `config.lua` (sezione `keyboard`) o dall'intestazione dello script.

## `mouse`

Non disponibile mentre il disco USB è collegato (`disk.attach`): errore `no mouse while the disk is attached`.

- `mouse.move(dx, dy)`: spostamento relativo.
- `mouse.click([tasto])`: `"left"` (default), `"right"`, `"middle"`.
- `mouse.scroll(n)`: rotella, `n` da -127 a 127.

## `ui`: display e pulsante

- `ui.status(testo)`: stato nella schermata dell'esecuzione, su due righe; un testo più lungo scorre da solo.
- `ui.progress(valore [, massimo])`: barra di avanzamento, massimo 100 se omesso.
- `ui.menu(titolo, voci)`: menu a lista (da 1 a 32 stringhe); click = giù, doppio click = su, pressione lunga = scegli. Restituisce l'indice scelto (da 1). Se l'utente esce (hold 3 s) lo script termina con `aborted`.
- `ui.confirm(testo [, predefinito_sì [, timeout_ms]])`: domanda sì/no, restituisce un booleano. Predefinito `true`. Con `timeout_ms` (fino a 3600000) il display mostra i secondi residui e, se non c'è alcun gesto entro la scadenza, la risposta è quella predefinita: serve con l'autorun, dove nessuno potrebbe premere nulla. Un click disattiva la scadenza.
- `ui.wait_button()`: attende un gesto e restituisce `"click"`, `"double"` o `"long"`.

## `sys`

- `sys.sleep(ms)`: pausa interrompibile.
- `sys.wait_host([ms])`: attende che il PC abbia montato la tastiera; `true`, oppure `false` allo scadere (default: 24 ore, in pratica finché non si esce con 3 secondi di pressione).
- `sys.log(testo)`: scrive nel log, anche sulla microSD (`/logs/grillo.log`, tag `script`). Con l'ora nota la riga sul file inizia con data e ora locali.
- `sys.serial()`: numero di serie USB del dongle (12 cifre esadecimali, il MAC); identifica la sua porta seriale sul PC.
- `sys.time()`: secondi UTC dal 1970, oppure `nil` se l'ora non è nota. Il dongle non ha batteria per l'orologio: l'ora la impostano NTP (quando un'app si collega a una rete) o `host.clock()`, e resta valida fino allo spegnimento.
- `sys.date([formato])`: ora locale (fuso `timezone` di `config.lua`) formattata come `strftime`; default `"%Y-%m-%d %H:%M:%S"`. `nil` se l'ora non è nota; `""` se il risultato supera 63 caratteri.
- `sys.set_time(secondi)`: imposta l'ora (secondi UTC dal 1970). Errore per un valore precedente al 2023. Di solito si usa `host.clock()`.

## `led`

- `led.rgb(r, g, b)`: colore fisso, componenti 0..255.
- `led.effect(nome [, colore [, periodo_ms]])`: `"off"`, `"solid"`, `"pulse"`, `"blink"`, `"rainbow"`. Colore come intero `0xRRGGBB` (default: il colore d'accento dell'app), periodo default 1000.
- `led.off()`.

## `fs`: lettura di file (sola lettura)

Solo sotto `/payloads` e `/data` della microSD; ogni altro percorso, o uno che contiene `..`, è un errore.

- `fs.read(percorso)`: contenuto del file come stringa, oppure `nil, "cannot open"`. File oltre 16 KB: errore.
- `fs.lines(percorso)`: iteratore sulle righe (senza `\r\n`). Una riga oltre 255 caratteri arriva divisa in più pezzi consecutivi, senza errore. Errore se il file non si apre.
- `fs.list(percorso)`: tabella dei nomi (file nascosti esclusi), oppure `nil, "cannot open"`.
- `fs.exists(percorso)`: booleano.

## `disk`: la microSD come disco sul PC

Mentre il disco è collegato, il dongle espone tastiera e disco insieme (profilo HID+MSC) e la microSD è in sola lettura per il PC. Il dongle si ri-enumera, quindi `disk.attach` attende che il PC rimonti la tastiera.

- `disk.attach([timeout_ms])`: default 15000. Errore se la SD non è montata.
- `disk.wait_eject([timeout_ms])`: default 120000. Restituisce `"eject"` se il PC ha espulso il disco, `"click"` se l'utente ha premuto il pulsante, `nil` allo scadere.
- `disk.detach()`: torna al solo profilo tastiera.

Se lo script termina con il disco ancora collegato, il dongle lo scollega comunque. I comandi per copiare ed espellere sul PC li digita lo script con `kb.type`: vedi [Copiare file sul PC](../how-to/copiare-file.md).

## `host`: risposta dal PC (primitive)

Il PC può rispondere al dongle con una riga `@G<nonce>:<codice>` scritta sulla porta seriale CDC. Il dongle accetta solo quella riga, con il nonce atteso: dal PC arrivano "finito" e un numero, mai l'output dei comandi. Ogni altra riga è scartata. Di solito si usa la libreria `host` qui sotto, non queste primitive.

- `host.expect(nonce)`: si aspetta una risposta con questo nonce (1..15 lettere o cifre). Va chiamata **prima** di digitare il comando, altrimenti una risposta rapida andrebbe persa.
- `host.wait([timeout_ms])`: attende la risposta (default 60000 ms). Restituisce il codice di uscita e il testo ricevuto (max 256 caratteri; il dongle sostituisce con `_` tutto ciò che non è lettera, cifra, `. _ : -` o spazio), oppure `nil` allo scadere. Errore `call host.expect() first` se manca `expect`.
- `host.append(id, data, etichetta, valore)`: aggiunge una riga `data,script,etichetta,valore` a `/logs/host/<id>.csv` sulla SD. `id` e `etichetta` sono ripuliti (solo `A-Z a-z 0-9 _ -`, 24 caratteri); massimo 64 file e 256 KB per file. Con il disco collegato (`disk.attach`) la riga aspetta in RAM (massimo 16) e viene scritta al `disk.detach`.

## Libreria `host`

In `sd-template/payloads/lib/host.lua`. Digita un comando nel terminale del PC e dice quando è finito e con quale codice di uscita.

```lua
local host = require("host")
host.shell("powershell")      -- o "sh"; una volta, con il terminale in primo piano
local code = host.run([[Start-Process notepad -Wait]])
if code ~= 0 then error("notepad: " .. tostring(code)) end
```

- `host.shell(tipo)`: `"powershell"` o `"sh"` (bash, zsh). Digita una volta il codice che trova la porta seriale del dongle dal suo numero di serie e verifica il canale; errore se il PC non risponde. `cmd.exe` non è supportato (vedi sotto).
- `host.run(comando [, {timeout = ms}])`: digita il comando seguito da una coda che invia il codice di uscita. Restituisce il codice, o `nil` allo scadere (default 60000 ms).
- `host.sync([opzioni])`: attende che la shell abbia eseguito tutto ciò che è stato digitato prima.
- `host.get(espressione [, {timeout}])`: valuta l'espressione nella shell del PC e restituisce il risultato come stringa (max 256 caratteri ripuliti), o `nil`. Più righe sono unite con spazi.
- `host.machine([espressione])`: imposta l'id della macchina per i log e la data del PC. Senza argomento usa il MAC della prima scheda di rete fisica (in ordine di indirizzo): è dell'hardware, quindi diverso anche tra PC clonati con Clonezilla, mentre `MachineGuid` e nome host si copiano con il disco. Restituisce l'id.
- `host.clock([{timeout}])`: legge l'ora UTC dal PC (`[DateTimeOffset]::UtcNow` in PowerShell, `date +%s` in sh), imposta l'orologio del dongle e restituisce `sys.date()`. Da lì le righe di `/logs/grillo.log` e `sys.date()` hanno la data. Errore se il PC non risponde.
- `host.log(etichetta, espressione)`: come `host.get`, poi aggiunge la riga a `/logs/host/<id>.csv` e restituisce il valore. Senza `host.machine` il file è `unknown.csv`.

```lua
host.shell("powershell")
host.machine()
host.log("seriale", [[(Get-CimInstance Win32_BIOS).SerialNumber]])
host.log("uuid", [[(Get-CimInstance Win32_ComputerSystemProduct).UUID]])
```

Non ridigitare mai un valore letto dal PC in un comando di shell senza controllarlo: può contenere ciò che ha scelto chi ha configurato la macchina.

Il comando deve essere bloccante: con `Start-Process` senza `-Wait`, o `open` su macOS, il codice arriva appena il programma parte. Non scrivere `${` nel comando. La porta si trova così: su Windows dal dispositivo padre della COM (che contiene il numero di serie), su Linux in `/dev/serial/by-id/*<serie>*`, su macOS in `/dev/cu.usbmodem<serie><n>`. Funziona anche con più dongle collegati. Su Windows e Linux non è stato verificato su un PC reale; il canale lato dongle sì (risposta con nonce giusto accettata, con nonce sbagliato scartata).

## Libreria `pcdisk`

Non è nel firmware ma in `sd-template/payloads/lib/pcdisk.lua`: `require("pcdisk")` dopo averla copiata sulla SD.

- `pcdisk.run(corpo [, {timeout = ms}])`: collega la SD al PC, digita nel terminale aperto sul PC una riga che trova il disco, esegue `corpo` ed espelle; poi attende l'espulsione (default 180000 ms) e scollega. Restituisce `"eject"` o `"click"`, errore se scade il tempo.
- `corpo` è una riga di PowerShell (`kb.os()` = `windows`) o di shell POSIX (`linux`, `mac`); `$d` è la radice della SD senza barra finale. Non usare `${` nel corpo.
- Il disco si riconosce dal file `.grillo` nella radice. Il supporto Linux (percorsi di automount di udisks) non è verificato.
- `opzioni.mount`: `"auto"` (default) usa il montaggio automatico del sistema; `"sudo"` (solo Linux) monta la SD a mano in `/mnt/grillo`, in sola lettura, e la smonta ed espelle alla fine. Il default si può impostare in `config.lua` con `pcdisk = { mount = "sudo" }`.
- Con `mount = "sudo"` la password è presa da `secrets.sudo` di `config.lua` (altro nome con `opzioni.secret`) e digitata con `kb.type_secret`. Prima il dongle esegue `sudo -k; sudo -v`, così il prompt compare sempre. Il volume si riconosce dall'etichetta `GRILLO-SD`, non da `.grillo`, che prima del mount non si può leggere. Se la password è sbagliata, i comandi successivi finiscono nel prompt di sudo: controlla `secrets.sudo`. Non verificato su una distribuzione reale.

## `cfg(chiave [, predefinito])`

Legge un valore di `config.lua` come stringa (es. `cfg("keyboard.layout")`). Restituisce `predefinito` se la chiave manca. Le chiavi `secrets.*` non sono leggibili da qui.

## `require(nome)`

Carica `/payloads/lib/nome.lua` una volta sola (le chiamate successive restituiscono lo stesso valore). `nome` può contenere solo lettere, cifre, `_` e `-`. Due moduli che si richiedono a vicenda danno l'errore `require loop on module 'nome'`.

```lua
local common = require("common")
common.pause("Premi quando sei pronto")
```
