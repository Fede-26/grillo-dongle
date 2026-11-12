**English** · [Italiano](../it/explanation/sicurezza.md)

# Security and accepted risks

Grillo is a personal tool for installing and configuring PCs. A device that types on the computer it is plugged into is, by design, powerful: this page states where the boundaries are and which risks were accepted on purpose.

## What the dongle can do

Once plugged in, the PC treats it as a trusted keyboard and mouse. It can perform any action a user can perform with the keyboard, and there is no confirmation on the PC. Use it only on computers you have the right to configure.

## Accepted risks

- **Plaintext passwords on the microSD.** `secrets` in `config.lua` is not encrypted. `kb.type_secret` only prevents a script from reading them as a string. Whoever has the card can read them. Use it for installation credentials you accept to expose this way.
- **Web HID without a PIN.** `webhid.pin = false` lets anyone on the same network (or on the hotspot) control the PC. The PIN, in any case, has 6 digits, is shown on the display, and locks for 30 seconds after 5 failures from the same IP address (the lock is per address: another host cannot lock out the owner by failing on purpose). The WebSocket channel is not encrypted (no HTTPS): anyone watching the local network sees the commands and the PIN. That is why access is meant for trusted networks.
- **Hotspot with the default password.** If you do not set `hotspot.pass`, it is `grillo-dongle`, known to anyone who reads this documentation.
- **Web Injector without a PIN.** `webinjector.pin = false` lets anyone on the same network run the microSD scripts on the connected PC. With the PIN, the same rules as the Web HID apply (per-address lock, a single client, no HTTPS). The page only selects scripts already on the SD: absolute paths and `..` are rejected, and scripts cannot be uploaded or modified from the network.
- **Web Files sees the whole microSD.** With the PIN, whoever is on the page can read, modify and delete any file, logs and `config.lua` included; without a PIN (`webfiles.pin = false`) anyone on the same network can. The `config.lua` passwords (`pass`, `password` and the `secrets` table) reach the page hidden behind placeholders, and only the **Show secrets** button sends them in plaintext: the channel is not encrypted, so anyone watching the network would see them. Downloads, uploads and saves go through HTTP routes that require the token issued after the PIN: the client's IP address would not be enough, because behind a NAT several devices share it. Paths with `\`, `:` or a name ending in `.` or a space (`..` included) are rejected. A `config.lua` saved with an error leaves the dongle with the default values (no known networks, hotspot with the default password) until it is fixed; the page checks the syntax before saving.
- **Trusted networks without a PIN.** A network with `trusted = true` in `config.lua` (or the hotspot with `hotspot.trusted = true`) makes all web apps skip the PIN: anyone on that network has the same powers as someone with the PIN. The choice is based on the network name: an access point that presents itself with the same SSID and the same password is indistinguishable. The dongle only connects to WPA2/WPA3-Personal networks, so the password is needed anyway.
- **Origin check on the WebSocket.** WebSockets do not follow the browser's same-origin policy: without a check, any page open in a browser on the same network could connect to `ws://<dongle IP>/ws` and, without a PIN, act. `websrv` rejects the handshake if the `Origin` header is not `http://<dongle IP>`. The comparison is against the socket's local address and not against the `Host` header, which in a DNS rebinding attack is the hostile site's. A client that does not send `Origin` (a script, not a browser) is accepted: whoever uses it is already on the network and does not need a bridge site.
- **A single client** at a time on the Web HID: whoever connects first keeps it. Another address receives `busy` even before the PIN check, so PINs cannot be tried during a session. The same address with the right PIN takes over the session (needed for a phone waking from standby): someone who shares the client's IP, for example behind the same NAT, and knows the PIN can take its place.
- **Debug console active in development builds.** With `CONFIG_GRILLO_USB_DEBUG_CDC=y` (the default) the CDC serial port accepts the [console](../reference/debug-console.md) commands, `cat` and `ls` included. Whoever controls the PC the dongle is plugged into can therefore read `config.lua`, `secrets` passwords included. The releases published on GitHub (`.github/workflows/release.yml`) build with `sdkconfig.release`, which disables the option: whoever flashes a release has no console.

## What limits a Lua script

The injector VM has no direct filesystem access (`io`, `os`, `dofile`, `loadfile` are absent), reads only `/payloads` and `/data` with `fs.*`, has a 96 KB memory limit, rejects precompiled bytecode and stops on a 3-second press. It does not limit what the script types on the PC.

## Netscan

Netscan sends traffic on the network it is connected to: ARP requests to every address of the /24, NBNS (UDP 137) and mDNS (UDP 5353) queries to the hosts found, a ping to the gateway, a DNS query and a `GET /generate_204` to `connectivitycheck.gstatic.com` (changeable with `netscan.probe_host`). On other people's networks, use it with their consent: the sweep can show up in the logs of a firewall or an intrusion detection system. It does not do port scans.

SSIDs and host names are chosen by whoever is on the network: on the display they are sanitized (printable ASCII only), and in the CSVs in `/scans/` non-numeric values starting with `=`, `+`, `-` or `@` get a leading apostrophe, so they do not become formulas when opened with a spreadsheet. The CSVs contain the MACs and host names of the scanned network: treat them as customer data.

## The return channel

The `host` channel lets the PC reply to the dongle over the CDC serial port. It is limited on purpose: the dongle accepts a single line format (`@G<nonce>:<code>`), with the nonce the script was waiting for, and discards everything else; the script receives only a numeric exit code. The script can receive a small inventory value (`host.get`, `host.log`) or a line typed by the user on the PC keyboard (`host.input`): at most 256 bytes, with everything that is not a letter (accented ones included, in UTF-8), a digit, `. _ : -` or a space replaced by `_`, so the value carries no quotes, `;` or `$` if it is retyped into a shell or written to a CSV. The value stays in the script's VM, which has no network, and in the logs in `/logs/host/`: the firmware builds the file name from a sanitized id, with a cap of 64 files of 256 KB each. There is no way to download files or the full output of a command. The command that writes the reply is typed by the script, so it depends on what the script already does on the PC: the channel adds no permissions. Before the channel, `host.shell` (and `pcdisk.run`) starts `powershell -NoProfile` or `exec sh`: with no profile or rc files, user-defined aliases and functions do not change the commands typed by the dongle.

## Reporting a problem

The repository is private. Report problems directly to whoever maintains it.
