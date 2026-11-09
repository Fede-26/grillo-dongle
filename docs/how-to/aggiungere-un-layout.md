# Aggiungere un layout

I layout sono in `components/hid/layouts_data.c`, generato da `tools/gen_layouts.py`. Non modificare a mano il file generato.

1. Apri `tools/gen_layouts.py` e aggiungi una tabella come `IT`: quattro righe di tasti fisici, ciascuna con quattro livelli (normale, Shift, AltGr, AltGr+Shift). I tasti sono indicati con l'usage HID della posizione US; `·` = nessun carattere.
2. Aggiungi `("nome", TABELLA)` a `LAYOUTS`.
3. Se il layout ha comportamenti diversi per sistema operativo, aggiungi le fonti:
   - `linux_altgr` (livelli xkb) o `linux_extra` per Linux;
   - `windows_klc`: file KLC di Microsoft Keyboard Layout Creator;
   - `mac_dump`: dump prodotto da `tools/dump_mac_layout.c` su un Mac (vedi `tools/layouts/README.md`).
4. Rigenera: `python3 tools/gen_layouts.py`.
5. Aggiungi test in `test/host/test_main.c` con `expect_key` e verifica: `make -C test/host`.
6. Aggiungi il nome ai selettori di `apps/webhid/web/index.html`.

Annota in `tools/layouts/README.md` la fonte di ogni dato non scritto a mano, con indirizzo e, se è un file scaricato, il suo hash.

Come funzionano tasti morti e livelli: [Layout, sistema operativo e tasti morti](../explanation/layout-e-tasti-morti.md).
