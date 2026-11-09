#include "display.h"

#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;
static SemaphoreHandle_t s_done;

// Sequenza di init ST7735S presa dal driver dell'esempio ufficiale LilyGO
// (examples/lcd/bsp_lcd/esp_lcd_st7735.c). Ritardi su SWRESET/SLPOUT
// aggiunti come da datasheet (120 ms).
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
    uint16_t delay_ms;
} init_cmd_t;

static const init_cmd_t s_init[] = {
    {0x01, 0, {0}, 120},                                // SWRESET
    {0x11, 0, {0}, 120},                                // SLPOUT
    {0xB1, 3, {0x05, 0x3A, 0x3A}, 0},                   // FRMCTR1
    {0xB2, 3, {0x05, 0x3A, 0x3A}, 0},                   // FRMCTR2
    {0xB3, 6, {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A}, 0}, // FRMCTR3
    {0xB4, 1, {0x03}, 0},                               // INVCTR
    {0xC0, 3, {0x62, 0x02, 0x04}, 0},                   // PWCTR1
    {0xC1, 1, {0xC0}, 0},                               // PWCTR2
    {0xC2, 2, {0x0D, 0x00}, 0},                         // PWCTR3
    {0xC3, 2, {0x8D, 0x6A}, 0},                         // PWCTR4
    {0xC4, 2, {0x8D, 0xEE}, 0},                         // PWCTR5
    {0xC5, 1, {0x0E}, 0},                               // VMCTR1
    {0x21, 0, {0}, 0},                                  // INVON: il pannello ha i colori invertiti
    // MADCTL: MY (0x80) | MV (0x20, orizzontale) | BGR (0x08)
    {0x36, 1, {0xA8}, 0},
    {0x3A, 1, {0x05}, 0},                               // COLMOD 16 bit
    {0xE0, 16, {0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07, 0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10}, 0},
    {0xE1, 16, {0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08, 0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10}, 0},
    {0x13, 0, {0}, 10},                                 // NORON
    {0x29, 0, {0}, 20},                                 // DISPON
};

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        // Backlight attivo basso: invertendo l'uscita, duty alto = più luce.
        .flags.output_invert = (BOARD_LCD_BL_ON_LEVEL == 0),
    };
    ledc_channel_config(&c);
}

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 255 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t display_init(void)
{
    backlight_init();

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_W * DISPLAY_H * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    // Reset hardware prima della sequenza di init
    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, s_init[i].cmd, s_init[i].len ? s_init[i].data : NULL,
                                                      s_init[i].len), TAG, "init cmd 0x%02x", s_init[i].cmd);
        if (s_init[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(s_init[i].delay_ms));
    }

    // La finestra di scrittura è sempre l'intero schermo: la impostiamo una
    // volta sola, così ogni flush è solo RAMWR + dati.
    uint16_t x0 = BOARD_LCD_X_GAP, x1 = BOARD_LCD_X_GAP + DISPLAY_W - 1;
    uint16_t y0 = BOARD_LCD_Y_GAP, y1 = BOARD_LCD_Y_GAP + DISPLAY_H - 1;
    esp_lcd_panel_io_tx_param(s_io, 0x2A, (uint8_t[]){x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF}, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, (uint8_t[]){y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF}, 4);

    s_fb = heap_caps_calloc(DISPLAY_W * DISPLAY_H, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_fb, ESP_ERR_NO_MEM, TAG, "framebuffer");
    s_done = xSemaphoreCreateBinary();

    display_flush();
    display_set_brightness(80);
    return ESP_OK;
}

uint16_t *display_fb(void)
{
    return s_fb;
}

void display_flush(void)
{
    esp_lcd_panel_io_tx_color(s_io, 0x2C, s_fb, DISPLAY_W * DISPLAY_H * 2);
    // Attendiamo la fine del DMA: chi chiama può subito ridisegnare nel
    // framebuffer senza tearing.
    xSemaphoreTake(s_done, portMAX_DELAY);
}
