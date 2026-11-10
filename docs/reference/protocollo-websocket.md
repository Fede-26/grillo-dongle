# Protocollo WebSocket del Web HID

L'app Web HID serve una pagina e un canale WebSocket che trasporta i comandi. Il server è il componente `websrv` (sopra `esp_http_server`); un solo client alla volta. Le rotte `/cfg` e `/ws`, l'accesso con `p<PIN>` e le risposte `ok`, `no`, `locked` e `busy` sono del componente e valgono per ogni app web; i comandi sono del Web HID.

## Endpoint

| Percorso | Metodo | Risposta |
| --- | --- | --- |
| `/` | GET | La pagina (HTML compresso gzip, `Cache-Control: no-store`). |
| `/cfg` | GET | `pin` se serve il PIN, `nopin` altrimenti. La pagina la usa per decidere se mostrare il campo PIN. |
| `/ws` | GET (upgrade WebSocket) | Canale dei comandi. |

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
| `no` | PIN errato. |
| `locked` | Troppi tentativi da questo indirizzo: bloccato per 30 secondi dopo 5 PIN errati. |
| `busy` | Un client con un altro indirizzo è già collegato. Arriva prima del controllo del PIN: durante una sessione non si possono provare PIN. |
| `L<layout> <os>` | Layout e OS correnti, inviato dopo l'accesso per allineare i selettori (es. `Lcolemak mac`). |
| `nohost` | Il PC non risponde alla tastiera USB. |
| `hostok` | Il PC risponde di nuovo, dopo un `nohost`. |
| `untypeable n` | `n` caratteri dell'ultimo `t` non erano nel layout e sono stati digitati come `?`. |

## Note

- Il PIN è di 6 cifre, generato a ogni avvio dell'app e mostrato sul display. L'indirizzo IP del client è mostrato sul display (abbreviato).
- Il canale non è cifrato: la protezione sta nel PIN e nella rete scelta. Vedi [Sicurezza](../explanation/sicurezza.md).
