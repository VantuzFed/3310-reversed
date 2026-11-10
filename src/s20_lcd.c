#include "s20_lcd.h"
#include <stddef.h>
#include <string.h>

static uint16_t utf8_next(const char **p) {
    const uint8_t *s = (const uint8_t *)*p;
    if (!s || !*s) return 0;
    uint8_t c = *s++;
    if (c < 0x80) {
        *p = (const char *)s;
        return c;
    } else if ((c & 0xE0) == 0xC0 && s[0]) {
        uint16_t cp = (uint16_t)(((c & 0x1F) << 6) | (s[0] & 0x3F));
        *p = (const char *)(s + 1);
        return cp;
    } else if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
        uint16_t cp = (uint16_t)(((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F));
        *p = (const char *)(s + 2);
        return cp;
    }
    *p = (const char *)s;
    return '?';
}

void s20_lcd_clear(s20_lcd_t *lcd, bool black) {
    memset(lcd->fb, black ? 0xFF : 0x00, S20_FB_SIZE);
}

void s20_lcd_set_pixel(s20_lcd_t *lcd, int x, int y, s20_draw_mode_t mode) {
    if (x < 0 || x >= S20_LCD_WIDTH || y < 0 || y >= S20_LCD_HEIGHT) return;
    uint8_t *cell = &lcd->fb[(y >> 3) * S20_LCD_WIDTH + x];
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (mode == S20_DRAW_SET) {
        *cell |= mask;
    } else if (mode == S20_DRAW_CLR) {
        *cell &= (uint8_t)~mask;
    } else {
        *cell ^= mask;
    }
}

void s20_lcd_fill_rect(s20_lcd_t *lcd, int x, int y, int w, int h, s20_draw_mode_t mode) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            s20_lcd_set_pixel(lcd, px, py, mode);
        }
    }
}

void s20_lcd_draw_rect_outline(s20_lcd_t *lcd, int x, int y, int w, int h, s20_draw_mode_t mode) {
    if (w <= 0 || h <= 0) return;
    s20_lcd_fill_rect(lcd, x, y, w, 1, mode);
    s20_lcd_fill_rect(lcd, x, y + h - 1, w, 1, mode);
    s20_lcd_fill_rect(lcd, x, y, 1, h, mode);
    s20_lcd_fill_rect(lcd, x + w - 1, y, 1, h, mode);
}

void s20_lcd_draw_bitmap_blob(s20_lcd_t *lcd, int x, int y, const s20_bitmap_t *bmp, const uint8_t *blob, s20_draw_mode_t mode) {
    if (!bmp || !blob || bmp->width == 0 || bmp->height == 0) return;
    const uint8_t *data = &blob[bmp->data_offset];
    for (int by = 0; by < bmp->height; by++) {
        for (int bx = 0; bx < bmp->width; bx++) {
            uint8_t byte = data[(by >> 3) * bmp->width + bx];
            if (byte & (1u << (by & 7))) {
                s20_lcd_set_pixel(lcd, x + bx, y + by, mode);
            }
        }
    }
}

void s20_lcd_draw_bitmap_raw(s20_lcd_t *lcd, int x, int y, const s20_bitmap_t *bmp, s20_draw_mode_t mode) {
    s20_lcd_draw_bitmap_blob(lcd, x, y, bmp, s20_bitmap_data, mode);
}

void s20_lcd_draw_bitmap_id(s20_lcd_t *lcd, int x, int y, uint16_t bmp_id, s20_draw_mode_t mode) {
    const s20_bitmap_t *bmp = s20_get_bitmap(bmp_id);
    if (bmp) s20_lcd_draw_bitmap_raw(lcd, x, y, bmp, mode);
}

static int glyph_advance(const s20_glyph_t *g) {
    return g ? (int)g->width : 0;
}

int s20_text_width(s20_font_id_t font_id, const char *utf8) {
    if (!utf8) return 0;
    int max_w = 0, cur_w = 0;
    const char *p = utf8;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        if (cp == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            continue;
        }
        const s20_glyph_t *g = s20_find_glyph((int)font_id, cp, NULL);
        if (!g) g = s20_find_glyph((int)font_id, '?', NULL);
        cur_w += glyph_advance(g);
    }
    return (cur_w > max_w) ? cur_w : max_w;
}

