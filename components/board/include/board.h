#pragma once

// Pin map del LilyGO T-Dongle-S3 (variante con LCD, senza PSRAM).
// Fonte: esempi ufficiali github.com/Xinyuan-LilyGO/T-Dongle-S3
// (examples/lcd, examples/led, examples/sd_card).

// LCD ST7735 0.96" 80x160, usato in orizzontale 160x80
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_MOSI          3
#define BOARD_LCD_SCLK          5
#define BOARD_LCD_CS            4
#define BOARD_LCD_DC            2
#define BOARD_LCD_RST           1
#define BOARD_LCD_BL            38
#define BOARD_LCD_BL_ON_LEVEL   0      // backlight attivo basso
#define BOARD_LCD_WIDTH         160
#define BOARD_LCD_HEIGHT        80
#define BOARD_LCD_X_GAP         1      // offset RAM controller in orizzontale
#define BOARD_LCD_Y_GAP         26
#define BOARD_LCD_PCLK_HZ       (27 * 1000 * 1000)

// LED RGB APA102 (DotStar), ordine colori BGR
#define BOARD_LED_DI            40
#define BOARD_LED_CI            39

// microSD su SDMMC 4-bit
#define BOARD_SD_CLK            12
#define BOARD_SD_CMD            16
#define BOARD_SD_D0             14
#define BOARD_SD_D1             17
#define BOARD_SD_D2             21
#define BOARD_SD_D3             18

// Unico pulsante: è anche lo strapping pin BOOT (premuto all'accensione = download mode)
#define BOARD_BUTTON            0
#define BOARD_BUTTON_ACTIVE     0
