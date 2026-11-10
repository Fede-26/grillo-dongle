# Usare il Web Injector

Il Web Injector esegue gli script dell'injector scelti da una pagina web, dal telefono o da un altro computer. Gli script sono gli stessi di `/payloads` sulla microSD e girano come nell'Injector: stesse impostazioni, stessa intestazione, stesso esito. La pagina non carica né modifica script.

## Avviare

1. Collega il dongle al PC su cui far girare lo script.
2. Nel launcher scegli **Web Injector**. Il dongle si collega al WiFi (o crea l'hotspot): vedi [Collegare il dongle al WiFi](wifi.md).
3. Sul display compaiono un QR, il nome della rete, l'indirizzo IP e un PIN di 6 cifre, come nel [Web HID](web-hid.md). Con l'hotspot si parte dal QR delle credenziali WiFi e, appena un dispositivo si collega, il display passa da solo a quello dell'indirizzo; un click alterna i due.
4. Apri l'indirizzo dal telefono (o inquadra il QR) e inserisci il PIN.

Tieni premuto il pulsante 3 secondi per uscire.

## La pagina

- **Folders**: le cartelle di `/payloads`, come sul display. A destra di ogni script, layout e sistema operativo effettivi (intestazione, altrimenti `config.lua`).
- **By OS** e **By tag**: gli script raggruppati per sistema operativo o per tag (`@tags`), come le viste "/By OS" e "/By tag" dell'Injector.

Toccando uno script si apre la scheda (nome, descrizione, layout, OS, tag, timeout); **Run** lo avvia. Un'intestazione non valida mostra l'errore e non si può eseguire.

Durante l'esecuzione la pagina mostra nome, stato (`ui.status`), barra di avanzamento (`ui.progress`) e la cronologia della run, una voce per riga con l'ora (quella del dongle se la conosce, altrimenti il tempo dall'avvio): avvio, righe di `sys.log`, ogni stato nuovo, i dialoghi con la risposta data (e se è arrivata dalla pagina), l'esito. I soli avanzamenti della barra non entrano. Se stai rileggendo più su, la lista non scorre da sola. **Abort** interrompe lo script come i 3 secondi di pressione. I dialoghi dello script compaiono anche sulla pagina:

- `ui.menu`: le voci come pulsanti;
- `ui.confirm`: No e Yes, con evidenziata la risposta predefinita (il timeout dello script vale anche qui);
- `ui.wait_button`: Click, Double e Long, come i gesti del pulsante.

Display e pulsante restano attivi: si può rispondere da entrambi, e vale la prima risposta.

Alla fine la pagina resta sull'esecuzione: l'esito (Done, Error con il messaggio, Aborted, Timeout) compare sopra la cronologia. Sul display l'esito resta finché non si preme **OK** sulla pagina o il pulsante del dongle. **Back** torna all'elenco e **Run again** riavvia lo stesso script; entrambi chiudono anche l'esito sul display. Dall'elenco, **Last run** riapre l'ultima esecuzione.

Il pulsante in alto a destra cambia l'aspetto della pagina: **Plain** (sobrio), **Arcade** (pixel art, righe di scansione) e **Retro** (monitor a fosfori ambra). Il browser ricorda la scelta.

## Se la pagina si chiude

Lo script continua. Quando la pagina si ricollega (da sola, finché il PIN è valido) riceve tutta la cronologia della run, anche le voci scritte a pagina chiusa, lo stato attuale, l'eventuale dialogo aperto e, a run finita, l'esito. Il dongle tiene le ultime 100 voci, ognuna fino a 200 byte; quando ne ha scartate la pagina lo dice in cima. La cronologia dell'ultima run resta finché non ne parte un'altra o non si esce dall'app.

## Senza PIN

In `config.lua`, `webinjector = { pin = false }`. Chiunque sulla stessa rete potrebbe allora avviare gli script della microSD sul PC collegato. Vedi [Sicurezza](../explanation/sicurezza.md).
