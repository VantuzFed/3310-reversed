#ifndef S20_PLATFORM_H
#define S20_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#define S20_LCD_WIDTH  84
#define S20_LCD_HEIGHT 48
#define S20_LCD_BANKS  (S20_LCD_HEIGHT / 8)
#define S20_FB_SIZE    (S20_LCD_WIDTH * S20_LCD_BANKS) /* 504 bytes */

typedef enum {
    S20_KEY_NONE = 0,
    S20_KEY_UP,
    S20_KEY_DOWN,
    S20_KEY_SELECT, /* Navi / Menu / OK */
    S20_KEY_BACK,   /* C / Back */
    S20_KEY_0,
    S20_KEY_1,
    S20_KEY_2,
    S20_KEY_3,
    S20_KEY_4,
    S20_KEY_5,
    S20_KEY_6,
    S20_KEY_7,
    S20_KEY_8,
    S20_KEY_9,
    S20_KEY_STAR,
    S20_KEY_HASH,
    S20_KEY_POWER,
    S20_KEY_LEFT,
    S20_KEY_RIGHT,
    S20_KEY_OP_ADD,
    S20_KEY_OP_SUB,
    S20_KEY_OP_MUL,
    S20_KEY_OP_DIV,
    S20_KEY_OP_DOT,
    S20_KEY_OP_EQ
} s20_key_t;

typedef struct {
    void (*present)(const uint8_t fb[S20_FB_SIZE], bool backlight, void *user);
    uint32_t (*get_ticks_ms)(void *user);
    bool (*get_local_time)(uint8_t *hour, uint8_t *minute, uint8_t *second, void *user);
    void (*audio_lock)(void *user);
    void (*audio_unlock)(void *user);
    void *user;
} s20_hal_t;

#endif /* S20_PLATFORM_H */
