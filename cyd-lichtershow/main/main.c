/*
 * CYD Lichtershow – bunte Animationen auf dem ILI9341-Display
 * des ESP32 Cheap Yellow Display (ESP32-2432S028)
 *
 * Hardware-Pinbelegung (CYD):
 *   TFT_MOSI : GPIO 13
 *   TFT_MISO : GPIO 12
 *   TFT_SCLK : GPIO 14
 *   TFT_CS   : GPIO 15
 *   TFT_DC   : GPIO  2
 *   TFT_BL   : GPIO 21  (Hintergrundbeleuchtung)
 *   Display  : ILI9341, 320 x 240 px
 */

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"

#include "esp_log.h"

/* ------------------------------------------------------------------ */
/*  Display-Konstanten                                                  */
/* ------------------------------------------------------------------ */
#define LCD_HOST          SPI2_HOST
#define LCD_PIXEL_CLOCK   40000000   /* 40 MHz */

#define PIN_NUM_MOSI      13
#define PIN_NUM_MISO      12
#define PIN_NUM_CLK       14
#define PIN_NUM_CS        15
#define PIN_NUM_DC         2
#define PIN_NUM_RST       -1          /* auf dem CYD hardverdrahtet */
#define PIN_NUM_BL        21

#define LCD_H_RES         320
#define LCD_V_RES         240

/* Zeilenpuffer-Höhe für die Übertragung */
#define LINE_BATCH        16

static const char *TAG = "lichtershow";

/* ------------------------------------------------------------------ */
/*  Hilfsfunktionen: Farben                                             */
/* ------------------------------------------------------------------ */

/* RGB888 → RGB565 (Big-Endian, wie ILI9341 es erwartet) */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);   /* Byte-Swap für SPI */
}

/* Hue [0..360) → RGB565  (S=1, V=1) */
static uint16_t hsv_to_rgb565(float hue, float sat, float val)
{
    float h = fmodf(hue, 360.0f);
    float s = sat;
    float v = val;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r1, g1, b1;

    if      (h < 60)  { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }

    return rgb565((uint8_t)((r1 + m) * 255),
                  (uint8_t)((g1 + m) * 255),
                  (uint8_t)((b1 + m) * 255));
}

/* ------------------------------------------------------------------ */
/*  Zeilenpuffer                                                        */
/* ------------------------------------------------------------------ */
static uint16_t line_buf[LCD_H_RES * LINE_BATCH];

static void flush_lines(esp_lcd_panel_handle_t panel,
                         int y_start, int lines)
{
    esp_lcd_panel_draw_bitmap(panel, 0, y_start,
                              LCD_H_RES, y_start + lines,
                              line_buf);
}

/* ------------------------------------------------------------------ */
/*  Animations-Szenen                                                   */
/* ------------------------------------------------------------------ */

/*
 * Szene 1: Regenbogen-Welle
 *   Jede Spalte hat eine Farbe, die vom Hue-Offset abhängt.
 *   Der Offset verschiebt sich mit der Zeit → Welle läuft über den Schirm.
 */
