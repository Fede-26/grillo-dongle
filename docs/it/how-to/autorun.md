[English](../../how-to/autorun.md) · **Italiano**

# Avviare uno script all'accensione

Con l'autorun il dongle avvia un'app (e uno script) da solo, dopo un conto alla rovescia che puoi annullare. Serve ai kiosk: inserisci il dongle e parte.

In `config.lua`:

```lua
autorun = { app = "injector", payload = "kiosk/setup.lua", delay = 3 },
```

- `app`: identificatore dell'app (`injector`, `webhid`, `usbdisk`).
- `payload`: percorso dello script rispetto a `/payloads`, per l'injector.
- `delay`: secondi di attesa, default 3.

All'avvio il display mostra il nome dell'app, lo script e una barra: un click annulla e lascia il launcher. Alla scadenza lo script parte senza la scheda di anteprima.

Con un identificatore sconosciuto il launcher mostra `autorun: unknown app` e parte normalmente: controlla `python3 tools/grillo.py cmd apps` per i nomi validi.
