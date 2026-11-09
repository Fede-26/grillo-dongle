# Collegare il dongle al WiFi

Le app Web HID e Netscan usano il WiFi (Netscan: Network info e LAN hosts; la scansione delle reti non richiede una rete nota). Il dongle sceglie da solo la rete.

## Aggiungere una rete nota

In `/config.lua` sulla microSD:

```lua
wifi = {
  { ssid = "Ufficio", pass = "password" },
  { ssid = "Laboratorio", pass = "altra-password" },
},
```

All'avvio dell'app il dongle cerca le reti, sceglie la prima nota (in ordine di preferenza) tra quelle visibili e vi si collega. Sono supportate solo reti WPA2/WPA3-Personal.

## Se nessuna rete nota è raggiungibile

Il dongle crea un proprio hotspot, `Grillo-XXXX` (XXXX = ultime cifre del MAC), con password `grillo-dongle`. Per cambiare nome e password:

```lua
hotspot = { ssid = "MioGrillo", pass = "cambiami123" },
```

La password deve avere almeno 8 caratteri, altrimenti vale quella di default.

## Verificare

Sul display dell'app compare l'indirizzo IP e un indicatore WiFi (`AP` se sei sull'hotspot). Con un hotspot l'indirizzo è sempre quello del dongle sull'hotspot; sulla rete nota è quello assegnato dal router.

La connessione è mantenuta con il risparmio energetico disattivato: la pagina web risponde più in fretta, a costo di un consumo maggiore.
