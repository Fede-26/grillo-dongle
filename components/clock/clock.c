#include "clock.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "clock";

// November 2023: an earlier time means nobody has set it
// (at power-on the clock starts from 1970).
#define VALID_TIME 1700000000

// lwIP keeps the pointer to the server name, not a copy: the config.lua
// string is freed when the configuration is reloaded.
static char s_server[64];

void clock_set_timezone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

bool clock_valid(void)
{
    return time(NULL) > VALID_TIME;
}

bool clock_set(time_t utc)
{
    if (utc <= VALID_TIME) return false;
    struct timeval tv = {.tv_sec = utc};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "time set: %lld", (long long)utc);
    return true;
}

void clock_sntp_start(const char *server)
{
    if (esp_sntp_enabled()) return;
    strlcpy(s_server, server, sizeof(s_server));
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_server);
    esp_sntp_init();
}

void clock_sntp_stop(void)
{
    if (esp_sntp_enabled()) esp_sntp_stop();
}
