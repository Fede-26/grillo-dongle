#pragma once

#include "esp_err.h"

// Shared theme for the web pages: registers /theme.css, /theme.js,
// /grillo.svg and the fonts in /fonts/ on the running server. Call after
// websrv_start. The page includes them with:
//   <link rel="stylesheet" href="/theme.css"> <script src="/theme.js"></script>
esp_err_t webtheme_register(void);
