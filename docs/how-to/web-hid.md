# Usare il Web HID

Il Web HID fa del dongle una tastiera e un touchpad comandati da una pagina web, dal telefono o da un altro computer.

## Avviare

1. Collega il dongle al PC da comandare.
2. Nel launcher scegli **Web HID**. Il dongle si collega al WiFi (o crea l'hotspot): vedi [Collegare il dongle al WiFi](wifi.md).
3. Sul display compaiono un QR, il nome della rete, l'indirizzo IP e un PIN di 6 cifre.
4. Sul telefono o sul computer, collegati alla stessa rete (o all'hotspot `Grillo-XXXX`) e apri l'indirizzo, oppure inquadra il QR. Un click sul pulsante alterna QR e testo dell'indirizzo.
5. Inserisci il PIN.

Tieni premuto il pulsante 3 secondi per uscire.

## La pagina

Due schede, **Touchpad** e **Keys**.

- **Touchpad**: un dito muove il mouse, un tocco breve clicca, due dita scorrono (rotella), un tocco breve a due dita è il click destro. Sotto, i tre pulsanti Left, Mid, Right si possono tenere premuti (per trascinare).
- **Keys**:
  - in alto i selettori di layout e sistema operativo: dicono al dongle com'è configurato il PC; con OS = macOS i modificatori compaiono come Ctrl, Opt, Shift, Cmd;
  - la casella "Type here" è il live typing: ciò che scrivi arriva al PC subito, compresa la cancellazione;
  - i modificatori (Ctrl, Alt/Opt, Shift, Win/Cmd) si attivano e valgono per il tasto successivo;
  - tasti speciali (Esc, Tab, Enter, Backspace, frecce, …) e il pannello F1–F12;
  - "Paste & type" apre un'area in cui incollare un testo lungo da digitare in un colpo solo.

Se il PC non risponde, la pagina riceve `nohost`: controlla il cavo e che il PC abbia riconosciuto la tastiera.

## Senza PIN

Su una rete fidata si può disattivare il PIN: in `config.lua`, `webhid = { pin = false }`. Chiunque sulla stessa rete potrebbe allora comandare il PC. Vedi [Sicurezza](../explanation/sicurezza.md).

Dopo 5 PIN errati gli accessi sono bloccati per 30 secondi. Un solo client alla volta.

Il protocollo è descritto in [Protocollo WebSocket](../reference/protocollo-websocket.md).
