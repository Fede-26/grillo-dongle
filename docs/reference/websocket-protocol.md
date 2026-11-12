**English** · [Italiano](../it/reference/protocollo-websocket.md)

# Web app WebSocket protocol

Web HID, Web Injector and Web Files serve a page and a WebSocket channel that carries the commands. The server is the `websrv` component (on top of `esp_http_server`); only one client at a time. The `/cfg` and `/ws` routes, login with `p<PIN>` and the `ok`, `no`, `locked` and `busy` replies belong to the component and apply to every web app; the commands belong to each app (Web HID below, then [Web Injector](#web-injector) and [Web Files](#web-files)).

## Endpoints

| Path | Method | Response |
| --- | --- | --- |
| `/` | GET | The page (gzip-compressed HTML, `Cache-Control: no-store`). |
| `/cfg` | GET | `pin` if the PIN is required, `nopin` otherwise (key `<app>.pin = false` or network marked `trusted`). The page uses it to decide whether to show the PIN field. |
| `/ws` | GET (WebSocket upgrade) | Command channel. The handshake is rejected (connection closed without a response) if the `Origin` header is present and is not `http://<dongle IP>` (with or without `:80`). Without `Origin`, as from a non-browser client, it is accepted. |

## Messages

Each message is a text frame: a type character followed by the arguments. Commands from an unauthenticated client are silently ignored; login (`p`) is accepted at any time.

### Client → dongle

| Message | Meaning |
| --- | --- |
| `p<PIN>` | Login. With the PIN disabled the argument is ignored (`p`). |
| `m dx dy` | Moves the mouse (relative). Each value is clamped to ±2000. |
| `b mask` | Mouse button state (bit 0 left, 1 right, 2 middle). |
| `w dy [dx]` | Wheel. Each value is clamped to ±127. |
| `t text` | Types the text with the current layout and OS. A character the layout does not have becomes `?` (see `untypeable`). |
| `c mods char` | Key combination: HID modifier mask plus the key that produces the character (e.g. Ctrl+C). |
| `k mods usage` | Key combination with a raw HID usage: special keys, F-keys. `usage` 0 = modifiers only. |
| `e n` | `n` Backspaces in a row (1..500; outside this range the command is ignored). Used by live typing, which splits longer deletions. |
| `l name` | PC layout: `it`, `us`, `colemak`. |
| `o name` | PC OS: `windows`, `linux`, `mac`. |

Frames are limited in size; an empty or too long frame closes the channel.

### Dongle → client

| Message | Meaning |
| --- | --- |
| `ok` | Login succeeded. If another session from the same IP address was still open, the dongle closes it and replaces it. |
| `T<token>` | Right after `ok`: session token for the HTTP routes of the apps that have them (`X-Grillo-Token` header or `t` parameter). It changes at every login; pages that do not use it ignore it. |
| `no` | Wrong PIN. |
| `locked` | Too many attempts from this address: locked for 30 seconds after 5 wrong PINs. |
| `busy` | A client with another address is already connected. It arrives before the PIN check: PINs cannot be tried during a session. |
| `L<layout> <os>` | Current layout and OS, sent after login to align the selectors (e.g. `Lcolemak mac`). |
| `nohost` | The PC does not respond to the USB keyboard. |
| `hostok` | The PC responds again, after a `nohost`. |
| `untypeable n` | `n` characters of the last `t` were not in the layout and were typed as `?`. |

## Notes

- The PIN is 6 digits, generated at every app start and shown on the display; there is none on networks marked `trusted` in `config.lua`. The client's IP address is shown on the display (abbreviated).
- The channel is not encrypted: protection lies in the PIN and in the chosen network. See [Security](../explanation/security.md).

## Web Injector

Same routes, same login and same `ok`, `no`, `locked`, `busy` replies. Text commands arrive from the client (one character plus the arguments); the dongle replies with JSON objects, one message per object, with the type in the `t` field. Paths are relative to `/payloads`: absolute paths and `..` receive `{"t":"error","text":"Bad path"}`.

### Client → dongle

| Message | Meaning |
| --- | --- |
| `l<folder>` | Listing of a folder (`l` alone = `/payloads`): one `entry` per item, then `end`. |
| `s` | All scripts (up to 3 levels), for the by-OS and by-tag views: `entry` with `path`, then `end`. |
| `m<script>` | Script card: `meta`. |
| `r<script>` | Runs the script. With another script running: `error`. |
| `a<id>:<n>` | Answer to dialog `id`: item index (`menu`), 0/1 for no/yes (`confirm`), 0/1/2 for click/double/long (`button`), 0 for the outcome (`done`). An `id` that is not the open dialog's is ignored: the dialog has already been closed by the button. |
| `x` | Stops the running script (like a 3-second press). |

### Dongle → client

| `t` | Fields | Meaning |
| --- | --- | --- |
| `hello` | `phase`: `idle`, `run`, `done`; `dropped` (history entries discarded) | After every login. Followed by the history of the current or last run (`log`), the last `state` with a script running, the open dialog and the outcome (`result`). |
| `entry` | `name`, `title`, `dir`; for scripts `layout`, `os`, `tags` (effective); with `s` also `path` | One listing item. |
| `end` | `scan`, `full` (listing truncated at 48 items) | End of the listing. |
| `meta` | `path`, `name`, `desc`, `layout`, `os`, `tags`, `timeout` (-1 if absent), `error` | Script header. |
| `state` | `name`, `status`, `p`, `max` (0 = no bar), `wait` (waiting for the USB host) | Script state changed. |
| `log` | `k`, `at`, `text` | History entry. `k`: `start` (script name), `log` (`sys.log` line), `status` (new status), `ask` (dialog opened), `answer` (answer, with "(from the page)" if it came from the page), `done` (outcome). `at`: dongle time (`HH:MM:SS`) or, without a valid time, time since boot (`+m:ss`). Text cut at 200 bytes; the dongle keeps the last 100 entries. |
| `menu` | `id`, `title`, `items` | `ui.menu` open. |
| `confirm` | `id`, `title`, `text`, `yes` (default answer) | `ui.confirm` open. |
| `button` | `id` | `ui.wait_button` waiting. |
| `done` | `id`, `name`, `path`, `ok`, `aborted`, `timeout`, `error` | Outcome: stays open on the display until someone answers. `path` is the script, to run it again. |
| `result` | like `done`, without `id` | Outcome of the last run, resent at every login until another one starts. |
| `close` | `id` | Dialog closed, wherever the answer came from. |
| `idle` | | Outcome closed: another script can be started. |
| `error` | `text` | Command rejected. |

Texts that are not valid UTF-8 (file names, script texts) arrive with `?` in place of the invalid bytes: the browser would close the channel.

## Web Files

Same routes, same login and same replies. Paths are relative to the microSD root and start with `/` (`/`, `/payloads/a.lua`); paths without a leading `/`, with `\` or `:`, or with a name ending in `.` or a space (`..` included: FatFs strips trailing dots and spaces) receive `{"t":"error","text":"Bad path"}`. The trash (`/.trash`, in any combination of upper and lower case: FAT does not distinguish them) is handled only with the trash commands.

### Client → dongle

| Message | Meaning |
| --- | --- |
| `l<folder>` | Listing: one `entry` per item (at most 1000), then `end`. |
| `f` | Card space: `space`. |
| `k<folder>` | New folder. |
| `n<file>` | New empty file. |
| `v<from>\t<to>` | Rename or move (folders too). The destination must not exist. |
| `c<from>\t<to>` | Copy a file. |
| `d<path>` | Move to the trash. |
| `D<path>` | Delete permanently, folders included with their contents. |
| `L` | Trash listing: one `trash` per item, then `end`. |
| `R<id>` | Restore trash item `id`. |
| `E<id>` | Permanently delete trash item `id`. |
| `X` | Empty the trash. |

Each command has a single final reply (`end`, `space`, `ok` or `error`) and the dongle runs them in order of arrival: the page matches the replies to the queued commands.

### Dongle → client

| `t` | Fields | Meaning |
| --- | --- | --- |
| `entry` | `name`, `dir`, `size` (bytes), `mtime` (seconds since 1970, UTC) | One folder item. |
| `trash` | `id`, `origin` (original path), `date` (empty if the time was not known), `dir`, `size` | One trash item. |
| `end` | `full` (listing truncated) | End of the listing. |
| `space` | `total`, `free` (bytes) | Card space. |
| `ok` | | Command done. |
| `error` | `text` | Command rejected or failed. |

### HTTP routes

They require the token from the `T<token>` message, in the `X-Grillo-Token` header or in the `t` parameter; without it, `403`. The path is in the `path` parameter, encoded as a query string.

| Route | Method | Meaning |
| --- | --- | --- |
| `/api/file?path=&reveal=` | GET | Text of a file for the editor, up to 64 KB (`413` beyond, `404` if missing or a folder). `/config.lua` arrives with the secrets replaced by `"<secret:N>"`, in clear only with `reveal=1`. |
| `/api/file?path=&overwrite=` | PUT | The body is the file contents (save and upload, of any size). If the file exists `overwrite=1` is required, otherwise `409`. For `/config.lua` the `"<secret:N>"` placeholders go back to the values of the current file; an N the file does not have receives `400` and the file does not change. Response `ok`. |
| `/api/download?path=&t=` | GET | The file as an attachment (`Content-Disposition` with the name in UTF-8, RFC 5987), with `Content-Length`: the browser shows total and remaining time. If reading is interrupted the connection is closed before the end, and the browser marks the download as failed. |
| `/api/check?path=` | POST | The body is the text of a script (up to 64 KB), even unsaved: it compiles it without running it. Response `{"t":"check","ok":...,"error":...,"line":...}`; for scripts in `/payloads` (excluding `lib`) also `header` with the header error, empty if it is valid. |
