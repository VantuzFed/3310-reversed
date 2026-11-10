#ifndef S20_ASSETS_H
#define S20_ASSETS_H

#include <stdint.h>

#define S20_NUM_STRINGS 1384
#define S20_NUM_FONTS   4
#define S20_NUM_BITMAPS 378
#define S20_NUM_BMP_IDS 384
#define S20_NUM_ANIMS   36
#define S20_NUM_MENUS   28
#define S20_NUM_TONE_FREQS 102
#define S20_NUM_TONES   35

typedef enum {
    S20_FONT_LARGE_BOLD = 0,
    S20_FONT_SMALL_PLAIN = 1,
    S20_FONT_SMALL_BOLD = 2,
    S20_FONT_TINY_PLAIN = 3
} s20_font_id_t;

typedef enum {
    S20_LANG_EN = 0,
    S20_LANG_RU = 1
} s20_lang_t;

typedef struct {
    uint16_t codepoint;
    uint8_t width;
    uint8_t height;
    uint8_t ascent;
    uint32_t data_offset;
} s20_glyph_t;

typedef struct {
    const char *name;
    int8_t fallback;
    uint16_t glyph_count;
    const s20_glyph_t *glyphs;
    const uint16_t *col_data;
} s20_font_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint32_t data_offset;
} s20_bitmap_t;

typedef struct {
    uint16_t delay;
    uint8_t mode;
    uint8_t frame_count;
    const uint16_t *frames;
} s20_anim_t;

typedef struct {
    uint16_t text_id;
    uint32_t arg;
    uint16_t hnd_ctx;
    uint16_t hnd_fn;
    uint16_t u12;
    uint32_t flags;
    uint8_t anim_id;
    int16_t submenu_id;
} s20_menu_item_t;

typedef struct {
    uint8_t count;
    uint8_t f1;
    uint16_t f2;
    uint32_t state_ptr;
    const s20_menu_item_t *items;
} s20_menu_t;

typedef struct {
    uint8_t id;
    const char *name;
    uint16_t len;
    const uint8_t *data;
} s20_tone_t;

extern const char * const s20_strings_en[S20_NUM_STRINGS];
extern const char * const s20_strings_ru[S20_NUM_STRINGS];
extern const s20_font_t s20_fonts[S20_NUM_FONTS];
extern const uint16_t s20_bitmap_id_map[S20_NUM_BMP_IDS];
extern const s20_bitmap_t s20_bitmaps[S20_NUM_BITMAPS];
extern const uint8_t s20_bitmap_data[];
extern const s20_anim_t s20_anims[S20_NUM_ANIMS];
extern const s20_menu_t s20_menus[S20_NUM_MENUS];
extern const uint16_t s20_tone_freqs[S20_NUM_TONE_FREQS];
extern const s20_tone_t s20_tones[S20_NUM_TONES];

const char *s20_get_string(uint16_t text_id, s20_lang_t lang);
const s20_glyph_t *s20_find_glyph(int font_id, uint16_t cp, int *out_font_id);
const s20_bitmap_t *s20_get_bitmap(uint16_t bmp_id);

#endif /* S20_ASSETS_H */
