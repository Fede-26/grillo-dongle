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
- Formato: `--- @chiave valore`. Una chiave sconosciuta, un valore numerico non valido o fuori intervallo, un `@layout` o un `@os` sconosciuti rendono lo script non avviabile, con il numero di riga nel messaggio.
- Un parametro assente prende il valore di `config.lua` (sezione `keyboard`), che a sua volta ha un default interno.

## Chiavi

| Chiave | Valore | Effetto |
| --- | --- | --- |
| `name` | testo | Titolo nell'elenco. Se manca: nome del file senza `.lua`. |
| `desc` | testo (max 159 byte: una lettera accentata ne vale 2) | Descrizione mostrata nell'anteprima. Se non entra in 3 righe scorre da sola una riga alla volta, con una barra sottile a destra; un click la fa avanzare subito. |
| `tags` | parole separate da virgole o spazi | Gruppi nella vista "per tag" dell'injector. |
| `layout` | `us`, `it`, `colemak` | Layout della tastiera del PC di destinazione. |
| `os` | `windows`, `linux`, `mac` (anche `win`, `macos`) | Sistema operativo del PC: sceglie livelli AltGr/Option, tasti morti e ripieghi. |
| `key_delay` | ms, 0..60000 | Pausa tra i tasti (default 8). |
| `hold` | ms, 0..60000 | Durata della pressione di ogni tasto (default 5). |
| `jitter` | ms, 0..60000 | ± ms casuali sulla pausa (default 0). |
| `line_delay` | ms, 0..60000 | Pausa in più dopo ogni `\n` (default 0). |
| `start_delay` | ms, 0..60000 | Attesa dopo che il PC ha montato la tastiera, prima di partire (default 1000). |
| `timeout` | secondi, 0..86400 | Interrompe lo script se dura di più, anche durante un `ui.menu` o un `ui.confirm`. |
| `led` | `effetto [RRGGBB]` | Effetto del LED durante l'esecuzione: `off`, `solid`, `pulse`, `blink`, `rainbow`, con colore esadecimale facoltativo. |

## Viste dell'injector

L'elenco può essere navigato per cartella, per sistema operativo o per tag. Nelle righe, il layout è abbreviato (`cmk` per Colemak) e separato dal sistema operativo da `/`. La vista per cartella segue le cartelle a qualunque profondità; le viste per OS e per tag cercano gli script fino a 3 livelli sotto `/payloads`. La cartella `lib` e i file nascosti o AppleDouble (`._*`) sono saltati.

Ogni vista mostra al massimo 48 voci, nell'ordine in cui la scheda le restituisce prima dell'ordinamento alfabetico: oltre, compare un avviso e conviene dividere gli script in sottocartelle.

Prima di avviare uno script, l'injector mostra sempre una scheda con nome, layout e OS: pressione lunga per avviare, doppio click per tornare indietro. Con l'autorun la scheda non compare.
