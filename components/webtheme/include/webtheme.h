#pragma once

#include "esp_err.h"

// Tema comune delle pagine web: registra sul server avviato /theme.css,
// /theme.js, /grillo.svg e i font in /fonts/. Da chiamare dopo websrv_start.
// La pagina li include con:
//   <link rel="stylesheet" href="/theme.css"> <script src="/theme.js"></script>
esp_err_t webtheme_register(void);
