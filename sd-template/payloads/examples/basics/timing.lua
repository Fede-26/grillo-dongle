--- @name Typing speed
--- @desc Types slowly, then fast, then with jitter. Shows kb.speed, kb.hold, kb.jitter, kb.line_delay.
--- @tags basics, timing
--- @layout it
--- @os windows
--- @key_delay 8
--- @hold 5

-- Four timings, all in ms. Calling with a value sets it, without reads it.
--   kb.speed       pause between two keys        (header: @key_delay)
--   kb.hold        how long each key stays down  (header: @hold)
--   kb.jitter      +- random ms on the pause     (header: @jitter)
--   kb.line_delay  extra pause after every "\n"  (header: @line_delay)
-- Slow targets (remote desktops, old BIOS screens, kiosk apps) need larger values.

ui.status("default: " .. kb.speed() .. " / " .. kb.hold())
kb.type("default speed: the quick brown fox\n")

kb.speed(60); kb.hold(40)
kb.type("slow: the quick brown fox\n")

kb.speed(0); kb.hold(1)
kb.type("fast: the quick brown fox\n")

kb.speed(30); kb.jitter(25)        -- irregular rhythm, looks like a person
kb.type("jitter: the quick brown fox\n")

kb.jitter(0); kb.line_delay(500)   -- wait half a second after each Enter
kb.type("line 1\nline 2\nline 3\n")
