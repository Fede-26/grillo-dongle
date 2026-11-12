[English](../../explanation/layouts-and-dead-keys.md) · **Italiano**

# Layout, sistema operativo e tasti morti

## Il problema

Una tastiera USB non invia caratteri ma codici di tasti fisici (usage HID). È il PC a tradurre «tasto accanto alla L» in `ò` oppure `;`, secondo il layout che ha configurato. Il dongle deve quindi fare il lavoro inverso: dato un carattere da digitare, trovare il tasto (e i modificatori) che lo produce nel layout del PC.

Il layout da solo non basta. Lo stesso layout produce caratteri diversi secondo il sistema operativo: AltGr su Windows e Linux è Option su macOS, e le combinazioni per le lettere accentate sono diverse.

## Come funziona

Per ogni layout (`us`, `it`, `colemak`) `tools/gen_layouts.py` genera:

- una tabella principale (livelli: normale, Shift, AltGr, AltGr+Shift) valida per tutti i sistemi;
- per ciascun sistema, una tabella supplementare di caratteri (`linux`, `mac`, `win`) che integra la principale: il generatore vi mette solo caratteri che la principale non ha. Fa eccezione `linux_extra`, scritta a mano, che può anche sovrascrivere;
- per ciascun sistema, un elenco di sequenze con tasto morto: un tasto che non produce nulla da solo, seguito dalla lettera da accentare (es. AltGr+r poi `a` = `à` sul Colemak di Linux).

`hid_translate` cerca prima nella tabella del sistema, poi in quella principale, poi nelle sequenze con tasto morto; se non trova nulla usa un ripiego, quando esiste.

## Ripieghi

- **Windows**: Alt tenuto premuto e il codice Windows-1252 del carattere sul tastierino numerico. Per i caratteri che hanno un codice in quel codepage.
- **Linux**: Ctrl+Shift+U, il codepoint in esadecimale, spazio. Funziona con le applicazioni GTK/IBus, non in un terminale puro.
- **macOS**: nessuno. Un carattere assente dal layout è un errore (`not typeable`), perché non esiste un metodo di inserimento universale per tastiera.

## Fonti dei dati

Le tabelle Option/AltGr non sono scritte a mano ma lette dal sistema:

- Linux (Colemak): xkeyboard-config, `symbols/us`, sezione `colemak_base`.
- Windows (Colemak): file KLC ufficiale di colemak.com (hash in `tools/layouts/README.md`).
- macOS (Colemak e Italiano - Pro): dump con `UCKeyTranslate` della Carbon API, programma `tools/dump_mac_layout.c`, da rieseguire su un Mac se Apple cambia i layout.

Il layout italiano su Linux e Windows è scritto nella tabella principale (tastiera ISO standard, più `` ` `` e `~` su Linux).

## Il caso `<` e `\` su macOS

Sulle tastiere ISO c'è un tasto in più, accanto al Maiusc sinistro. Per le tastiere ISO macOS scambia i codici interni del tasto accanto al Maiusc e di quello a sinistra dell'1; per decidere se una tastiera è ISO si basa sul riconoscimento fatto al primo collegamento (Keyboard Setup Assistant). Questa è una ricostruzione, con confidenza media: non è stata verificata sulla documentazione Apple.

Il layout `it` su `mac` assume che macOS tratti il dongle come ISO, quindi usa gli stessi tasti di Windows per `<` e `\`. Se il tuo Mac lo riconosce come ANSI, i due caratteri escono scambiati: togli `mac_iso` dalla voce `IT` di `tools/gen_layouts.py` e rigenera (vedi `tools/layouts/README.md`).

L'assunzione non è stata verificata su un Mac con il dongle collegato.
