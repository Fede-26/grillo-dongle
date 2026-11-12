#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Server HTTP + WebSocket per le app comandate da una pagina web (Web HID,
// in futuro il file browser web e l'injector web). Una sola istanza alla
// volta: appartiene all'app in primo piano, che la ferma all'uscita.
//
// Rotte fisse:
//   GET /cfg  "pin" o "nopin": la pagina sa se chiedere il PIN senza
//             consumare tentativi.
//   GET /ws   WebSocket. L'handshake è rifiutato se l'header Origin non è
//             http://<IP del dongle> (un sito qualunque aperto in un browser
//             sulla stessa rete non può collegarsi). Il primo messaggio
//             deve essere "p<PIN>"; il server
//             risponde "ok", "no" (PIN errato), "locked" (troppi errori da
//             quell'indirizzo, 30 s) o "busy" (sessione aperta da un altro
//             dispositivo). Dopo "ok" arriva "T<token>" per le rotte HTTP
//             (websrv_authorized); poi i messaggi di testo vanno all'app.
//
// Un solo client autenticato alla volta. Lo stesso indirizzo IP con il PIN
// giusto subentra alla propria sessione: è il telefono che si riconnette
// mentre la vecchia connessione non è ancora stata chiusa.

typedef struct {
    const char *pin;   // "" = nessun PIN: chiunque sulla rete entra
    size_t stack_size; // del task httpd: dipende da cosa fanno i callback
    size_t max_frame;  // un messaggio più lungo chiude la connessione
    // Messaggio di testo del client autenticato, terminato da 0. Gira nel
    // task httpd: un lavoro lungo blocca le altre richieste e va interrotto
    // quando websrv_stopping() diventa true.
    void (*on_message)(httpd_req_t *req, char *msg, size_t len);
    // Facoltativa: subito dopo "ok", per mandare alla pagina lo stato iniziale.
    void (*on_auth)(httpd_req_t *req);
    // Facoltativa: il client autenticato se n'è andato o è stato sostituito,
    // e a ogni websrv_stop. Per rilasciare ciò che la sessione teneva (es.
    // tasti premuti).
    void (*on_session_end)(void);
} websrv_config_t;

esp_err_t websrv_start(const websrv_config_t *cfg);
void websrv_stop(void);

// Pagina statica già compressa con gzip (il browser la decomprime da sé),
// tipicamente "/" con il file incorporato nel firmware. Dopo websrv_start.
esp_err_t websrv_add_page(const char *uri, const char *type, const uint8_t *gz, size_t len);
// File statico qualunque (tema, font, immagini), al massimo 8 compresa la
// pagina. WEBSRV_GZIP: il contenuto è già in gzip. WEBSRV_CACHE: il browser
// può tenerlo una settimana (solo per ciò che non cambia col firmware).
enum { WEBSRV_GZIP = 1 << 0, WEBSRV_CACHE = 1 << 1 };
esp_err_t websrv_add_file(const char *uri, const char *type, const uint8_t *data, size_t len, unsigned flags);
// Rotta aggiuntiva (API, download). Dopo websrv_start. Non è protetta dal
// PIN: l'handler chiama websrv_authorized() prima di fare qualcosa.
esp_err_t websrv_add_route(const httpd_uri_t *uri);
// true se la richiesta porta il token della sessione autenticata, nell'header
// X-Grillo-Token o nel parametro t (per i link di download). Il token arriva
// alla pagina subito dopo "ok", nel messaggio "T<token>", e cambia a ogni
// accesso: basarsi sull'IP non basterebbe, dietro un NAT più dispositivi lo
// condividono.
bool websrv_authorized(httpd_req_t *req);
// Parametro `key` della query string, decodificato (%XX, +). false se manca
// o non entra in `len`.
bool websrv_query(httpd_req_t *req, const char *key, char *out, size_t len);
// true se la richiesta arriva dall'indirizzo del client autenticato.
bool websrv_is_client(httpd_req_t *req);

// Messaggio di testo al client della richiesta in corso.
void websrv_reply(httpd_req_t *req, const char *msg);
// Messaggio di testo al client autenticato, da un task qualunque (per
// esempio quello dell'app). Copiato: il buffer si può riusare subito.
// ESP_ERR_INVALID_STATE se nessuno è collegato.
esp_err_t websrv_send(const char *msg);
// IP del client autenticato, stringa vuota se nessuno.
const char *websrv_client_ip(void);
// true da quando è stata chiamata websrv_stop: httpd_stop attende che le
// richieste in corso finiscano, quindi un lavoro lungo in on_message deve
// controllarlo e smettere.
bool websrv_stopping(void);
