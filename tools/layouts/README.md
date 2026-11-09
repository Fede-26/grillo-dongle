# Dati dei layout da sistemi operativi

Questi file sono la fonte da cui `tools/gen_layouts.py` ricava i livelli
AltGr/Option e i tasti morti del Colemak. Non vanno modificati a mano.

- `colemak-windows.klc`: sorgente del layout "Colemak" 1.1 per Windows
  (2007), file `src/Colemak.klc` di
  <https://colemak.com/pub/windows/Colemak-1.1-Caps-Lock-Unchanged.zip>
  (SHA256 dello zip `65a082aa58b3910b2584e056aef818fe6dd40de5dc3b085de1ecbd5a2f45ea8d`,
  uguale a quello della pagina <https://colemak.com/Windows>). Pubblico dominio.
  Formato di Microsoft Keyboard Layout Creator, UTF-16.
- `macos_colemak.tsv`: prodotto da `tools/dump_mac_layout.c`, che interroga il
  layout "Colemak" di macOS con `UCKeyTranslate`. Da rigenerare su un Mac
  (comando nell'intestazione del file C) se Apple cambia il layout.
- Linux: la mappa è scritta in `gen_layouts.py` (`linux_altgr`), copiata da
  xkeyboard-config, `symbols/us`, sezione `colemak_base`.
