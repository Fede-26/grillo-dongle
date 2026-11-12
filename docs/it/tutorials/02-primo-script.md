[English](../../tutorials/02-first-script.md) · **Italiano**

# Il tuo primo script

In questo tutorial scrivi uno script Lua che apre il Blocco note su Windows e ci digita un testo, mostrando l'avanzamento sul display del dongle.

Prerequisiti: [Prima installazione](01-prima-installazione.md) completata.

## 1. Crea il file

Sulla microSD crea `/payloads/blocco-note.lua`:

```lua
--- @name Blocco note
--- @desc Apre il Blocco note e scrive una riga.
--- @layout it
--- @os windows

ui.status("Apro il Blocco note")
kb.press("GUI", "r")          -- Win+R
sys.sleep(500)
kb.type("notepad\n")
sys.sleep(1500)               -- attende che la finestra si apra

ui.status("Scrivo")
kb.type("Ciao dal grillo, oggi è ${giorno}.\n", {giorno = "lunedì"})
ui.status("Fatto")
```

Le prime righe `--- @...` sono l'[intestazione](../reference/intestazione-script.md): dicono all'injector come mostrare lo script e con quale tastiera digitare.

## 2. Eseguilo

1. Inserisci la scheda nel dongle e collegalo al PC.
2. Injector → **Blocco note** → pressione lunga per avviare.
3. Il PC apre il Blocco note e il dongle digita. `è` e `ì` escono corretti perché il layout è `it`.

## 3. Cosa hai usato

- `kb.press("GUI", "r")` preme insieme i tasti: [`kb.press`](../reference/lua-api.md#kbpresstasto-).
- `kb.type` scrive testo; `${giorno}` è sostituito dal valore della tabella.
- `sys.sleep(ms)` aspetta: le pause servono perché il PC è più lento del dongle.
- `ui.status` aggiorna il testo sul display.

## 4. Se qualcosa non va

- Il Blocco note non si apre: aumenta le pause; oppure imposta `--- @start_delay 3000` nell'intestazione per dare più tempo al PC dopo l'inserimento.
- I caratteri sono sbagliati: layout o sistema operativo nell'intestazione non corrispondono al PC. Vedi [Scegliere layout e sistema operativo](../how-to/layout-e-os.md).
- Errore con riga e file sul display: leggi il messaggio; il log completo è in `/logs/grillo.log` sulla scheda.

Tutte le funzioni sono elencate nell'[API Lua](../reference/lua-api.md).
