# Intestazione degli script

Ogni script `.lua` in `/payloads` può iniziare con un blocco di commenti che lo descrive e ne imposta i parametri. L'injector lo legge senza eseguire lo script, per mostrarlo nell'elenco e applicare le impostazioni prima della partenza.

```lua
--- @name Installa kiosk
--- @desc Configura il browser in modalità kiosk.
--- @tags kiosk, setup
--- @layout it
--- @os windows
--- @key_delay 12
```

## Regole

- L'intestazione è il blocco iniziale di righe che cominciano con `--`: finisce alla prima riga che non è un commento. Le righe vuote nel blocco sono ammesse.
- Una riga è una direttiva solo se inizia con `--- @` (tre trattini, spazio, `@`). Gli altri commenti sono ignorati.
- Formato: `--- @chiave valore`. Una chiave sconosciuta o un valore numerico non valido rendono lo script non avviabile, con il numero di riga nel messaggio.
- Un parametro assente prende il valore di `config.lua` (sezione `keyboard`), che a sua volta ha un default interno.

## Chiavi

| Chiave | Valore | Effetto |
| --- | --- | --- |
| `name` | testo | Titolo nell'elenco. Se manca: nome del file senza `.lua`. |
| `desc` | testo (max 160 caratteri) | Descrizione mostrata nell'anteprima. Se non entra in 3 righe si gira a pagine, da sola ogni 3,5 secondi o subito con un click (la pagina corrente, `1/3`, è a destra). |
| `tags` | parole separate da virgole o spazi | Gruppi nella vista "per tag" dell'injector. |
| `layout` | `us`, `it`, `colemak` | Layout della tastiera del PC di destinazione. |
| `os` | `windows`, `linux`, `mac` | Sistema operativo del PC: sceglie livelli AltGr/Option, tasti morti e ripieghi. |
| `key_delay` | ms | Pausa tra i tasti (default 8). |
| `hold` | ms | Durata della pressione di ogni tasto (default 5). |
| `jitter` | ms | ± ms casuali sulla pausa (default 0). |
| `line_delay` | ms | Pausa in più dopo ogni `\n` (default 0). |
| `start_delay` | ms | Attesa dopo che il PC ha montato la tastiera, prima di partire (default 1000). |
| `timeout` | secondi | Interrompe lo script se dura di più. |
| `led` | `effetto [RRGGBB]` | Effetto del LED durante l'esecuzione: `off`, `solid`, `pulse`, `blink`, `rainbow`, con colore esadecimale facoltativo. |

## Viste dell'injector

L'elenco può essere navigato per cartella, per sistema operativo o per tag. Nelle righe, il layout è abbreviato (`cmk` per Colemak) e separato dal sistema operativo da `/`. Cartelle e script sono cercati fino a 3 livelli sotto `/payloads`; la cartella `lib` e i file nascosti o AppleDouble (`._*`) sono saltati.

Prima di avviare uno script, l'injector mostra sempre una scheda con nome, layout e OS: pressione lunga per avviare, doppio click per tornare indietro. Con l'autorun la scheda non compare.
