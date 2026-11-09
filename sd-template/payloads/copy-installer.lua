--- @name Copy installer
--- @desc Template: mounts the SD read-only, copies a file, waits for the eject.
--- @layout it
--- @os windows
--- @start_delay 1000

-- TODO: set layout and os in the header to match the target PC.
-- TODO: every command below is an empty template. Write them yourself.

ui.status("Waiting for the shell")
-- TODO: open the shell on the target (kb.press / kb.type).

ui.status("Attaching SD")
disk.attach()

ui.status("Copying")
-- TODO: command that copies the file from the SD volume to the PC (kb.type).

ui.status("Ejecting")
-- TODO: command that ejects the SD volume on the PC (kb.type).

-- The host eject is detected by the dongle; a button click also continues.
local how = disk.wait_eject(60000)
if not how then error("SD not ejected within 60 s") end
sys.log("disk released by " .. how)

disk.detach()

ui.status("Installing")
-- TODO: command that runs the installer (kb.type).

ui.status("Done")
