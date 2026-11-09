# API Lua dell'injector

Riferimento delle funzioni disponibili negli script `.lua` eseguiti dall'injector. Per il formato dell'intestazione vedi [Intestazione degli script](intestazione-script.md).

## Ambiente

Lo script gira in una VM Lua 5.4 sandbox, un solo script alla volta.

- Librerie standard presenti: `base`, `coroutine`, `table`, `string`, `math`, `utf8`. Mancano `io`, `os`, `package`, `debug`; `dofile` e `loadfile` sono rimossi.
- `print(...)` scrive nel log (livello info), non sul display.
- Memoria massima della VM: 96 KB. Oltre il limite lo script termina con un errore di memoria.
- Si può interrompere tenendo premuto il pulsante 3 secondi: la prossima chiamata a una funzione del dongle (o 1000 istruzioni Lua) solleva l'errore `aborted`.
- È accettato solo codice sorgente testuale, mai bytecode precompilato.
- Prima dell'esecuzione il dongle attende che il PC abbia montato la tastiera, poi attende `start_delay`.

Un errore Lua non gestito termina lo script e viene mostrato sul display con nome file e riga.

## `kb`: tastiera

### `kb.type(testo [, variabili])`

Digita una stringa UTF-8 con il layout e l'OS correnti. In `testo`, `${nome}` è sostituito dal valore di `variabili.nome` (se la tabella manca, dalla variabile globale `nome`). Una variabile non definita è un errore: digitare `${user}` alla lettera non è mai ciò che si voleva. `\n` preme Invio, `\t` preme Tab.

Errori: `U+XXXX not typeable with layout NOME` se il carattere non esiste nel layout e non c'è un ripiego per l'OS; `USB host not responding` se il PC non risponde.

```lua
kb.type("Hello ${who}!\n", {who = "Grillo"})
```

### `kb.type_secret(nome)`

Digita la stringa `secrets.nome` di `config.lua` senza mai passarla per una stringa Lua: lo script non può stamparla né salvarla. Errore se la chiave non esiste. Vedi [Digitare una password](../how-to/password.md).

### `kb.tap(tasto [, n])`

Preme e rilascia un tasto `n` volte (default 1). `tasto` è un nome della tabella sotto oppure un singolo carattere del layout corrente (`"r"`). Una lettera maiuscola indica il tasto, non lo Shift.

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

- `ui.status(testo)`: riga di stato nella schermata dell'esecuzione.
- `ui.progress(valore [, massimo])`: barra di avanzamento, massimo 100 se omesso.
- `ui.menu(titolo, voci)`: menu a lista (da 1 a 32 stringhe); click = giù, doppio click = su, pressione lunga = scegli. Restituisce l'indice scelto (da 1). Se l'utente esce (hold 3 s) lo script termina con `aborted`.
- `ui.confirm(testo [, predefinito_sì [, timeout_ms]])`: domanda sì/no, restituisce un booleano. Predefinito `true`. Con `timeout_ms` (fino a 3600000) il display mostra i secondi residui e, se non c'è alcun gesto entro la scadenza, la risposta è quella predefinita: serve con l'autorun, dove nessuno potrebbe premere nulla. Un click disattiva la scadenza.
- `ui.wait_button()`: attende un gesto e restituisce `"click"`, `"double"` o `"long"`.

## `sys`

- `sys.sleep(ms)`: pausa interrompibile.
- `sys.wait_host([ms])`: attende che il PC abbia montato la tastiera; `true`, oppure `false` allo scadere (default: all'infinito).
- `sys.log(testo)`: scrive nel log, anche sulla microSD.

## `led`

- `led.rgb(r, g, b)`: colore fisso, componenti 0..255.
- `led.effect(nome [, colore [, periodo_ms]])`: `"off"`, `"solid"`, `"pulse"`, `"blink"`, `"rainbow"`. Colore come intero `0xRRGGBB` (default: il colore d'accento dell'app), periodo default 1000.
- `led.off()`.

## `fs`: lettura di file (sola lettura)

Solo sotto `/payloads` e `/data` della microSD; ogni altro percorso, o uno che contiene `..`, è un errore.

- `fs.read(percorso)`: contenuto del file come stringa, oppure `nil, "cannot open"`. File oltre 16 KB: errore.
- `fs.lines(percorso)`: iteratore sulle righe (senza `\r\n`, righe fino a 255 caratteri). Errore se il file non si apre.
- `fs.list(percorso)`: tabella dei nomi (file nascosti esclusi), oppure `nil, "cannot open"`.
- `fs.exists(percorso)`: booleano.

## `disk`: la microSD come disco sul PC

Mentre il disco è collegato, il dongle espone tastiera e disco insieme (profilo HID+MSC) e la microSD è in sola lettura per il PC. Il dongle si ri-enumera, quindi `disk.attach` attende che il PC rimonti la tastiera.

- `disk.attach([timeout_ms])`: default 15000. Errore se la SD non è montata.
- `disk.wait_eject([timeout_ms])`: default 120000. Restituisce `"eject"` se il PC ha espulso il disco, `"click"` se l'utente ha premuto il pulsante, `nil` allo scadere.
- `disk.detach()`: torna al solo profilo tastiera.

Se lo script termina con il disco ancora collegato, il dongle lo scollega comunque. I comandi per copiare ed espellere sul PC li digita lo script con `kb.type`: vedi [Copiare file sul PC](../how-to/copiare-file.md).

## Libreria `pcdisk`

Non è nel firmware ma in `sd-template/payloads/lib/pcdisk.lua`: `require("pcdisk")` dopo averla copiata sulla SD.

- `pcdisk.run(corpo [, {timeout = ms}])`: collega la SD al PC, digita nel terminale aperto sul PC una riga che trova il disco, esegue `corpo` ed espelle; poi attende l'espulsione (default 180000 ms) e scollega. Restituisce `"eject"` o `"click"`, errore se scade il tempo.
- `corpo` è una riga di PowerShell (`kb.os()` = `windows`) o di shell POSIX (`linux`, `mac`); `$d` è la radice della SD senza barra finale. Non usare `${` nel corpo.
- Il disco si riconosce dal file `.grillo` nella radice. Il supporto Linux (percorsi di automount di udisks) non è verificato.

## `cfg(chiave [, predefinito])`

Legge un valore di `config.lua` come stringa (es. `cfg("keyboard.layout")`). Restituisce `predefinito` se la chiave manca. Le chiavi `secrets.*` non sono leggibili da qui.

## `require(nome)`

Carica `/payloads/lib/nome.lua` una volta sola (le chiamate successive restituiscono lo stesso valore). `nome` può contenere solo lettere, cifre, `_` e `-`.

```lua
local common = require("common")
common.pause("Premi quando sei pronto")
```
