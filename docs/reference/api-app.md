# API C delle app

Il contratto completo, con i commenti, è in `components/appfw/include/app.h` e `appmgr.h`. Qui un riassunto per chi scrive un'app; il procedimento è in [Aggiungere un'app](../how-to/aggiungere-unapp.md).

## Descrittore

```c
struct app_desc {
    const char *id;            // usato in config.lua (autorun.app)
    const char *name;          // nome nel launcher
    const gfx_icon_t *icon;    // icona 12x12, disegnata 2x
    uint32_t accent;           // colore d'accento di UI e LED
    uint32_t requires;         // maschera di app_req_t
    usb_profile_t usb;         // profilo USB mentre l'app gira (maschera di USB_F_*)
    uint32_t stack;            // stack del task in byte
    void (*run)(app_ctx_t *ctx);
    void (*subtitle)(char *buf, size_t len);   // facoltativa: riga sotto il nome
};
```

`requires`: `APP_REQ_SD` (SD montata FAT su `/sd`), `APP_REQ_CARD` (scheda presente, anche non FAT), `APP_REQ_NET` (userà il WiFi), `APP_REQ_BLE` (riservato). Se un requisito manca, `appmgr_unavailable()` restituisce il motivo e il launcher non avvia l'app.

Profili USB (`usbdev.h`): un profilo è la maschera delle interfacce, `USB_F_KBD`, `USB_F_MOUSE`, `USB_F_CDC` e il disco in uno dei due modi, `USB_F_DISK_RW` (SD all'host in lettura e scrittura, il firmware non la vede) o `USB_F_DISK_RO` (SD in sola lettura per l'host, il firmware la tiene montata). Quelli già pronti: `USB_PROFILE_NONE` (TinyUSB spento, torna la USB-Serial-JTAG), `USB_PROFILE_HID` (tastiera + mouse + CDC), `USB_PROFILE_HID_DISK` (tastiera + disco in sola lettura + CDC, senza mouse), `USB_PROFILE_MSC` (disco in lettura e scrittura). Con `CONFIG_GRILLO_USB_DEBUG_CDC` la CDC si aggiunge a ogni profilo. Le combinazioni con più di 4 endpoint IN (tastiera + mouse + disco + CDC) vengono rifiutate con `ESP_ERR_NOT_SUPPORTED`.

Per cambiare profilo mentre l'app gira: `esp_err_t app_usb_apply(usb_profile_t p)`. Passa la SD all'host o la riprende secondo il modo del disco; se il profilo nuovo fallisce, TinyUSB resta spento e la SD torna al firmware. Non chiamare `usbdev_apply` direttamente: non sposta la SD.

## Ciclo di vita

`run(ctx)` gira in un task dedicato. Quando ritorna, l'app è terminata e il sistema rilascia d'ufficio: profilo USB, SD prestata o condivisa, livello LED dell'app, lease di rete presi dal task dell'app. Un'app può farlo prima per conto suo.

`ctx->arg` è l'argomento di avvio (per l'autorun, il percorso dello script); può essere `NULL`.

Un'app che modifica `/config.lua` chiama `app_config_changed()`: il sistema lo ricarica quando l'app esce, non subito, perché ricaricarlo libera le stringhe di `config_str` che le app in corso stanno usando.

## Eventi

```c
bool app_next_event(app_ctx_t *ctx, app_event_t *ev, uint32_t timeout_ms);
bool app_should_exit(app_ctx_t *ctx);
void app_post(app_ctx_t *ctx, app_ev_type_t type, int32_t arg);
void app_cancel_exit(app_ctx_t *ctx);
```

