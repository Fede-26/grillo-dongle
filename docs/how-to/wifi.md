**English** · [Italiano](../it/how-to/wifi.md)

# Connect the dongle to WiFi

The Web HID and Netscan apps use WiFi (Netscan: Network info and LAN hosts; scanning for networks does not require a known network).

## Add a known network

In `/config.lua` on the microSD:

```lua
wifi = {
  { ssid = "Office", pass = "password" },
  { ssid = "Lab", pass = "other-password" },
},
```

When the app starts, the dongle scans for networks and checks which known networks are visible:

- only one: it connects to that one;
- more than one: the display shows the **WiFi** menu with the visible known networks and their signal (RSSI in dBm, strongest at the top). Choose with click and long press. If you press nothing within 6 seconds (the countdown is in the title) it connects to the strongest; a click stops the countdown. Holding for 3 seconds exits the app without connecting.

The order of the list in `config.lua` does not matter. Only WPA2/WPA3-Personal networks are supported: open networks are not, and an entry without `pass` is ignored (with a warning in the log).

## If no known network is reachable

The dongle creates its own hotspot, `Grillo-XXXX` (XXXX = last digits of the MAC), with password `grillo-dongle`. To change name and password:

```lua
hotspot = { ssid = "MyGrillo", pass = "changeme123" },
```

The password must be at least 8 characters long, otherwise the default one applies.

## Trusted networks: no PIN

The web apps (Web HID, Web Injector, Web Files) ask for a 6-digit PIN shown on the display. On a network you trust you can skip it: add `trusted = true` to the network's entry.

```lua
wifi = {
  { ssid = "Home", pass = "password", trusted = true }, -- no PIN
  { ssid = "Office", pass = "other-password" },         -- PIN required
},
hotspot = { pass = "changeme123", trusted = true },     -- no PIN on the hotspot
```

The PIN is required if the app's `<app>.pin` key is `true` (the default) **and** the current network is not marked `trusted`. With `<app>.pin = false` that app asks for no PIN on any network. On the display, in place of the PIN, `trusted` appears (in green) if the network decides it, `none` (in yellow) if the app's key decides it.

Without a PIN anyone on the same network can use the page: control the PC's keyboard and mouse, start scripts, read and modify the microSD. Mark as trusted only networks where every device is yours. For the hotspot this only makes sense with your own password, not the default one. See [Security](../explanation/security.md).

## Check

The app's display shows the IP address and a WiFi indicator (`AP` if you are on the hotspot). With a hotspot the address is always the dongle's address on the hotspot; on a known network it is the one assigned by the router.

The connection is kept with power saving turned off: the web page responds faster, at the cost of higher power consumption.
