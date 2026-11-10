# Usare Netscan

Netscan è l'app per capire che rete hai davanti. Ha tre schermate, che si scelgono dal menu iniziale. Gesti: click = giù, doppio click = su, pressione lunga = scegli, tenuto premuto 3 secondi = esci dall'app. In ogni elenco la riga `Back` chiude la schermata (dalla prima riga, un doppio click ci porta in fondo).

Non fa scansioni di porte. Traffico che invia: richieste ARP alla /24, query NBNS (UDP 137) e mDNS (UDP 5353) agli host trovati per conoscerne il nome, un ping al gateway, una query DNS, una richiesta HTTP e la sincronizzazione dell'ora via NTP (fatta a ogni collegamento a una rete, da qualunque app).

## Reti WiFi

Cerca le reti visibili, la più forte per prima, con canale e RSSI (dBm: più vicino a 0 = più forte). Una pressione lunga su una rete mostra SSID, BSSID, vendor del produttore (se c'è `oui.bin`), canale, segnale e tipo di sicurezza. `Rescan` ripete la scansione.

Serve per scegliere un canale libero o per verificare che l'access point del cliente copra la zona. Non richiede una rete nota.

## Network info

Si collega a una delle reti di `wifi` in `config.lua` (con più reti visibili, scegli dal menu: vedi [Collegare il dongle al WiFi](wifi.md)) e controlla in sequenza:

| Riga | Significato |
| --- | --- |
| `WiFi` | Rete e segnale. |
| `IP` | Indirizzo e prefisso assegnati dal DHCP. |
| `GW` | Ultimi ottetti del gateway e tempo di risposta al ping. Giallo "no reply" non è un errore: molti router non rispondono al ping. |
| `DNS` | Server DNS e tempo per risolvere un nome. |
| `Web` | `Online`: internet raggiungibile. `Captive portal`: la rete intercetta la richiesta e serve una pagina di accesso (risposta 200 o 3xx). `HTTP nnn`: un'altra risposta inattesa. `No internet`: nessuna risposta entro 4 secondi, oppure la query DNS è fallita (in quel caso anche la riga `DNS` è rossa). |

Il controllo web è una `GET /generate_204` a `connectivitycheck.gstatic.com` (a memoria: l'endpoint di Google che risponde 204 senza corpo). Per usare un altro host imposta `netscan.probe_host`. Click = ripeti, doppio click = torna al menu.

## LAN hosts

Si collega come sopra e cerca gli host della rete: una richiesta ARP per ogni indirizzo della /24 del dongle (o della rete, se più piccola), poi nome e produttore di quelli trovati. Dura circa 10 secondi. Ogni riga mostra l'IP e, a destra, il nome o il produttore; una pressione lunga apre il dettaglio con il MAC.

I nomi arrivano da NBNS (porta UDP 137, rispondono i Windows) e da mDNS (porta 5353, rispondono Mac, Linux e molte stampanti). Un host che non risponde a nessuno dei due resta senza nome.

Limiti da conoscere:

- Un access point con *client isolation* nasconde gli host agli altri client WiFi: la lista sarà quasi vuota. Non si può aggirare dal dongle.
- Un dispositivo in risparmio energetico può non rispondere alla richiesta ARP e non comparire. Ripeti la scansione.
- Un MAC con il secondo bit del primo byte a 1 è "privato" (casuale, tipico di telefoni e PC recenti): il produttore non si può ricavare e la riga mostra `(private MAC)`.

## Salvataggio

Ogni scansione WiFi e LAN viene salvata in `/scans/` sulla microSD, in CSV (`wifi-…csv`, `lan-<rete>-…csv`). Leggili con l'app USB disk. Il dongle non ha un orologio a batteria: l'ora viene da NTP appena il dongle si collega a una rete (o da `host.clock()` in uno script) e il nome del file contiene la data; senza ora il nome ha un numero progressivo.

Nel CSV i valori che inizierebbero con `=`, `+`, `-` o `@` (non numeri) hanno un apice davanti: un SSID o un nome host scelti da altri non devono diventare formule quando apri il file con un foglio di calcolo.

## Mostrare il produttore: `oui.bin`

Senza file, Netscan mostra solo il MAC. Per i produttori:

1. Scarica a mano la lista dell'IEEE, `oui.csv`, da <https://standards-oui.ieee.org/oui/oui.csv> (o `oui.txt` dalla stessa cartella).
2. Convertila: `tools/make_oui.py oui.csv oui.bin`.
3. Copia `oui.bin` nella radice della microSD.

Il formato permette la ricerca binaria direttamente sulla scheda, senza caricarlo in RAM.

## Chiavi di `config.lua`

Vedi la [reference](../reference/config-lua.md): `timezone`, `ntp`, `netscan.probe_host`.
