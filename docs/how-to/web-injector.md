# Usare il Web Injector

Il Web Injector esegue gli script dell'injector scelti da una pagina web, dal telefono o da un altro computer. Gli script sono gli stessi di `/payloads` sulla microSD e girano come nell'Injector: stesse impostazioni, stessa intestazione, stesso esito. La pagina non carica né modifica script.

## Avviare

1. Collega il dongle al PC su cui far girare lo script.
2. Nel launcher scegli **Web Injector**. Il dongle si collega al WiFi (o crea l'hotspot): vedi [Collegare il dongle al WiFi](wifi.md).
3. Sul display compaiono un QR, il nome della rete, l'indirizzo IP e un PIN di 6 cifre, come nel [Web HID](web-hid.md). Con l'hotspot un click alterna il QR delle credenziali WiFi e quello dell'indirizzo.
4. Apri l'indirizzo dal telefono (o inquadra il QR) e inserisci il PIN.

Tieni premuto il pulsante 3 secondi per uscire.

## La pagina

- **Folders**: le cartelle di `/payloads`, come sul display. A destra di ogni script, layout e sistema operativo effettivi (intestazione, altrimenti `config.lua`).
- **By OS** e **By tag**: gli script raggruppati per sistema operativo o per tag (`@tags`), come le viste "/By OS" e "/By tag" dell'Injector.

Toccando uno script si apre la scheda (nome, descrizione, layout, OS, tag, timeout); **Run** lo avvia. Un'intestazione non valida mostra l'errore e non si può eseguire.

Durante l'esecuzione la pagina mostra nome, stato (`ui.status`), barra di avanzamento (`ui.progress`) e le righe di `sys.log`; **Abort** interrompe lo script come i 3 secondi di pressione. I dialoghi dello script compaiono anche sulla pagina:

- `ui.menu`: le voci come pulsanti;
- `ui.confirm`: No e Yes, con evidenziata la risposta predefinita (il timeout dello script vale anche qui);
- `ui.wait_button`: Click, Double e Long, come i gesti del pulsante.

Display e pulsante restano attivi: si può rispondere da entrambi, e vale la prima risposta. L'esito resta sul display e sulla pagina finché non si preme OK (o il pulsante).

Il pulsante in alto a destra cambia l'aspetto della pagina: **Plain** (sobrio), **Arcade** (pixel art, righe di scansione) e **Retro** (monitor a fosfori ambra). Il browser ricorda la scelta.

## Se la pagina si chiude

Lo script continua. Quando la pagina si ricollega (da sola, finché il PIN è valido) riceve lo stato attuale e l'eventuale dialogo aperto; le righe di log scritte nel frattempo non vengono ripetute.

## Senza PIN

In `config.lua`, `webinjector = { pin = false }`. Chiunque sulla stessa rete potrebbe allora avviare gli script della microSD sul PC collegato. Vedi [Sicurezza](../explanation/sicurezza.md).
