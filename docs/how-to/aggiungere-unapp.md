# Aggiungere un'app

Un'app è un componente in `apps/` che esporta un descrittore `app_desc_t`. Il contratto è in `components/appfw/include/app.h`, riassunto in [API C delle app](../reference/api-app.md).

1. Crea `apps/nome/` con `CMakeLists.txt`, `nome.c` e `include/nome.h`. Prendi `apps/usbdisk` come esempio minimo.
2. Dichiara il descrittore: `id` (usato da `autorun.app`), `name`, icona 12x12, colore d'accento, requisiti (`APP_REQ_SD`, `APP_REQ_NET`, …), profilo USB, dimensione dello stack, funzione `run`.
3. Aggiungi l'app a `main/app_list.c` e il componente al `REQUIRES` di `main/CMakeLists.txt`.
4. In `run(ctx)`: disegna con le funzioni di `ui.h`, attendi eventi con `app_next_event`, esci quando `app_should_exit(ctx)` o `APP_EV_EXIT`.

Quando `run` ritorna, il sistema rilascia d'ufficio profilo USB, SD prestata, livello LED e rete: non serve farlo a mano.
