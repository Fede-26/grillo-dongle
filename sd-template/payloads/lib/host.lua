-- Return channel from the PC: the script types a command in the terminal and
-- learns when it finished and with which exit code.
--
--   local host = require("host")
--   host.shell("powershell")            -- once, with the terminal in the foreground
--   local code = host.run([[Start-Process notepad -Wait]])
--   if code ~= 0 then error("notepad: " .. tostring(code)) end
--
-- How it works: after the command the script types a tail that writes the line
-- "@G<nonce>:<code>" to the dongle's serial port (CDC). The dongle accepts
-- only that line: from the PC come "finished" and a number, never the output.
-- The port is found from the dongle's USB serial number (sys.serial()), so
-- it works even with several dongles plugged in.
--
-- Reading and recording values (inventory):
--   local mac = host.machine()                       -- machine id: MAC of a physical adapter
--   host.log("serial", [[(Get-CimInstance Win32_BIOS).SerialNumber]])
--   local model = host.get([[(Get-CimInstance Win32_ComputerSystem).Model]])
--   host.clock()                                     -- PC time on the dongle (log, sys.date)
--   local note = host.input("Comment")               -- the user types on the PC and presses the button
-- The value is at most 256 bytes; the dongle replaces with "_" anything
-- that is not a letter (accented ones included, in UTF-8), a digit, ". _ : -" or a space. Never retype a value read
-- from the PC inside a shell command without checking it: it may contain whatever
-- the person who configured the machine chose.
--
-- The command must be blocking: with a program that detaches at once
-- (e.g. `Start-Process` without `-Wait`, `open` on macOS) the code arrives as soon
-- as it starts, not when it ends. Do not write `${` in the command: kb.type reads it
-- as a script variable. host.shell first starts PowerShell or sh in the
-- terminal (common.shell), so any open shell works: cmd.exe,
-- fish, zsh, bash.
local M = {}
local common = require("common")

local kind -- "powershell" | "sh", set by host.shell

local function nonce()
    return string.format("%06x", math.random(0, 0xFFFFFF))
end

