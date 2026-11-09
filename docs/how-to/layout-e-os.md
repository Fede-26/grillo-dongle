# Scegliere layout e sistema operativo

Il dongle invia posizioni di tasti, non caratteri: il PC li interpreta con il proprio layout. Per digitare `@` o `è` giusti, il dongle deve sapere quale layout e quale sistema operativo ha il PC.

## Per uno script

Nell'intestazione:

```lua
--- @layout it
--- @os windows
```

Oppure nel codice, per cambiare a metà script: `kb.layout("us")`, `kb.os("mac")`.

## Per tutti gli script

Default in `config.lua`:

```lua
keyboard = { layout = "it", os = "windows" },
```

## Per il Web HID

Con i due selettori della pagina.

## Quale scegliere

| Il PC ha | Layout | OS |
| --- | --- | --- |
| Tastiera italiana (Windows, Linux) | `it` | `windows` o `linux` |
| Tastiera italiana su Mac ("Italiano - Pro") | `it` | `mac` |
| Tastiera americana | `us` | qualunque |
| Layout Colemak | `colemak` | quello del PC |

L'OS conta anche con lo stesso layout: cambiano i livelli AltGr/Option, i tasti morti e i ripieghi. Per i dettagli vedi [Layout, sistema operativo e tasti morti](../explanation/layout-e-tasti-morti.md).

## Se un carattere non esce

- `U+XXXX not typeable with layout NOME`: il carattere non esiste nel layout e non c'è un ripiego per quel sistema operativo. Su macOS non ci sono ripieghi.
- Un carattere esce al posto di un altro (es. `<` e `\` scambiati su Mac): il Mac ha riconosciuto il dongle con un tipo di tastiera diverso da quello previsto. Vedi `tools/layouts/README.md` (opzione `mac_iso`).
