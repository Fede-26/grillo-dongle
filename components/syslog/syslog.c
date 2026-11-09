#include "syslog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

// Riga speciale che chiede al task di scrivere subito e segnalare.
static const char FLUSH_MARK[] = "\x01";

static int log_vprintf(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int r = s_prev(fmt, ap);
    // Il formato di esp_log inizia con la lettera del livello, eventualmente
    // preceduta dal codice colore ANSI.
    const char *p = fmt;
    if (p[0] == '\033') p = strchr(p, 'm') ? strchr(p, 'm') + 1 : p;
    if (s_q && (p[0] == 'E' || p[0] == 'W')) {
        char line[LINE_MAX_];
        vsnprintf(line, sizeof(line), fmt, copy);
        // Non blocca mai: se la coda è piena la riga si perde, ma il
        // logging non deve rallentare chi lo chiama.
        xQueueSend(s_q, line, 0);
    }
    va_end(copy);
    return r;
}

static void write_lines(FILE *f, const char *first)
{
    if (first[0] != FLUSH_MARK[0]) fputs(first, f);
    char line[LINE_MAX_];
    while (xQueueReceive(s_q, line, 0) == pdTRUE) {
        if (line[0] != FLUSH_MARK[0]) fputs(line, f);
    }
}

static void syslog_task(void *arg)
{
    char line[LINE_MAX_];
    for (;;) {
        xQueueReceive(s_q, line, portMAX_DELAY);
        bool flush_req = line[0] == FLUSH_MARK[0];
        if (storage_state() == STORAGE_MOUNTED) {
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
