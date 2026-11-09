# Digitare una password senza scriverla negli script

Gli script stanno in chiaro sulla microSD e si copiano facilmente. Le password vanno quindi in `config.lua` e si digitano con `kb.type_secret`, che non le restituisce mai allo script.

In `config.lua`:

```lua
secrets = { admin = "la-password" },
```

Nello script:

```lua
kb.type("Administrator\t")
kb.type_secret("admin")
kb.tap("ENTER")
```

Lo script non può leggere il valore: `cfg("secrets.admin")` restituisce sempre `nil`. Un nome inesistente dà l'errore `secret 'nome' not in config.lua`.

## Cosa non protegge

Le password sono comunque in chiaro in `config.lua`, e chiunque abbia la scheda le legge. Sono ammesse solo per credenziali di installazione che accetti di esporre così. Vedi [Sicurezza](../explanation/sicurezza.md).
