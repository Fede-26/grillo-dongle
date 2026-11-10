# Collegare il dongle al WiFi

Le app Web HID e Netscan usano il WiFi (Netscan: Network info e LAN hosts; la scansione delle reti non richiede una rete nota).

## Aggiungere una rete nota

In `/config.lua` sulla microSD:

```lua
wifi = {
  { ssid = "Ufficio", pass = "password" },
  { ssid = "Laboratorio", pass = "altra-password" },
},
```

All'avvio dell'app il dongle cerca le reti e guarda quali reti note sono visibili:

- una sola: si collega a quella;
- più di una: sul display compare il menu **WiFi** con le reti note visibili e il segnale (RSSI in dBm, la più forte in alto). Scegli con click e pressione lunga. Se non premi nulla entro 6 secondi (il conto alla rovescia è nel titolo) si collega alla più forte; un click ferma il conto alla rovescia. Tenere premuto 3 secondi esce dall'app senza collegarsi.

L'ordine dell'elenco in `config.lua` non conta. Sono supportate solo reti WPA2/WPA3-Personal: le reti aperte no, e una voce senza `pass` viene ignorata (con un avviso nel log).

## Se nessuna rete nota è raggiungibile

Il dongle crea un proprio hotspot, `Grillo-XXXX` (XXXX = ultime cifre del MAC), con password `grillo-dongle`. Per cambiare nome e password:

```lua
hotspot = { ssid = "MioGrillo", pass = "cambiami123" },
```

La password deve avere almeno 8 caratteri, altrimenti vale quella di default.

## Verificare

Sul display dell'app compare l'indirizzo IP e un indicatore WiFi (`AP` se sei sull'hotspot). Con un hotspot l'indirizzo è sempre quello del dongle sull'hotspot; sulla rete nota è quello assegnato dal router.

La connessione è mantenuta con il risparmio energetico disattivato: la pagina web risponde più in fretta, a costo di un consumo maggiore.
