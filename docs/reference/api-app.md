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
```

Tipi: `APP_EV_CLICK`, `APP_EV_DOUBLE`, `APP_EV_LONG`, `APP_EV_EXIT` (hold 3 s: terminare al più presto, sempre consegnato), `APP_EV_USB`, `APP_EV_NET`, `APP_EV_TICK` e `APP_EV_USER` (generati dall'app con `app_post`, utile da timer e task secondari).

## Dialoghi bloccanti

- `app_menu(ctx, titolo, voci, n, selezionata)`: click = giù, doppio = su, lungo = scegli; restituisce l'indice o -1 se l'app deve uscire.
- `app_menu_ex(...)`: come sopra, con una colonna di testo a destra di ogni riga.
- `app_confirm(ctx, titolo, testo, predefinito_sì)`.
- `app_message(ctx, titolo, testo, colore)`: attende click o lungo.

## Disegno

Lo schermo è 160x80. Una barra di stato di 12 pixel più una linea d'accento appartengono al sistema; l'area sotto (da `UI_CONTENT_Y`) è dell'app.

```c
ui_begin();            // prende il lock, pulisce l'area contenuto
...                    // gfx_* e ui_draw_*
ui_end();              // ridisegna la barra, invia il frame
```

Primitive: `gfx_text`, `gfx_text_aligned`, `gfx_text_scaled`, `gfx_text_fit`; `ui_draw_row`, `ui_draw_list`, `ui_draw_progress`, `ui_draw_pager`, `ui_draw_wrapped`, `ui_draw_hint`, `ui_toast`. Palette e costanti in `ui.h`. Il font è unscii generato da `tools/gen_font.py`.

## Rete

```c
esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms);   // NET_AUTO | NET_STA | NET_AP
void net_release(void);
void net_get_info(net_info_t *info);
```

Le richieste sono a conteggio di riferimenti (lease): il WiFi resta acceso finché c'è almeno un'app che l'ha richiesto. `NET_AUTO` sceglie la migliore rete nota, altrimenti crea l'hotspot.
