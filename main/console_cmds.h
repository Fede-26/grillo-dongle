#pragma once

void console_cmds_register(void);

// Implemented in main.c: asks to reload config.lua on the next app exit,
// when no app is using the configuration.
void main_request_config_reload(void);
