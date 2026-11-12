#pragma once

#include <stdbool.h>
#include <stdio.h>

#define PAYLOAD_DIR "/sd/payloads"
#define PAYLOAD_LIB_DIR PAYLOAD_DIR "/lib"
#define PAYLOAD_PATH_MAX 160

// Metadata read from the header of a script ("--- @key value"),
// without running it. Numeric fields are -1 if missing.
typedef struct {
    char path[PAYLOAD_PATH_MAX];
    char name[48];
    char desc[160];
    char layout[16];
    char os[12];
    char led[32];
    char tags[40]; // @tags: words separated by commas or spaces
    int key_delay, hold, jitter, line_delay, start_delay, timeout;
    char error[96]; // invalid header: message with line number
} payload_meta_t;

typedef struct {
    char name[64];  // file or folder name
    char title[48]; // @name or file name without extension
    bool is_dir;
    // Scripts only, as written in the header (empty if missing: the default
    // comes from config.lua, resolved by whoever shows the row).
    char layout[16];
    char os[12];
    char tags[40];
    char path[PAYLOAD_PATH_MAX]; // full path, needed by the views that do not follow folders
} payload_entry_t;

// Lists folders and .lua scripts in `dir` (folders first, alphabetical order).
// Skips hidden files, AppleDouble files ("._*") and the lib folder.
int payload_list(const char *dir, payload_entry_t *out, int max);
bool payload_read_meta(const char *path, payload_meta_t *m);
// Like payload_read_meta from an already open file (fmemopen too: the file
// browser checks the header of a text not yet saved).
void payload_parse_meta(FILE *f, const char *path, payload_meta_t *m);

// Like payload_list but recursive (up to 3 levels) and scripts only, in a
// flat list: basis for the by-OS and by-tag views.
int payload_scan(const char *dir, payload_entry_t *out, int max);
// true if `tag` appears in `tags` (case-insensitive, whole words).
bool payload_has_tag(const char *tags, const char *tag);

// Effective layout and OS of the script: header, otherwise config.lua.
// payload_os returns a hid_os_t.
const char *payload_layout(const payload_entry_t *e);
int payload_os(const payload_entry_t *e);

// Distinct tags found in the scripts, in alphabetical order, at most `max`.
// PAYLOAD_TAG_LEN matches the tags field: a truncated tag no longer matched
// payload_has_tag.
#define PAYLOAD_TAG_LEN sizeof(((payload_entry_t *)0)->tags)
int payload_collect_tags(const payload_entry_t *e, int n, char tags[][PAYLOAD_TAG_LEN], int max);
