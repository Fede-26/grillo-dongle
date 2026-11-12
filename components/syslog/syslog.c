#include "syslog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "clock.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "storage.h"

#define LOG_DIR    STORAGE_ROOT "/logs"
#define LOG_FILE   LOG_DIR "/grillo.log"
#define LOG_OLD    LOG_DIR "/grillo.1.log"
#define LOG_MAX    (256 * 1024)
#define LINE_MAX_  160

static QueueHandle_t s_q;
static SemaphoreHandle_t s_flush_done;
static vprintf_like_t s_prev;

// Special line that asks the task to write immediately and signal.
static const char FLUSH_MARK[] = "\x01";

static int log_vprintf(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int r = s_prev(fmt, ap);
    // The esp_log format starts with the level letter, possibly
    // preceded by the ANSI color code.
    const char *p = fmt;
    if (p[0] == '\033') p = strchr(p, 'm') ? strchr(p, 'm') + 1 : p;
    if (s_q && (p[0] == 'E' || p[0] == 'W' || p[0] == 'I')) {
        char line[LINE_MAX_];
        vsnprintf(line, sizeof(line), fmt, copy);
        // Of the info lines, only the scripts' sys.log ones are kept
        // (tag "script"): they are the diary the script author asked for.
        // The tag is a format argument, so the already composed line is
        // inspected ("I (1234) script: ...").
        const char *q = line[0] == '\033' && strchr(line, 'm') ? strchr(line, 'm') + 1 : line;
        const char *tag = strstr(q, ") ");
        bool keep = p[0] != 'I' || (tag && strncmp(tag + 2, "script:", 7) == 0);
        // Never blocks: if the queue is full the line is lost, but
        // logging must not slow down the caller.
        if (keep) xQueueSend(s_q, line, 0);
    }
    va_end(copy);
    return r;
}

// With the time known (SNTP or host.clock) every line starts with the local
// date and time: the esp_log time since boot alone does not say when it
// happened. It is taken at write time, which follows the line by a few
// milliseconds, so as not to call localtime inside the vprintf of any task.
static void write_lines(FILE *f, const char *first)
{
    char stamp[24] = "";
    if (clock_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S ", &tm);
    }
    // fputs and not fprintf: the task has 3 KB of stack
    if (first[0] != FLUSH_MARK[0]) {
        fputs(stamp, f);
        fputs(first, f);
    }
    char line[LINE_MAX_];
    while (xQueueReceive(s_q, line, 0) == pdTRUE) {
        if (line[0] != FLUSH_MARK[0]) {
            fputs(stamp, f);
            fputs(line, f);
        }
    }
}

static void syslog_task(void *arg)
{
    char line[LINE_MAX_];
    for (;;) {
        xQueueReceive(s_q, line, portMAX_DELAY);
        bool flush_req = line[0] == FLUSH_MARK[0];
        // With the SD shared read-only with the PC nothing is written: the
        // queued lines are discarded.
        if (storage_begin_write()) {
            mkdir(LOG_DIR, 0777);
            struct stat st;
            if (stat(LOG_FILE, &st) == 0 && st.st_size > LOG_MAX) {
                remove(LOG_OLD);
                rename(LOG_FILE, LOG_OLD);
            }
            FILE *f = fopen(LOG_FILE, "a");
            if (f) {
                write_lines(f, line);
                fclose(f);
            }
            storage_end_write();
        }
        if (flush_req) xSemaphoreGive(s_flush_done);
    }
}

void syslog_init(void)
{
    s_q = xQueueCreate(16, LINE_MAX_);
    s_flush_done = xSemaphoreCreateBinary();
    xTaskCreate(syslog_task, "syslog", 3072, NULL, 2, NULL);
    s_prev = esp_log_set_vprintf(log_vprintf);
}

void syslog_flush(void)
{
    if (!s_q) return;
    char mark[LINE_MAX_] = {FLUSH_MARK[0], 0};
    if (xQueueSend(s_q, mark, pdMS_TO_TICKS(100)) == pdTRUE) {
        xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(1000));
    }
}