-- Text to type once per shell: finds the port and defines Send-G / $G.
local function helper(serial)
    if kind == "powershell" then
        -- The COM port is a child of the composite device, whose id contains the serial number.
        return [[$G=(Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' -and (Get-PnpDeviceProperty -InstanceId $_.PNPDeviceID -KeyName DEVPKEY_Device_Parent).Data -like '*]]
            .. serial .. [[*' } | Select-Object -First 1 -ExpandProperty Name) -replace '.*\((COM\d+)\).*','$1'; function Send-G($n,$c){$p=New-Object IO.Ports.SerialPort $G,115200; $p.Encoding=[Text.Encoding]::UTF8; $p.Open(); $p.WriteLine('@G'+$n+':'+$c); $p.Close()}]]
    end
    -- macOS: /dev/cu.usbmodem<serial><n>; Linux: /dev/serial/by-id/*<serial>*. In sh a
    -- pattern with no match reaches ls as is, and the error goes to /dev/null.
    return [[G=$(ls /dev/cu.usbmodem*]] .. serial .. [[* /dev/serial/by-id/*]] .. serial
        .. [[* 2>/dev/null | head -1)]]
end

-- Exit code of the command, or nil if it does not arrive within opts.timeout (default 60000 ms).
function M.run(cmd, opts)
    if not kind then error("host.run: call host.shell() first") end
    local n = nonce()
    local line
    if kind == "powershell" then
        line = [[$LASTEXITCODE=0; ]] .. cmd .. [[; Send-G ]] .. n .. [[ $(if($?){0}elseif($LASTEXITCODE){$LASTEXITCODE}else{1})]]
    else
        line = cmd .. [[; c=$?; printf '@G%s:%s\n' ]] .. n .. [[ $c > "$G"]]
    end
    -- expect before typing: the reply may arrive while the dongle is still pressing keys
    host.expect(n)
    kb.type(line .. "\n")
    return host.wait((opts and opts.timeout) or 60000)
end

-- Value (sanitized string, max 256 bytes) of the expression `expr` evaluated in the
-- PC shell, or nil if it does not arrive within opts.timeout (default 60000 ms).
-- An expression that returns several lines is joined with spaces.
function M.get(expr, opts)
    if not kind then error("host.get: call host.shell() first") end
    local n = nonce()
    local line
    if kind == "powershell" then
        line = [[Send-G ]] .. n .. [[ ((]] .. expr .. [[) -join ' ' -replace '\s+',' ')]]
    else
        line = [[printf '@G%s:%s\n' ]] .. n .. [[ "$( (]] .. expr .. [[) | tr '\n' ' ')" > "$G"]]
    end
    host.expect(n)
    kb.type(line .. "\n")
    local _, text = host.wait((opts and opts.timeout) or 60000)
    return text
end

local machine_id, machine_date

local function default_machine_expr()
    if kind == "powershell" then
        return [[(Get-NetAdapter -Physical | Sort-Object MacAddress | Select-Object -First 1).MacAddress]]
    elseif kb.os() == "mac" then
        return [[ifconfig en0 | awk '/ether/{print $2}']]
    end
    return [[for d in /sys/class/net/*/device; do cat "$(dirname "$d")/address"; done | sort | head -1]]
end

-- Sets the machine id (which names /logs/host/<id>.csv) and the PC date
-- used in the log lines. Without arguments it uses the MAC of the first physical
-- network adapter (in address order): it belongs to the hardware, so it differs even between
-- two PCs cloned with Clonezilla, while MachineGuid and host name are copied with the disk.
function M.machine(expr)
    machine_id = M.get(expr or default_machine_expr())
    if not machine_id or machine_id == "" then error("host.machine: no value from the PC") end
    machine_date = M.get(kind == "powershell" and [[Get-Date -Format s]] or [[date +%Y-%m-%dT%H:%M:%S]])
    return machine_id
end

-- Sets the dongle clock from the PC time and returns the local date
-- like sys.date(). The UTC time is read (seconds since 1970): the dongle shows it
-- with the time zone of config.lua (timezone, default Europe/Rome with daylight saving time),
-- so a PC with the wrong time zone does not shift the logs. The dongle has no
-- battery: the time stays valid until power-off.
function M.clock(opts)
    local v = M.get(kind == "powershell" and "[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()" or "date +%s", opts)
    local t = v and tonumber(v)
    if not t then error("host.clock: no time from the PC") end
    sys.set_time(t)
    return sys.date()
end

-- Evaluates `expr` on the PC like host.get and appends a line to /logs/host/<id>.csv on the
-- dongle's SD (with the disk attached the line waits for the detach). Returns the value.
function M.log(label, expr, opts)
    local value = M.get(expr, opts)
    if value == nil then error("host.log: no reply for '" .. label .. "'") end
    host.append(machine_id or "unknown", machine_date or "", label, value)
    return value
end

-- Text to put in single quotes in the PC shell
local function quoted(s)
    if kind == "powershell" then return "'" .. s:gsub("'", "''") .. "'" end
    return "'" .. s:gsub("'", [['\'']]) .. "'"
end

-- Text typed by the user on the PC keyboard, or nil if it does not arrive within
-- opts.timeout (default 600000 ms). The dongle types a command that shows
-- `prompt` and reads a line (Read-Host, read), then stops: the user types and
-- presses the dongle button (or Enter); on click the dongle presses Enter for
-- them and resumes with the text received on the serial port. In sh the arrow keys do not
-- move the cursor: they would end up in the text as characters. The text is
-- sanitized like that of host.get: accented letters stay, symbols
-- such as , ; " $ become "_".
function M.input(prompt, opts)
    if not kind then error("host.input: call host.shell() first") end
    opts = opts or {}
    local n = nonce()
    local line
    if kind == "powershell" then
        line = [[Send-G ]] .. n .. [[ (Read-Host ]] .. quoted(prompt) .. [[)]]
    else
        line = [[printf '%s: ' ]] .. quoted(prompt) .. [[; IFS= read -r v; printf '@G%s:%s\n' ]] .. n .. [[ "$v" > "$G"]]
    end
    host.expect(n)
    kb.type(line .. "\n")
    ui.status(opts.status or "Type on the PC, then click")
    local r, text = host.wait(opts.timeout or 600000, true)
    if r == false then
        kb.tap("ENTER")
        r, text = host.wait(15000)
    end
    if r == nil then return nil end
    return text
end

-- Waits until the shell has run everything typed before.
function M.sync(opts)
    return M.run(kind == "powershell" and "$null" or ":", opts)
end

-- Starts the shell ("powershell" or "sh"), prepares it and checks the channel: error if
-- it does not reply.
function M.shell(k)
    if k ~= "powershell" and k ~= "sh" then error("host.shell: powershell or sh") end
    kind = k
    common.shell(k)
    local serial = sys.serial()
    if serial == "" then error("host.shell: USB serial number not available") end
    kb.type(helper(serial) .. "\n")
    sys.sleep(1500)
    if M.sync({timeout = 15000}) == nil then
        error("host.shell: the PC does not reply on the dongle serial port")
    end
end

return M
