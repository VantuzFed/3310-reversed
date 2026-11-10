#ifndef S20_MENU_H
#define S20_MENU_H

#include "s20_assets.h"
#include "s20_audio.h"
#include "s20_games.h"
#include "s20_lcd.h"
#include "s20_platform.h"
#include <stdbool.h>
#include <stdint.h>

#define S20_MAX_MENU_STACK 8
#define S20_COMP_MAX_NOTES 32

typedef enum {
    S20_SCREEN_SPLASH = 0,
    S20_SCREEN_IDLE,
    S20_SCREEN_MENU,
    S20_SCREEN_LANGUAGE,
    S20_SCREEN_TONE_LIST,
    S20_SCREEN_TONE_VOLUME,
    S20_SCREEN_TONE_ALERT,
    S20_SCREEN_COMPOSER,
    S20_SCREEN_CALCULATOR,
    S20_SCREEN_CALC_OPTIONS,
    S20_SCREEN_CALC_RATE,
    S20_SCREEN_GAMES,
    S20_SCREEN_STOPWATCH,
    S20_SCREEN_POPUP
} s20_screen_t;

typedef enum {
    S20_ALERT_CALL = 0,
    S20_ALERT_MSG = 1,
    S20_ALERT_KEYPAD = 2
} s20_alert_mode_t;

typedef struct {
    int16_t menu_id;
    uint8_t selected; /* 1..count */
    uint8_t scroll_top;
} s20_menu_frame_t;

typedef struct {
    uint8_t pitch;   /* 0=rest, 1..7=c..b */
    uint8_t octave;  /* 1..3 */
    bool sharp;
    uint8_t dur_div; /* 4, 8, 16, 2 */
} s20_comp_note_t;

typedef struct {
    s20_lcd_t lcd;
    s20_audio_t audio;
    const s20_hal_t *hal;
    s20_screen_t screen;
    s20_lang_t lang;
    bool backlight;
    bool keyguard;
    bool netmonitor_enabled;
    uint8_t signal_level;  /* 0..7 */
    uint8_t battery_level; /* 0..7 */

    /* Sound / Tones settings */
    uint8_t active_tone_idx; /* 0..S20_NUM_TONES-1 (12 = Nokia tune) */
    uint8_t tone_volume;     /* 1..5 */
    uint8_t call_alert;      /* 0..4 */
    uint8_t msg_alert;       /* 0..4 */
    uint8_t keypad_level;    /* 0..3 */
    bool warning_tones;

    /* Tones UI state */
    int tone_cursor;
    int tone_scroll;
    uint8_t vol_cursor;
    s20_alert_mode_t alert_mode;
    int alert_cursor;
    int alert_scroll;

    /* Composer state */
    s20_comp_note_t comp_notes[S20_COMP_MAX_NOTES];
    int comp_len;
    uint8_t comp_oct;
    uint8_t comp_dur_div;
    bool comp_sharp;

    /* Menu stack */
    s20_menu_frame_t stack[S20_MAX_MENU_STACK];
    int stack_depth;
    uint8_t radio_state[S20_NUM_MENUS];

    /* Animation & marquee timers */
    uint32_t last_anim_tick;
    uint8_t anim_frame;
    uint32_t menu_enter_tick;

    /* Numeric shortcut buffer */
    char dial_buf[16];
    uint8_t dial_len;

    /* Popup state */
    char popup_title[64];
    char popup_body[128];
    uint16_t popup_bmp_id;
    s20_screen_t popup_return_screen;
    uint32_t popup_expire_tick;

    /* Language selector state */
    int lang_cursor;

    /* Calculator state */
    double calc_acc;
    double calc_cur;
    double calc_rate;
    char calc_buf[16];
    uint8_t calc_len;
    uint8_t calc_op; /* 0=+, 1=-, 2=*, 3=/ */
    bool calc_has_acc;
    bool calc_has_op;
    bool calc_fresh_result;
    bool calc_error;
    int calc_opt_cursor;

    /* Games subsystem */
    s20_games_t games;

    /* Stopwatch state */
    bool sw_running;
    uint32_t sw_accum_ms;
    uint32_t sw_start_tick;
} s20_ui_t;

void s20_ui_init(s20_ui_t *ui, const s20_hal_t *hal);
void s20_ui_tick(s20_ui_t *ui);
void s20_ui_handle_key(s20_ui_t *ui, s20_key_t key);
void s20_ui_render(s20_ui_t *ui);
void s20_ui_set_language(s20_ui_t *ui, s20_lang_t lang);

#endif /* S20_MENU_H */
