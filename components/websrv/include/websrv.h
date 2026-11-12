#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

// HTTP + WebSocket server for the apps driven by a web page (Web HID, in the
// future the web file browser and the web injector). Only one instance at a
// time: it belongs to the foreground app, which stops it on exit.
//
// Fixed routes:
//   GET /cfg  "pin" or "nopin": the page knows whether to ask for the PIN
//             without using up attempts.
//   GET /ws   WebSocket. The handshake is refused if the Origin header is not
//             http://<dongle IP> (an arbitrary site open in a browser on the
//             same network cannot connect). The first message must be
//             "p<PIN>"; the server
//             replies "ok", "no" (wrong PIN), "locked" (too many errors from
//             that address, 30 s) or "busy" (session opened by another
//             device). After "ok" comes "T<token>" for the HTTP routes
//             (websrv_authorized); then the text messages go to the app.
//
// Only one authenticated client at a time. The same IP address with the
// right PIN takes over its own session: it is the phone reconnecting while
// the old connection has not been closed yet.

typedef struct {
    const char *pin;   // "" = no PIN: anyone on the network gets in
    size_t stack_size; // of the httpd task: depends on what the callbacks do
    size_t max_frame;  // a longer message closes the connection
    // Text message from the authenticated client, 0-terminated. Runs in the
    // httpd task: a long job blocks the other requests and must be
    // interrupted when websrv_stopping() becomes true.
    void (*on_message)(httpd_req_t *req, char *msg, size_t len);
    // Optional: right after "ok", to send the initial state to the page.
    void (*on_auth)(httpd_req_t *req);
    // Optional: the authenticated client left or was replaced, and on every
    // websrv_stop. To release what the session was holding (e.g. pressed
    // keys).
    void (*on_session_end)(void);
} websrv_config_t;

esp_err_t websrv_start(const websrv_config_t *cfg);
void websrv_stop(void);

// Static page already gzip-compressed (the browser decompresses it by
// itself), typically "/" with the file embedded in the firmware. After
// websrv_start.
esp_err_t websrv_add_page(const char *uri, const char *type, const uint8_t *gz, size_t len);
// Any static file (theme, fonts, images), at most 8 including the page.
// WEBSRV_GZIP: the content is already gzipped. WEBSRV_CACHE: the browser may
// keep it for a week (only for what does not change with the firmware).
enum { WEBSRV_GZIP = 1 << 0, WEBSRV_CACHE = 1 << 1 };
esp_err_t websrv_add_file(const char *uri, const char *type, const uint8_t *data, size_t len, unsigned flags);
// Additional route (API, download). After websrv_start. It is not protected
// by the PIN: the handler calls websrv_authorized() before doing anything.
esp_err_t websrv_add_route(const httpd_uri_t *uri);
// true if the request carries the authenticated session's token, in the
// X-Grillo-Token header or in the t parameter (for download links). The token
// reaches the page right after "ok", in the "T<token>" message, and changes
// at every login: relying on the IP would not be enough, behind a NAT several
// devices share it.
bool websrv_authorized(httpd_req_t *req);
// Query string parameter `key`, decoded (%XX, +). false if missing or if it
// does not fit in `len`.
bool websrv_query(httpd_req_t *req, const char *key, char *out, size_t len);
// true if the request comes from the authenticated client's address.
bool websrv_is_client(httpd_req_t *req);

// Text message to the client of the current request.
void websrv_reply(httpd_req_t *req, const char *msg);
// Text message to the authenticated client, from any task (for example the
// app's). Copied: the buffer can be reused immediately.
// ESP_ERR_INVALID_STATE if nobody is connected.
esp_err_t websrv_send(const char *msg);
// IP of the authenticated client, empty string if none.
const char *websrv_client_ip(void);
// true from when websrv_stop was called: httpd_stop waits for the requests
// in progress to finish, so a long job in on_message must check it and
// stop.
bool websrv_stopping(void);
