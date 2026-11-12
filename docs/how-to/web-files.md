**English** · [Italiano](../it/how-to/web-files.md)

# Use Web Files

Web Files opens the dongle's microSD in a web page, from a phone or a computer: browse folders, upload and download files, rename and move them, delete them (with a trash) and edit text files, with an editor for Lua scripts and a table editor for CSVs. The page sees the whole card, `config.lua` and the logs included.

## Start

1. Power the dongle: Web Files does not use USB, any outlet will do (see [Android phone](#android-phone) for connecting it to a phone).
2. In the launcher choose **Web Files**. The dongle connects to WiFi (or creates the hotspot): see [Connect the dongle to WiFi](wifi.md).
3. The display shows a QR code, the network name, the IP address and a 6-digit PIN, as in the [Web HID](web-hid.md). With the hotspot it starts with the WiFi credentials QR and, as soon as a device connects, the display switches by itself to the address QR; a click toggles between the two.
4. Open the address (or scan the QR) and enter the PIN.

Hold the button for 3 seconds to exit.

## Browse

At the top is the path (**SD / payloads / lib**): each part leads back to that folder. The list sorts by name, size or date; **Hidden** also shows hidden files (those starting with `.`, such as the `._*` files macOS leaves on the card, and `System Volume Information`). On the right, the card's free space.

Tapping a folder opens it; tapping a text file (`.lua`, `.csv`, `.txt`, `.md`, `.json`, `.log` and similar) opens it in the editor. The **⋯** button on each row shows the actions:

- **Edit**: only for text files up to 64 KB.
- **Download**: downloads the file.
- **Rename / move**: the full path is editable, so the same field serves to rename and to move (`/payloads/a.lua` → `/payloads/old/a.lua`). The destination folder must exist. You can also change only the letter case (`readme.txt` → `README.txt`).
- **Copy**: files only.
- **Move to trash**: see [Trash](#trash).
- **Delete permanently**: after a confirmation; a folder is deleted with all its contents.

**+ Folder** and **+ File** create a folder or an empty file (the text file opens right away in the editor).

## Upload files

**Upload** picks one or more files, which go into the open folder; from a computer you can also drag them onto the page. The bar shows the progress. If a file with the same name already exists, the page asks whether to replace it. The file first lands in `<name>.part` and takes its real name only at the end: if the connection drops halfway, the previous file stays intact. If the final rename itself fails, the new content stays in `<name>.part`.

The speed measured on the device is about 90 KB/s upload and 130 KB/s download (1.5 MB file, WiFi at −50 dBm): fine for scripts, CSVs and logs, slow for large files. To copy a lot of data use [USB disk](copy-files.md).

## Editor

The editor opens text files up to 64 KB.

- **Save** (or Ctrl+S, Cmd+S on the Mac) saves; a dot next to the name marks unsaved changes, and closing the page or the editor with changes asks for confirmation.
- **Find** (or Ctrl+F, Cmd+F) searches case-insensitively; Enter moves to the next match.
- Tab and Shift+Tab indent and unindent (4 spaces), also on several selected lines; Enter keeps the line's indentation. Esc leaves the text box, so Tab goes back to moving between buttons. Undo (Ctrl+Z) works after these insertions too.
- In Lua scripts, keywords, strings, comments, numbers and the dongle modules (`kb`, `ui`, `sys`, ...) have different colors.
- **Check** (`.lua` only) sends the text to the dongle, which compiles it without running it: a syntax error appears above the editor with the line number highlighted, and the cursor moves to that line. For injector scripts (in `/payloads`, not in `lib`) it also checks the `--- @key` header (see [Script header](../reference/script-header.md)). The text does not need to be saved first.

### CSV

`.csv` and `.tsv` files open as a table: every cell is editable, **+ Row** and **− Row** add a row below the selected cell's row or remove it, **+ Col** and **− Col** do the same with columns. The separator (comma, semicolon or tab) is detected from the first line; if it is wrong you change it from the menu, and the file is read again. Fields containing separators, quotes or line breaks are written in quotes (RFC 4180); unnecessary quotes disappear on save. **Text** switches to raw text, **Table** goes back to the table.

### config.lua

`config.lua` opens with the passwords hidden: in place of the values of `pass`/`password` (WiFi networks, hotspot) and of everything in `secrets` appears `"<secret:1>"`, `"<secret:2>"`, ... Leave the placeholders where they are (or move them): on save the dongle puts back the real values taken from the current file. To change a password, write the new value in place of the placeholder. The numbers follow the order of the passwords in the file: after saving, the editor reloads the renumbered text, so later saves also restore the right values. A placeholder the file no longer has (for example after editing `config.lua` from [USB disk](copy-files.md) with the page open) blocks the save: reopen the file. **Show secrets** reloads the file with the values in plaintext.

The new configuration takes effect when you exit Web Files, because the running apps are using the old one. Before saving it the page checks the syntax and, if it finds an error, asks for confirmation: a `config.lua` that does not load shows "config.lua error" on exit and leaves the dongle with the default values, so with no known networks (it creates the hotspot with the default password, `grillo-dongle`). From there you can fix the file with Web Files on the hotspot. The check finds syntax errors, not a wrong value.

## Trash

**Move to trash** moves the item to `/.trash` on the card, together with the path it came from and the date (if the dongle knows the time). **Trash** shows the contents:

- **Restore** puts it back in its place. If in the meantime an item with the same name has appeared there, the restored one gets a suffix (`a (2).lua`); if the original folder no longer exists it is recreated.
- **Delete permanently** deletes it from the trash.
- **Empty trash** deletes the whole trash.

The trash is a normal folder on the card: it takes up space until you empty it, and it is also visible from the PC with [USB disk](copy-files.md).

## Android phone

Plugged into the USB-C port of an Android phone, the dongle is powered and Web Files works: to read the card from the phone, open the page over WiFi (or on the dongle's hotspot). Web Files does not expose the card over USB.

In the USB settings, Android shows which of the two devices controls the connection. Do not change the setting: the dongle acts only as a USB peripheral, and when you try to swap the roles the dongle reboots. The most likely cause is that during the role change the phone briefly cuts power to the port (a reconstruction, not measured).

## Without a PIN

In `config.lua`, `webfiles = { pin = false }`. Anyone on the same network could then read, modify and delete everything on the microSD, `config.lua` passwords included. See [Security](../explanation/security.md).
