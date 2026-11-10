#ifndef S20_LCD_H
#define S20_LCD_H

#include "s20_assets.h"
#include "s20_platform.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    S20_DRAW_SET = 0,   /* Black pixels on */
    S20_DRAW_CLR = 1,   /* White pixels off */
    S20_DRAW_XOR = 2    /* Invert */
} s20_draw_mode_t;

typedef enum {
    S20_ALIGN_LEFT = 0,
    S20_ALIGN_CENTER = 1,
    S20_ALIGN_RIGHT = 2
} s20_align_t;

typedef struct {
    uint8_t fb[S20_FB_SIZE];
} s20_lcd_t;

void s20_lcd_clear(s20_lcd_t *lcd, bool black);
void s20_lcd_set_pixel(s20_lcd_t *lcd, int x, int y, s20_draw_mode_t mode);
void s20_lcd_fill_rect(s20_lcd_t *lcd, int x, int y, int w, int h, s20_draw_mode_t mode);
void s20_lcd_draw_rect_outline(s20_lcd_t *lcd, int x, int y, int w, int h, s20_draw_mode_t mode);

void s20_lcd_draw_bitmap_blob(s20_lcd_t *lcd, int x, int y, const s20_bitmap_t *bmp, const uint8_t *blob, s20_draw_mode_t mode);
void s20_lcd_draw_bitmap_raw(s20_lcd_t *lcd, int x, int y, const s20_bitmap_t *bmp, s20_draw_mode_t mode);
void s20_lcd_draw_bitmap_id(s20_lcd_t *lcd, int x, int y, uint16_t bmp_id, s20_draw_mode_t mode);

int s20_text_width(s20_font_id_t font_id, const char *utf8);
int s20_text_first_line_width(s20_font_id_t font_id, const char *utf8);
int s20_lcd_draw_text(s20_lcd_t *lcd, int x, int y, s20_font_id_t font_id,
                      const char *utf8, s20_draw_mode_t mode);
void s20_lcd_draw_text_aligned(s20_lcd_t *lcd, int rx, int ry, int rw, int rh,
                               s20_font_id_t font_id, const char *utf8,
                               s20_align_t align, s20_draw_mode_t mode,
                               int scroll_px, bool single_line);

void s20_lcd_draw_scrollbar(s20_lcd_t *lcd, int x, int y, int h, int total, int current);

#endif /* S20_LCD_H */
