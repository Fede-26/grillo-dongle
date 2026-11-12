[English](../../how-to/flash-a-release.md) · **Italiano**

# Flashare una release senza compilare

Le [release](../../../.github/workflows/release.yml) pubblicate su GitHub contengono `grillo-vX.Y.Z-merged.bin`: bootloader, tabella delle partizioni e app già uniti in un'unica immagine, pronta da scrivere a partire dall'indirizzo 0x0. Non serve ESP-IDF, solo `esptool`.

```sh
pip install esptool
```

Collega il dongle tenendo premuto il pulsante (download mode), poi trova la porta e flasha:

```sh
esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX write_flash 0x0 grillo-vX.Y.Z-merged.bin
```

Scollega e ricollega il dongle senza premere il pulsante: sul display compare il launcher.

Per preparare la microSD, continua da [Prima installazione](../tutorials/01-prima-installazione.md#2-prepara-la-microsd).

Le immagini di release compilano con `sdkconfig.release`: niente [console di debug](../reference/console-debug.md) sulla porta seriale (vedi [Sicurezza](../explanation/sicurezza.md)). Per sviluppare o avere la console, compila da sorgente come nel [README](../../../README.it.md#avvio-rapido).

Per aggiornare un dongle già flashato: scollegalo, ricollegalo tenendo premuto il pulsante (download mode) e ripeti il comando `write_flash`.
