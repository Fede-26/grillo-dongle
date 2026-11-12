**English** · [Italiano](../it/how-to/netscan.md)

# Use Netscan

Netscan is the app for figuring out what network you are facing. It has four screens, chosen from the start menu. Gestures: click = down, double click = up, long press = select, held for 3 seconds = exit the app. In every list the `Back` row closes the screen (from the first row, a double click jumps to it, at the bottom). A selected row too long for the display, for example an IP with the host name next to it, scrolls by itself after one second.

It does not do port scans. Traffic it sends: ARP requests to the /24, NBNS (UDP 137) and mDNS (UDP 5353) queries to the hosts found to learn their names, a ping to the gateway, a DNS query, an HTTP request, a broadcast DHCPDISCOVER (never followed by a request: no address is taken) and time synchronization via NTP (done on every connection to a network, by any app).

## WiFi networks

Looks for visible networks, strongest first, with channel and RSSI (dBm: closer to 0 = stronger). A long press on a network shows SSID, BSSID, manufacturer vendor (if `oui.bin` is present), channel, signal and security type. `Rescan` repeats the scan.

Useful for choosing a free channel or checking that the customer's access point covers the area. It does not require a known network.

## Network info

Connects to one of the `wifi` networks in `config.lua` (with several networks visible, choose from the menu: see [Connect the dongle to WiFi](wifi.md)) and checks in sequence:

| Row | Meaning |
| --- | --- |
| `WiFi` | Network and signal. |
| `IP` | Address and prefix assigned by DHCP. |
| `GW` | Last octets of the gateway and ping response time. A yellow "no reply" is not an error: many routers do not answer pings. |
| `DNS` | DNS server and time to resolve a name. |
| `Web` | `Online`: internet reachable. `Captive portal`: the network intercepts the request and serves a login page (response 200 or 3xx). `HTTP nnn`: another unexpected response. `No internet`: no response within 4 seconds, or the DNS query failed (in that case the `DNS` row is red too). |

The web check is a `GET /generate_204` to `connectivitycheck.gstatic.com` (from memory: the Google endpoint that answers 204 with no body). To use another host, set `netscan.probe_host`. Click = repeat, double click = back to the menu.

## LAN hosts

Connects as above and looks for the hosts on the network: one ARP request for each address of the dongle's /24 (or of the network, if smaller), then the name and manufacturer of those found. It takes about 10 seconds. Each row shows the IP and, on the right, the name or the manufacturer; a long press opens the detail with the MAC.

Names come from NBNS (UDP port 137, Windows machines answer) and from mDNS (port 5353, Macs, Linux and many printers answer). A host that answers neither stays without a name.

Limits to be aware of:

- An access point with *client isolation* hides hosts from other WiFi clients: the list will be almost empty. This cannot be worked around from the dongle.
- A device in power saving mode may not answer the ARP request and not show up. Repeat the scan.
- A MAC with the second bit of the first byte set to 1 is "private" (random, typical of recent phones and PCs): the manufacturer cannot be derived and the row shows `(private MAC)`.

## DHCP servers

Connects as above, asks all DHCP servers on the network for an address (a broadcast DHCPDISCOVER, sent three times) and collects the offers for 5 seconds. Each row is a server: IP and, on the right, the manufacturer. A long press opens the detail with MAC, manufacturer, offered address with prefix, gateway, DNS and lease duration.

The dongle accepts no offer: without the following request (DHCPREQUEST) the server does not reserve the address, and the dongle keeps the one it already has.

| Servers found | Meaning |
| --- | --- |
| 1 | Normal. Green LED. |
| 0 | Nobody answered: DHCP is off, or the access point blocks broadcasts. Red toast and LED. |
| more than 1 | There is an unauthorized (*rogue*) DHCP server: a router plugged in on the wrong port, a PC with connection sharing, an attack. Clients take the address from whoever answers first, so some end up on a wrong network. Toast "More than one DHCP server!" and red LED. The MAC and the manufacturer help find the device. |

The MAC comes from an ARP request to the server's IP. It is missing if the server is on another network (it answers through a relay) or does not answer ARP.

## Saving

Every WiFi, LAN and DHCP scan is saved to `/scans/` on the microSD, as CSV (`wifi-…csv`, `lan-<network>-…csv`, `dhcp-<network>-…csv`). Read them with the USB disk app. The dongle has no battery-backed clock: the time comes from NTP as soon as the dongle connects to a network (or from `host.clock()` in a script) and the file name contains the date; without the time the name has a sequence number.

In the CSV, values that would start with `=`, `+`, `-` or `@` (not numbers) get a leading apostrophe: an SSID or a host name chosen by others must not become a formula when you open the file with a spreadsheet.

## Show the manufacturer: `oui.bin`

Without the file, Netscan shows only the MAC. For manufacturers:

1. Manually download the IEEE list, `oui.csv`, from <https://standards-oui.ieee.org/oui/oui.csv> (or `oui.txt` from the same folder).
2. Convert it: `tools/make_oui.py oui.csv oui.bin`.
3. Copy `oui.bin` to the root of the microSD.

The format allows binary search directly on the card, without loading it into RAM.

## `config.lua` keys

See the [reference](../reference/config-lua.md): `timezone`, `ntp`, `netscan.probe_host`.