static void scene_rainbow_wave(esp_lcd_panel_handle_t panel,
                                int frames)
{
    for (int f = 0; f < frames; f++) {
        float offset = f * 1.2f;               /* Geschwindigkeit */
        for (int y0 = 0; y0 < LCD_V_RES; y0 += LINE_BATCH) {
            int batch = (y0 + LINE_BATCH <= LCD_V_RES) ?
                         LINE_BATCH : (LCD_V_RES - y0);
            for (int dy = 0; dy < batch; dy++) {
                int y = y0 + dy;
                for (int x = 0; x < LCD_H_RES; x++) {
                    float hue = fmodf(
                        x * (360.0f / LCD_H_RES) +
                        y * 0.3f + offset, 360.0f);
                    line_buf[dy * LCD_H_RES + x] =
                        hsv_to_rgb565(hue, 1.0f, 1.0f);
                }
            }
            flush_lines(panel, y0, batch);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*
 * Szene 2: Konzentrischer Kreise (Ripple)
 *   Kreise wachsen vom Mittelpunkt nach außen.
 */
static void scene_ripple(esp_lcd_panel_handle_t panel, int frames)
{
    float cx = LCD_H_RES / 2.0f;
    float cy = LCD_V_RES / 2.0f;

    for (int f = 0; f < frames; f++) {
        float phase = f * 4.0f;
        for (int y0 = 0; y0 < LCD_V_RES; y0 += LINE_BATCH) {
            int batch = (y0 + LINE_BATCH <= LCD_V_RES) ?
                         LINE_BATCH : (LCD_V_RES - y0);
            for (int dy = 0; dy < batch; dy++) {
                int y = y0 + dy;
                float fy = (float)y - cy;
                for (int x = 0; x < LCD_H_RES; x++) {
                    float fx = (float)x - cx;
                    float dist = sqrtf(fx * fx + fy * fy);
                    float wave = sinf((dist - phase) * 0.18f);
                    float hue  = fmodf(dist * 1.5f + f * 2.0f, 360.0f);
                    float val  = (wave + 1.0f) * 0.5f;
                    line_buf[dy * LCD_H_RES + x] =
                        hsv_to_rgb565(hue, 1.0f, val);
                }
            }
            flush_lines(panel, y0, batch);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

/*
 * Szene 3: Plasma
 *   Klassisches Demo-Scene-Plasma aus überlagerten Sinus-Wellen.
 */
static void scene_plasma(esp_lcd_panel_handle_t panel, int frames)
{
    for (int f = 0; f < frames; f++) {
        float t = f * 0.08f;
        for (int y0 = 0; y0 < LCD_V_RES; y0 += LINE_BATCH) {
            int batch = (y0 + LINE_BATCH <= LCD_V_RES) ?
                         LINE_BATCH : (LCD_V_RES - y0);
            for (int dy = 0; dy < batch; dy++) {
                int y = y0 + dy;
                float fy = (float)y / LCD_V_RES;
                for (int x = 0; x < LCD_H_RES; x++) {
                    float fx = (float)x / LCD_H_RES;
                    float v  = sinf(fx * 10.0f + t)
                             + sinf(fy * 10.0f + t)
                             + sinf((fx + fy) * 7.0f + t)
                             + sinf(sqrtf(fx*fx + fy*fy) * 12.0f + t);
                    float hue = fmodf(v * 45.0f + 180.0f, 360.0f);
                    line_buf[dy * LCD_H_RES + x] =
                        hsv_to_rgb565(hue, 1.0f, 1.0f);
                }
            }
            flush_lines(panel, y0, batch);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*
 * Szene 4: Farbige vertikale Balken, die von links nach rechts laufen
 */
static void scene_color_bars(esp_lcd_panel_handle_t panel, int frames)
{
    /* Farbtabelle einmalig berechnen */
    static uint16_t colors_be[9];
    static bool colors_init = false;
    if (!colors_init) {
        colors_be[0] = rgb565(255,   0,   0);
        colors_be[1] = rgb565(255, 128,   0);
        colors_be[2] = rgb565(255, 255,   0);
        colors_be[3] = rgb565(  0, 255,   0);
        colors_be[4] = rgb565(  0, 255, 255);
        colors_be[5] = rgb565(  0,   0, 255);
        colors_be[6] = rgb565(128,   0, 255);
        colors_be[7] = rgb565(255,   0, 255);
        colors_be[8] = rgb565(255, 255, 255);
        colors_init  = true;
    }

    int bar_w = 40;
    for (int f = 0; f < frames; f++) {
        int scroll = f * 2;
        for (int y0 = 0; y0 < LCD_V_RES; y0 += LINE_BATCH) {
            int batch = (y0 + LINE_BATCH <= LCD_V_RES) ?
                         LINE_BATCH : (LCD_V_RES - y0);
            for (int dy = 0; dy < batch; dy++) {
                for (int x = 0; x < LCD_H_RES; x++) {
                    int bar_idx = ((x + scroll) / bar_w) % 9;
                    line_buf[dy * LCD_H_RES + x] = colors_be[bar_idx];
                }
            }
            flush_lines(panel, y0, batch);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/*
 * Szene 5: Sternenhimmel – zufällig aufblitzende Pixel auf schwarzem Grund
 */
#define NUM_STARS 200

static void scene_starfield(esp_lcd_panel_handle_t panel, int frames)
{
    typedef struct { int16_t x, y; uint8_t bright; int8_t delta; } Star;
    static Star stars[NUM_STARS];

    /* Initialisierung */
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x      = (int16_t)(rand() % LCD_H_RES);
        stars[i].y      = (int16_t)(rand() % LCD_V_RES);
        stars[i].bright = (uint8_t)(rand() % 256);
        stars[i].delta  = (rand() & 1) ? 3 : -3;
    }

    for (int f = 0; f < frames; f++) {
        for (int y0 = 0; y0 < LCD_V_RES; y0 += LINE_BATCH) {
            int batch = (y0 + LINE_BATCH <= LCD_V_RES) ?
                         LINE_BATCH : (LCD_V_RES - y0);
            /* Zeilenpuffer schwarz füllen, dann Sterne einzeichnen */
            memset(line_buf, 0, sizeof(uint16_t) * LCD_H_RES * batch);
            for (int i = 0; i < NUM_STARS; i++) {
                if (stars[i].y >= y0 && stars[i].y < y0 + batch) {
                    uint8_t b = stars[i].bright;
                    line_buf[(stars[i].y - y0) * LCD_H_RES + stars[i].x] =
                        rgb565(b, b, b);
                }
            }
            flush_lines(panel, y0, batch);
        }

        /* Helligkeit animieren */
        for (int i = 0; i < NUM_STARS; i++) {
            int nb = (int)stars[i].bright + stars[i].delta;
            if (nb > 255) { nb = 255; stars[i].delta = -3; }
            if (nb <  10) { nb =  10; stars[i].delta =  3;
                /* Stern zufällig neu platzieren */
                stars[i].x = (int16_t)(rand() % LCD_H_RES);
                stars[i].y = (int16_t)(rand() % LCD_V_RES);
            }
            stars[i].bright = (uint8_t)nb;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ------------------------------------------------------------------ */
/*  Hintergrundbeleuchtung                                              */
/* ------------------------------------------------------------------ */
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PIN_NUM_BL,
        .duty       = 255,   /* volle Helligkeit */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    ESP_LOGI(TAG, "CYD Lichtershow startet…");

    /* ---- Hintergrundbeleuchtung ---- */
    backlight_init();

    /* ---- SPI-Bus ---- */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = PIN_NUM_MOSI,
        .miso_io_num   = PIN_NUM_MISO,
        .sclk_io_num   = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LINE_BATCH * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* ---- LCD I/O (SPI-Panel-IO) ---- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = PIN_NUM_DC,
        .cs_gpio_num       = PIN_NUM_CS,
        .pclk_hz           = LCD_PIXEL_CLOCK,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* ---- ILI9341-Panel ---- */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(
        io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);

    /* Anzeigebereich: volle Auflösung, Querformat */
    esp_lcd_panel_set_gap(panel_handle, 0, 0);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_LOGI(TAG, "Display initialisiert – starte Animationen");

    /* ---- Endlos-Animationsschleife ---- */
    while (1) {
        ESP_LOGI(TAG, "Szene: Regenbogen-Welle");
        scene_rainbow_wave(panel_handle, 120);

        ESP_LOGI(TAG, "Szene: Ripple");
        scene_ripple(panel_handle, 100);

        ESP_LOGI(TAG, "Szene: Plasma");
        scene_plasma(panel_handle, 120);

        ESP_LOGI(TAG, "Szene: Farbbalken");
        scene_color_bars(panel_handle, 100);

        ESP_LOGI(TAG, "Szene: Sternenhimmel");
        scene_starfield(panel_handle, 150);
    }
}
