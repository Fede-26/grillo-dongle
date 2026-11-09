# Protocollo WebSocket del Web HID

L'app Web HID serve una pagina e un canale WebSocket che trasporta i comandi. Il server è `esp_http_server` del dongle; un solo client alla volta.

## Endpoint

| Percorso | Metodo | Risposta |
| --- | --- | --- |
| `/` | GET | La pagina (HTML compresso gzip, `Cache-Control: no-store`). |
| `/cfg` | GET | `pin` se serve il PIN, `nopin` altrimenti. La pagina la usa per decidere se mostrare il campo PIN. |
| `/ws` | GET (upgrade WebSocket) | Canale dei comandi. |

## Messaggi

Ogni messaggio è un frame di testo: un carattere di tipo seguito dagli argomenti. Il primo messaggio deve essere l'accesso; i comandi di un client non autenticato sono ignorati.

### Client → dongle

| Messaggio | Significato |
| --- | --- |
| `p<PIN>` | Accesso. Con PIN disattivato l'argomento è ignorato (`p`). |
| `m dx dy` | Sposta il mouse (relativo). |
| `b maschera` | Stato dei pulsanti del mouse (bit 0 sinistro, 1 destro, 2 centrale). |
| `w dy [dx]` | Rotella. |
| `t testo` | Digita il testo con il layout e l'OS correnti. |
| `c mods carattere` | Combinazione: maschera dei modificatori HID più il tasto che produce il carattere (es. Ctrl+C). |
| `k mods usage` | Combinazione con usage HID grezzo: tasti speciali, F-key. `usage` 0 = solo modificatori. |
| `e n` | `n` Backspace di seguito (1..200). Lo usa il live typing. |
| `l nome` | Layout del PC: `it`, `us`, `colemak`. |
| `o nome` | OS del PC: `windows`, `linux`, `mac`. |

I frame sono limitati in dimensione; un frame vuoto o troppo lungo chiude il canale.

### Dongle → client

| Messaggio | Significato |
| --- | --- |
| `ok` | Accesso riuscito. |
| `no` | PIN errato. |
| `locked` | Troppi tentativi: accessi bloccati per 30 secondi dopo 5 PIN errati. |
| `busy` | Un altro client è già collegato. |
| `L<layout> <os>` | Layout e OS correnti, inviato dopo l'accesso per allineare i selettori (es. `Lcolemak mac`). |
| `nohost` | Il PC non risponde alla tastiera USB. |

## Note

- Il PIN è di 6 cifre, generato a ogni avvio dell'app e mostrato sul display. L'indirizzo IP del client è mostrato sul display (abbreviato).
- Il canale non è cifrato: la protezione sta nel PIN e nella rete scelta. Vedi [Sicurezza](../explanation/sicurezza.md).
