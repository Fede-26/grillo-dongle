# Aggiungere un'app

Un'app è un componente in `apps/` che esporta un descrittore `app_desc_t`. Il contratto è in `components/appfw/include/app.h`, riassunto in [API C delle app](../reference/api-app.md).

1. Crea `apps/nome/` con `CMakeLists.txt`, `nome.c` e `include/nome.h`. Prendi `apps/usbdisk` come esempio minimo.
2. Dichiara il descrittore: `id` (usato da `autorun.app`), `name`, icona 12x12, colore d'accento, requisiti (`APP_REQ_SD`, `APP_REQ_NET`, …), profilo USB, dimensione dello stack, funzione `run`. Il colore d'accento identifica l'app anche sul LED: sceglilo lontano da quelli già usati (injector arancio `0xFF8A20`, Web HID blu `0x4070FF`, Netscan ciano `0x20D0D0`, USB disk magenta `0xE040B0`) e da quelli di sistema (verde Grillo `0x7CFC00` e `UI_OK`, rosso `UI_ERR`, ambra `UI_WARN`).
3. Aggiungi l'app a `main/app_list.c` e il componente al `PRIV_REQUIRES` di `main/CMakeLists.txt`.
4. In `run(ctx)`: disegna con le funzioni di `ui.h`, attendi eventi con `app_next_event`, esci quando `app_should_exit(ctx)` o `APP_EV_EXIT`. Non restare a lungo senza attendere eventi (calcoli lunghi, chiamate di rete senza scadenza): l'uscita arriva solo lì, e altrimenti all'utente resta il riavvio. Per la rete usa `app_net_acquire`, che lascia scegliere la rete quando ce n'è più d'una.

Il profilo USB è la combinazione di interfacce che serve all'app (vedi [API C delle app](../reference/api-app.md#descrittore)): se nessuno dei profili pronti va bene, componi la maschera `USB_F_*`. Per cambiarlo a metà usa `app_usb_apply`, che sposta anche la SD.

Per un'app comandata da una pagina web usa i componenti `websrv` (server, PIN, una sessione per volta) e `qr` (QR code sul display, anche per le credenziali dell'hotspot), `webui` (la schermata con QR, indirizzo e PIN) e `webtheme` (tema comune delle pagine): `apps/webhid` e `apps/webinjector` sono gli esempi. La pagina va compressa in fase di build come in `apps/webinjector/CMakeLists.txt`, con un nome di file diverso da quello delle altre app (il simbolo incorporato deriva dal nome), e registrata con `websrv_add_page`; poi `webtheme_register()`.

Quando `run` ritorna, il sistema rilascia d'ufficio profilo USB, SD prestata, livello LED e rete: non serve farlo a mano.
