--- @name Copy file (macOS)
--- @desc Copies /data/files/setup.sh from the SD to /tmp in a macOS Terminal, then ejects it.
--- @tags pc-disk, mac
--- @layout it
--- @os mac
--- @start_delay 1500

-- Open Terminal first (Cmd+Space, "terminal"). The SD mounts under /Volumes and is
-- found through its .grillo marker file; diskutil ejects it at the end.
ui.status("Opening Terminal")
kb.press("GUI", "SPACE")
sys.sleep(700)
kb.type("terminal\n")
sys.sleep(2500)

require("pcdisk").run([[cp "$d/data/files/setup.sh" /tmp/ && chmod +x /tmp/setup.sh]])
ui.status("Copied")
sys.sleep(1500)