Tipi: `APP_EV_CLICK`, `APP_EV_DOUBLE`, `APP_EV_LONG`, `APP_EV_EXIT` (hold 3 s: terminare al più presto), `APP_EV_USB`, `APP_EV_NET`, `APP_EV_TICK` e `APP_EV_USER` (generati dall'app con `app_post`, utile da timer e task secondari), `APP_EV_ANSWER` (risposta data da fuori al dialogo aperto: `app_menu*` lo prende come indice della voce, `app_confirm*` come no/sì, `app_message` si chiude; fuori da un dialogo è scartato). Con `APP_EV_ANSWER` una pagina web risponde ai dialoghi dello stesso display, e vale la prima risposta.

`APP_EV_EXIT` è consegnato anche se quando arriva la coda è piena. `app_post(ctx, APP_EV_EXIT, 0)` è anche il modo di chiedere l'uscita dall'interno dell'app (per esempio da un timer di timeout): imposta `exit_requested` e chiude i dialoghi aperti. `app_cancel_exit` annulla una richiesta già ricevuta, quando l'app decide di restare (l'utente risponde "No" a "Uscire?", oppure l'hold ha solo interrotto un lavoro), e scarta gli EXIT ancora in coda.

L'uscita arriva all'app solo quando attende un evento. Se l'app non torna ad attendere entro 3 secondi dalla richiesta, il sistema propone il riavvio con un secondo hold.

## Dialoghi bloccanti

- `app_menu(ctx, titolo, voci, n, selezionata)`: click = giù, doppio = su, lungo = scegli; restituisce l'indice o -1 se l'app deve uscire. La riga selezionata che non entra nel display scorre in orizzontale: un secondo ferma, poi fino alla fine, un secondo ferma, da capo. Le altre righe restano troncate con i puntini.
- `app_menu_ex(...)`: come sopra, con una colonna di testo a destra di ogni riga.
- `app_menu_timeout(..., timeout_ms)`: come `app_menu_ex`; senza gesti entro la scadenza restituisce la voce selezionata. I secondi residui sono nel titolo; un click li ferma.
- `app_confirm(ctx, titolo, testo, predefinito_sì)`.
- `app_confirm_timeout(ctx, titolo, testo, predefinito_sì, timeout_ms)`: come sopra; se non c'è alcun gesto entro `timeout_ms` (0 = mai) restituisce il predefinito. Un click disattiva la scadenza.
- `app_message(ctx, titolo, testo, colore)`: attende click o lungo.

Nei dialoghi il testo lungo scorre da solo. Gli eventi che il dialogo non usa (USB, rete, TICK, USER) tornano in coda alla chiusura, nell'ordine di arrivo. Alla chiusura il titolo della barra torna quello di prima.

## Disegno

Lo schermo è 160x80. Una barra di stato di 12 pixel più una linea d'accento appartengono al sistema; l'area sotto (da `UI_CONTENT_Y`) è dell'app.

```c
ui_begin();            // prende il lock, pulisce l'area contenuto
...                    // gfx_* e ui_draw_*
ui_end();              // ridisegna la barra, invia il frame
```

Primitive: `gfx_text`, `gfx_text_aligned`, `gfx_text_scaled`, `gfx_text_fit`; `ui_draw_row`, `ui_draw_list`, `ui_draw_progress`, `ui_draw_pager`, `ui_draw_wrapped`, `ui_draw_hint`, `ui_toast`. Palette e costanti in `ui.h`. Il font è unscii generato da `tools/gen_font.py`.

Testo a scorrimento: `ui_scroll_t`. Per un testo che potrebbe non entrare nell'area, invece di troncarlo:

```c
ui_scroll_t sc;
ui_scroll_init(&sc, &gfx_font_small, x, y, w, righe, testo);
for (;;) {
    ui_begin(); ui_scroll_draw(&sc, UI_TEXT); ui_end();
    app_event_t ev;
    if (!app_next_event(ctx, &ev, ui_scroll_wait_ms(&sc))) { ui_scroll_tick(&sc); continue; }
    ...                // ui_scroll_step(&sc) per un passo manuale
}
```

## Rete

```c
esp_err_t app_net_acquire(app_ctx_t *ctx, net_mode_t mode, uint32_t timeout_ms); // con scelta della rete
esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms);   // NET_AUTO | NET_STA | NET_AP | NET_SCAN
void net_release(void);                                        // un lease del task chiamante
void net_get_info(net_info_t *info);                           // up, mode, ssid, ip, rssi, clients, trusted
esp_netif_t *net_sta_netif(void);                              // interfaccia STA: IP, gateway, DNS, MAC
int net_scan(net_ap_t *out, int max);                          // reti visibili, la più forte per prima
const char *net_auth_name(uint8_t auth);                       // "WPA2", "open"...
```

Le richieste sono a conteggio di riferimenti (lease): il WiFi resta acceso finché c'è almeno un lease. Un lease appartiene al task che lo prende, quindi va rilasciato dallo stesso task; quelli che il task dell'app lascia aperti li rilascia il sistema all'uscita. Se l'utente chiede l'uscita mentre `app_net_acquire` attende la connessione, l'attesa si interrompe e la funzione restituisce `ESP_ERR_NOT_FINISHED`. `NET_AUTO` usa una rete nota, altrimenti crea l'hotspot. Con più reti note visibili `app_net_acquire` mostra un menu (RSSI, 6 secondi, poi la più forte) e restituisce `ESP_ERR_NOT_FINISHED` se l'utente esce; `net_acquire` prende sempre la più forte. Le reti aperte non sono supportate. `NET_SCAN` accende la radio in modalità STA senza connettersi: basta per `net_scan()` (circa 2 secondi, al massimo 40 reti). `net_scan()` funziona con un lease `NET_SCAN` o `NET_STA`, non con l'hotspot. Un lease `NET_SCAN` è compatibile con una connessione STA già attiva, non viceversa.

## Server web e QR code

Per le app comandate da una pagina web (componenti `websrv`, `qr`, `webui`, `webtheme` e `webjson`; gli esempi sono `apps/webhid`, `apps/webinjector` e `apps/webfiles`):

```c
esp_err_t websrv_start(const websrv_config_t *cfg); // pin, stack_size, max_frame, on_message, on_auth, on_session_end
esp_err_t websrv_add_page(const char *uri, const char *type, const uint8_t *gz, size_t len); // pagina già in gzip
esp_err_t websrv_add_file(const char *uri, const char *type, const uint8_t *data, size_t len, unsigned flags); // WEBSRV_GZIP, WEBSRV_CACHE
esp_err_t websrv_add_route(const httpd_uri_t *uri);  // rotta propria: non protetta dal PIN
bool websrv_authorized(httpd_req_t *req);           // la richiesta porta il token della sessione?
bool websrv_query(httpd_req_t *req, const char *key, char *out, size_t len); // parametro decodificato
bool websrv_is_client(httpd_req_t *req);             // la richiesta viene dal client autenticato?
void websrv_reply(httpd_req_t *req, const char *msg); // messaggio WebSocket al client
esp_err_t websrv_send(const char *msg);              // al client autenticato, da qualunque task
const char *websrv_client_ip(void);                  // "" se nessuno è collegato
bool websrv_stopping(void);                          // da controllare nei lavori lunghi di on_message
void websrv_stop(void);

bool qr_draw(const char *text, int x, int y, int box);  // tra ui_begin e ui_end
void qr_wifi_text(char *out, size_t len, const char *ssid, const char *pass); // QR per unirsi all'hotspot

void webui_make_pin(const char *pin_key, char *pin, size_t len);      // 6 cifre, o "" (chiave false o rete sicura)
void webui_draw_connecting(const gfx_icon_t *icon, uint32_t accent);  // ricerca della rete
void webui_draw_main(const net_info_t *ni, const char *pin, int page, const char *client, uint32_t accent); // QR, IP, PIN
esp_err_t webtheme_register(void);                   // /theme.css, /theme.js, /grillo.svg, /fonts/*
```

Il server serve al massimo 8 file compresa la pagina, più 8 rotte aggiunte con `websrv_add_route`. `webtheme_register` (dopo `websrv_start`) aggiunge il tema comune: la pagina lo include con `<link rel="stylesheet" href="/theme.css">` e `<script src="/theme.js"></script>`, imposta `--acc` con il colore dell'app e mette nell'intestazione `<img class="logo" src="/grillo.svg">` e il pulsante `<button id="theme" class="chip">` che cambia modalità. Il tema regola solo l'aspetto; la disposizione resta della pagina.

Il server ha due rotte fisse: `/cfg` dice alla pagina se serve il PIN, `/ws` è il WebSocket (handshake rifiutato se l'header `Origin` non è la pagina del dongle). Il PIN si sceglie con `webui_make_pin("<app>.pin", ...)` dopo `app_net_acquire`: è vuoto se la chiave è `false` o se la rete è marcata `trusted` in `config.lua` (`net_info_t.trusted`). Il primo messaggio deve essere `p<PIN>`; le risposte e le regole (blocco per indirizzo, una sessione per volta, subentro dallo stesso IP) sono in [Protocollo WebSocket](protocollo-websocket.md). Dopo l'autenticazione ogni messaggio di testo arriva a `on_message`, nel task del server: un lavoro lungo blocca le altre richieste e deve smettere quando `websrv_stopping()` diventa vero. Una rotta aggiunta con `websrv_add_route` non è protetta: l'handler chiama `websrv_authorized` prima di fare qualcosa. Il token arriva alla pagina dopo il PIN (`T<token>`) e cambia a ogni accesso. Per i messaggi JSON c'è `webjson` (`jb_begin`, `jb_kstr`, `jb_kint`, `jb_kbool`, `jb_end`), che sostituisce i byte non UTF-8 validi: il browser chiuderebbe il WebSocket. `on_session_end` è chiamata quando il client se ne va, quando un altro lo sostituisce e a ogni `websrv_stop`: è il posto dove rilasciare ciò che la sessione teneva (il Web HID lascia i tasti).
