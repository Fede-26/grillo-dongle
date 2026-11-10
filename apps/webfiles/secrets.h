#pragma once

#include <stdbool.h>
#include <stddef.h>

// Segreti di config.lua nascosti alla pagina web, che non è cifrata.
//
// Sono segreti i letterali stringa assegnati a una chiave `pass` o
// `password` (reti WiFi, hotspot) e tutti quelli dentro la tabella
// `secrets = { ... }`. Il testo è analizzato come Lua (commenti, stringhe
// con escape e lunghe), non con ricerche testuali: una "pass" in un commento
// non conta, e una stringa con "}" dentro non chiude la tabella.

// Copia di `src` con ogni segreto sostituito da "<secret:N>" (N da 1, in
// ordine). Da liberare con free(); NULL senza memoria.
char *secrets_mask(const char *src);

// Testo modificato dalla pagina con i segnaposto ancora dentro: ogni
// letterale "<secret:N>" (in qualunque punto, non solo dove era) torna al
// letterale N-esimo di `orig` così com'era scritto. Un N che `orig` non ha
// è un errore: NULL con `*unknown` a true. Da liberare con free(); NULL
// anche senza memoria.
char *secrets_unmask(const char *edited, const char *orig, bool *unknown);
