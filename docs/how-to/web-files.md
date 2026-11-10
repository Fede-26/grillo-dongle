# Usare Web Files

Web Files apre la microSD del dongle in una pagina web, dal telefono o da un computer: sfogliare le cartelle, caricare e scaricare file, rinominarli e spostarli, eliminarli (con un cestino) e modificare i testi, con un editor per gli script Lua e uno a tabella per i CSV. La pagina vede tutta la scheda, `config.lua` e i log compresi.

## Avviare

1. Alimenta il dongle: Web Files non usa la USB, basta una qualunque presa (vedi [Telefono Android](#telefono-android) per il collegamento a un telefono).
2. Nel launcher scegli **Web Files**. Il dongle si collega al WiFi (o crea l'hotspot): vedi [Collegare il dongle al WiFi](wifi.md).
3. Sul display compaiono un QR, il nome della rete, l'indirizzo IP e un PIN di 6 cifre, come nel [Web HID](web-hid.md). Con l'hotspot si parte dal QR delle credenziali WiFi e, appena un dispositivo si collega, il display passa da solo a quello dell'indirizzo; un click alterna i due.
4. Apri l'indirizzo (o inquadra il QR) e inserisci il PIN.

Tieni premuto il pulsante 3 secondi per uscire.

## Sfogliare

In alto il percorso (**SD / payloads / lib**): ogni pezzo riporta a quella cartella. L'elenco si ordina per nome, dimensione o data; **Hidden** mostra anche i file nascosti (quelli che iniziano con `.`, come i `._*` che macOS lascia sulla scheda, e `System Volume Information`). A destra, lo spazio libero della scheda.

Toccare una cartella la apre; toccare un file di testo (`.lua`, `.csv`, `.txt`, `.md`, `.json`, `.log` e simili) lo apre nell'editor. Il pulsante **⋯** di ogni riga mostra le azioni:

- **Edit**: solo per i file di testo fino a 64 KB.
- **Download**: scarica il file.
- **Rename / move**: il percorso completo è modificabile, quindi lo stesso campo serve a rinominare e a spostare (`/payloads/a.lua` → `/payloads/old/a.lua`). La cartella di destinazione deve esistere.
- **Copy**: solo file.
- **Move to trash**: vedi [Cestino](#cestino).
- **Delete permanently**: dopo una conferma; una cartella viene eliminata con tutto il contenuto.

**+ Folder** e **+ File** creano una cartella o un file vuoto (il file di testo si apre subito nell'editor).

## Caricare file

**Upload** sceglie uno o più file, che vanno nella cartella aperta; da un computer si possono anche trascinare sulla pagina. La barra mostra l'avanzamento. Se un file con lo stesso nome esiste già, la pagina chiede se sostituirlo. Il file arriva prima in `<nome>.part` e prende il nome vero solo alla fine: se la connessione cade a metà, il file di prima resta intatto.

La velocità misurata sul dispositivo è di circa 90 KB/s in upload e 130 KB/s in download (file di 1,5 MB, WiFi a −50 dBm): adatta a script, CSV e log, lenta per file grandi. Per copiare molti dati usa [USB disk](copiare-file.md).

## Editor

L'editor apre i file di testo fino a 64 KB.

- **Save** (o Ctrl+S, Cmd+S sul Mac) salva; un pallino accanto al nome segnala le modifiche non salvate, e chiudere la pagina o l'editor con modifiche chiede conferma.
- **Find** (o Ctrl+F, Cmd+F) cerca senza distinguere le maiuscole; Invio passa al risultato successivo.
- Tab e Shift+Tab rientrano e tolgono il rientro (4 spazi), anche su più righe selezionate; Invio mantiene il rientro della riga. Esc lascia la casella di testo, così Tab torna a spostarsi tra i pulsanti. Annulla (Ctrl+Z) funziona anche dopo questi inserimenti.
- Negli script Lua parole chiave, stringhe, commenti, numeri e moduli del dongle (`kb`, `ui`, `sys`, ...) hanno colori diversi.
- **Check** (solo `.lua`) manda il testo al dongle, che lo compila senza eseguirlo: un errore di sintassi compare sopra l'editor con il numero di riga evidenziato, e il cursore va su quella riga. Per gli script dell'injector (in `/payloads`, non in `lib`) controlla anche l'intestazione `--- @chiave` (vedi [Intestazione degli script](../reference/intestazione-script.md)). Il testo non deve essere già salvato.

### CSV

I file `.csv` e `.tsv` si aprono come tabella: ogni cella è modificabile, **+ Row** e **− Row** aggiungono una riga sotto quella della cella selezionata o la tolgono, **+ Col** e **− Col** fanno lo stesso con le colonne. Il separatore (virgola, punto e virgola o tabulazione) è riconosciuto dalla prima riga; se è sbagliato si cambia dal menu, e il file viene riletto. Campi con separatori, virgolette o a capo sono scritti tra virgolette (RFC 4180); le virgolette superflue spariscono al salvataggio. **Text** passa al testo grezzo, **Table** torna alla tabella.

### config.lua

`config.lua` si apre con le password nascoste: al posto dei valori di `pass`/`password` (reti WiFi, hotspot) e di tutto ciò che sta in `secrets` compare `"<secret:1>"`, `"<secret:2>"`, ... Lascia i segnaposto dove sono (o spostali): al salvataggio il dongle rimette i valori veri presi dal file attuale. Per cambiare una password scrivi il valore nuovo al posto del segnaposto. **Show secrets** ricarica il file con i valori in chiaro.

La configurazione nuova entra in funzione quando esci da Web Files, perché le app in corso stanno usando quella vecchia. Prima di salvarlo la pagina controlla la sintassi e, se trova un errore, chiede conferma: un `config.lua` che non si carica fa comparire "config.lua error" all'uscita e lascia il dongle con i valori predefiniti, quindi senza reti note (crea l'hotspot con la password di default, `grillo-dongle`). Da lì puoi correggere il file con Web Files sull'hotspot. Il controllo trova gli errori di sintassi, non un valore sbagliato.

## Cestino

**Move to trash** sposta l'elemento in `/.trash` sulla scheda, insieme al percorso da cui veniva e alla data (se il dongle conosce l'ora). **Trash** mostra il contenuto:

- **Restore** lo rimette al suo posto. Se nel frattempo lì è comparso un elemento con lo stesso nome, quello ripristinato prende un suffisso (`a (2).lua`); se la cartella d'origine non c'è più viene ricreata.
- **Delete permanently** lo elimina dal cestino.
- **Empty trash** elimina tutto il cestino.

Il cestino è una cartella normale della scheda: occupa spazio finché non lo svuoti, e si vede anche dal PC con [USB disk](copiare-file.md).

## Telefono Android

Collegato alla porta USB-C di un telefono Android, il dongle viene alimentato e Web Files funziona: per leggere la scheda dal telefono apri la pagina sul WiFi (o sull'hotspot del dongle). Web Files non espone la scheda sulla USB.

Nelle impostazioni USB Android indica quale dei due dispositivi controlla il collegamento. Non cambiare l'impostazione: il dongle fa solo da periferica USB, e provando a invertire i ruoli il dongle si riavvia. La causa più probabile è che durante il cambio di ruolo il telefono tolga per un attimo l'alimentazione alla porta (ricostruzione, non misurata).

## Senza PIN

In `config.lua`, `webfiles = { pin = false }`. Chiunque sulla stessa rete potrebbe allora leggere, modificare ed eliminare tutto ciò che è sulla microSD, password di `config.lua` comprese. Vedi [Sicurezza](../explanation/sicurezza.md).
