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
    usb_profile_t usb;         // profilo USB mentre l'app gira
    uint32_t stack;            // stack del task in byte
    void (*run)(app_ctx_t *ctx);
    void (*subtitle)(char *buf, size_t len);   // facoltativa: riga sotto il nome
};
```

`requires`: `APP_REQ_SD` (SD montata FAT su `/sd`), `APP_REQ_CARD` (scheda presente, anche non FAT), `APP_REQ_NET` (userà il WiFi), `APP_REQ_BLE` (riservato). Se un requisito manca, `appmgr_unavailable()` restituisce il motivo e il launcher non avvia l'app.

Profili USB (`usbdev.h`): `USB_PROFILE_NONE` (TinyUSB spento, torna la USB-Serial-JTAG), `USB_PROFILE_HID` (tastiera + mouse + seriale CDC), `USB_PROFILE_HID_DISK` (tastiera + SD in sola lettura, senza mouse), `USB_PROFILE_MSC` (SD come disco in lettura e scrittura).

## Ciclo di vita

`run(ctx)` gira in un task dedicato. Quando ritorna, l'app è terminata e il sistema rilascia d'ufficio: profilo USB, SD prestata, livello LED dell'app, lease di rete. Un'app può farlo prima per conto suo.

`ctx->arg` è l'argomento di avvio (per l'autorun, il percorso dello script); può essere `NULL`.

## Eventi

```c
bool app_next_event(app_ctx_t *ctx, app_event_t *ev, uint32_t timeout_ms);
bool app_should_exit(app_ctx_t *ctx);
void app_post(app_ctx_t *ctx, app_ev_type_t type, int32_t arg);
void app_cancel_exit(app_ctx_t *ctx);
```

Tipi: `APP_EV_CLICK`, `APP_EV_DOUBLE`, `APP_EV_LONG`, `APP_EV_EXIT` (hold 3 s: terminare al più presto), `APP_EV_USB`, `APP_EV_NET`, `APP_EV_TICK` e `APP_EV_USER` (generati dall'app con `app_post`, utile da timer e task secondari).

`APP_EV_EXIT` è consegnato anche se quando arriva la coda è piena. `app_post(ctx, APP_EV_EXIT, 0)` è anche il modo di chiedere l'uscita dall'interno dell'app (per esempio da un timer di timeout): imposta `exit_requested` e chiude i dialoghi aperti. `app_cancel_exit` annulla una richiesta già ricevuta, quando l'app decide di restare (l'utente risponde "No" a "Uscire?", oppure l'hold ha solo interrotto un lavoro), e scarta gli EXIT ancora in coda.

L'uscita arriva all'app solo quando attende un evento. Se l'app non torna ad attendere entro 3 secondi dalla richiesta, il sistema propone il riavvio con un secondo hold.

## Dialoghi bloccanti

- `app_menu(ctx, titolo, voci, n, selezionata)`: click = giù, doppio = su, lungo = scegli; restituisce l'indice o -1 se l'app deve uscire.
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
void net_release(void);
void net_get_info(net_info_t *info);
esp_netif_t *net_sta_netif(void);                              // interfaccia STA: IP, gateway, DNS, MAC
int net_scan(net_ap_t *out, int max);                          // reti visibili, la più forte per prima
const char *net_auth_name(uint8_t auth);                       // "WPA2", "open"...
```

Le richieste sono a conteggio di riferimenti (lease): il WiFi resta acceso finché c'è almeno un'app che l'ha richiesto. `NET_AUTO` usa una rete nota, altrimenti crea l'hotspot. Con più reti note visibili `app_net_acquire` mostra un menu (RSSI, 6 secondi, poi la più forte) e restituisce `ESP_ERR_NOT_FINISHED` se l'utente esce; `net_acquire` prende sempre la più forte. Le reti aperte non sono supportate. `NET_SCAN` accende la radio in modalità STA senza connettersi: basta per `net_scan()` (circa 2 secondi, al massimo 40 reti). `net_scan()` funziona con un lease `NET_SCAN` o `NET_STA`, non con l'hotspot. Un lease `NET_SCAN` è compatibile con una connessione STA già attiva, non viceversa.
