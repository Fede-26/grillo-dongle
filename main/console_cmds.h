#pragma once

void console_cmds_register(void);

// Implementata in main.c: chiede di rileggere config.lua alla prossima
// uscita da un'app, quando nessuna app sta usando la configurazione.
void main_request_config_reload(void);