int s20_text_first_line_width(s20_font_id_t font_id, const char *utf8) {
    if (!utf8) return 0;
    int w = 0;
    const char *p = utf8;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        if (cp == '\n') cp = ' ';
        const s20_glyph_t *g = s20_find_glyph((int)font_id, cp, NULL);
        if (!g) g = s20_find_glyph((int)font_id, '?', NULL);
        w += glyph_advance(g);
    }
    return w;
}

static int draw_glyph_clipped(s20_lcd_t *lcd, int x, int y,
                              int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                              s20_font_id_t font_id, uint16_t cp, s20_draw_mode_t mode) {
    int actual_font = (int)font_id;
    const s20_glyph_t *g = s20_find_glyph((int)font_id, cp, &actual_font);
    if (!g) g = s20_find_glyph((int)font_id, '?', &actual_font);
    if (!g) return 0;

    const uint16_t *cols = &s20_fonts[actual_font].col_data[g->data_offset];
    for (int cx = 0; cx < g->width; cx++) {
        int px = x + cx;
        if (px < clip_x0 || px >= clip_x1) continue;
        uint16_t col = cols[cx];
        for (int cy = 0; cy < g->height; cy++) {
            int py = y + cy;
            if (py < clip_y0 || py >= clip_y1) continue;
            if (col & (1u << cy)) {
                s20_lcd_set_pixel(lcd, px, py, mode);
            }
        }
    }
    return (int)g->width;
}

int s20_lcd_draw_text(s20_lcd_t *lcd, int x, int y, s20_font_id_t font_id,
                      const char *utf8, s20_draw_mode_t mode) {
    if (!utf8) return 0;
    int cx = x;
    int cy = y;
    int line_h = (font_id == S20_FONT_LARGE_BOLD) ? 13 : (font_id == S20_FONT_TINY_PLAIN ? 7 : 9);
    const char *p = utf8;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        if (cp == '\n') {
            cx = x;
            cy += line_h;
            continue;
        }
        cx += draw_glyph_clipped(lcd, cx, cy, 0, 0, S20_LCD_WIDTH, S20_LCD_HEIGHT,
                                 font_id, cp, mode);
    }
    return cx - x;
}

void s20_lcd_draw_text_aligned(s20_lcd_t *lcd, int rx, int ry, int rw, int rh,
                               s20_font_id_t font_id, const char *utf8,
                               s20_align_t align, s20_draw_mode_t mode,
                               int scroll_px, bool single_line) {
    if (!utf8 || rw <= 0 || rh <= 0) return;
    int total_w = single_line ? s20_text_first_line_width(font_id, utf8)
                              : s20_text_width(font_id, utf8);
    int x = rx;
    if (total_w > rw && single_line) {
        x = rx - scroll_px;
    } else if (align == S20_ALIGN_CENTER) {
        x = rx + (rw - total_w) / 2;
    } else if (align == S20_ALIGN_RIGHT) {
        x = rx + (rw - total_w);
    }

    int cx = x;
    int cy = ry;
    int line_h = (font_id == S20_FONT_LARGE_BOLD) ? 13 : (font_id == S20_FONT_TINY_PLAIN ? 7 : 9);
    const char *p = utf8;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        if (cp == '\n') {
            if (single_line) {
                cp = ' ';
            } else {
                cx = x;
                cy += line_h;
                continue;
            }
        }
        cx += draw_glyph_clipped(lcd, cx, cy, rx, ry, rx + rw, ry + rh,
                                 font_id, cp, mode);
    }
}

void s20_lcd_draw_scrollbar(s20_lcd_t *lcd, int x, int y, int h, int total, int current) {
    if (total <= 1 || h < 6) return;
    /* Vertical track line in center (x + 2) */
    for (int py = y; py < y + h; py++) {
        s20_lcd_set_pixel(lcd, x + 2, py, S20_DRAW_SET);
    }
    /* Proportional thumb (3px wide, at least 4px tall) */
    int thumb_h = h / total;
    if (thumb_h < 4) thumb_h = 4;
    if (thumb_h > h) thumb_h = h;
    int max_off = h - thumb_h;
    int thumb_y = y + ((current * max_off) / (total - 1));
    s20_lcd_fill_rect(lcd, x + 1, thumb_y, 3, thumb_h, S20_DRAW_SET);
}
