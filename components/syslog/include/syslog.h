#pragma once

// Copia su SD (/sd/logs/grillo.log) i log di livello warning ed error, più
// le righe info con tag "script" (sys.log degli script dell'injector), oltre
// alla console normale. Serve quando la USB è occupata da HID/MSC e
// la seriale non è disponibile. Rotazione semplice: oltre 256 KB il file
// diventa grillo.1.log. Quando l'ora è nota (componente clock) ogni riga
// inizia con data e ora locali.
void syslog_init(void);

// Scrive subito su SD le righe in coda (es. prima di prestare la SD all'host).
void syslog_flush(void);
