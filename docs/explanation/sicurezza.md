# Sicurezza e rischi accettati

Grillo è uno strumento personale per installare e configurare PC. Un dispositivo che digita sul computer a cui è collegato è, per costruzione, potente: questa pagina dice dove sono i confini e quali rischi sono stati accettati di proposito.

## Cosa può fare il dongle

Una volta collegato, il PC lo tratta come una tastiera e un mouse fidati. Può eseguire qualunque azione che un utente può fare con la tastiera, e non esiste alcuna conferma sul PC. Usalo solo su computer che hai il diritto di configurare.

## Rischi accettati

- **Password in chiaro sulla microSD.** `secrets` in `config.lua` non è cifrato. `kb.type_secret` evita solo che uno script le legga come stringa. Chi ha la scheda le legge. Usalo per credenziali di installazione che accetti di esporre così.
- **Web HID senza PIN.** `webhid.pin = false` lascia che chiunque sulla stessa rete (o sull'hotspot) comandi il PC. Il PIN è comunque di 6 cifre, mostrato sul display, con blocco di 30 secondi dopo 5 errori, e il canale WebSocket non è cifrato (niente HTTPS): chi osserva la rete locale vede i comandi e il PIN. Per questo l'accesso è pensato per reti fidate.
- **Hotspot con password di default.** Se non imposti `hotspot.pass` vale `grillo-dongle`, nota a chiunque legga questa documentazione.
- **Un solo client** per volta sul Web HID: chi si collega per primo lo tiene.

## Cosa limita uno script Lua

La VM dell'injector non ha accesso diretto al filesystem (`io`, `os`, `dofile`, `loadfile` assenti), legge solo `/payloads` e `/data` con `fs.*`, ha un limite di memoria di 96 KB, rifiuta il bytecode precompilato e si interrompe con la pressione di 3 secondi. Non limita ciò che lo script digita sul PC.

## Segnalare un problema

Il repository è privato. Segnala problemi direttamente a chi lo gestisce.
