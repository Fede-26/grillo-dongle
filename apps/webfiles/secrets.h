#pragma once

#include <stdbool.h>
#include <stddef.h>

// config.lua secrets hidden from the web page, which is not encrypted.
//
// Secrets are the string literals assigned to a `pass` or `password` key
// (WiFi networks, hotspot) and all those inside the `secrets = { ... }`
// table. The text is parsed as Lua (comments, strings with escapes and long
// strings), not with text searches: a "pass" in a comment does not count,
// and a string with a "}" inside does not close the table.

// Copy of `src` with every secret replaced by "<secret:N>" (N from 1, in
// order). Free with free(); NULL when out of memory.
char *secrets_mask(const char *src);

// Text edited by the page with the placeholders still inside: every
// "<secret:N>" literal (anywhere, not only where it was) goes back to the
// N-th literal of `orig` exactly as it was written. An N that `orig` does
// not have is an error: NULL with `*unknown` set to true. Free with free();
// NULL also when out of memory.
char *secrets_unmask(const char *edited, const char *orig, bool *unknown);
