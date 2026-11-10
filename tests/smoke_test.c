#include "s20_menu.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t fake_ticks = 0;
static uint8_t fake_hour = 17, fake_min = 35, fake_sec = 6;

static uint32_t test_get_ticks(void *user) {
    (void)user;
    return fake_ticks;
}

static bool test_get_local_time(uint8_t *hour, uint8_t *minute, uint8_t *second, void *user) {
    (void)user;
    if (hour) *hour = fake_hour;
    if (minute) *minute = fake_min;
    if (second) *second = fake_sec;
    return true;
}

static int present_calls = 0;
static void test_present(const uint8_t fb[S20_FB_SIZE], bool backlight, void *user) {
    (void)fb;
    (void)backlight;
    (void)user;
    present_calls++;
}

static void dump_ascii(const char *label, const uint8_t fb[S20_FB_SIZE]) {
    printf("=== %s ===\n", label);
    for (int y = 0; y < S20_LCD_HEIGHT; y++) {
        putchar('|');
        for (int x = 0; x < S20_LCD_WIDTH; x++) {
            uint8_t b = fb[(y >> 3) * S20_LCD_WIDTH + x];
            putchar((b & (1u << (y & 7))) ? '#' : ' ');
        }
        puts("|");
    }
}

static int count_lit_pixels(const uint8_t fb[S20_FB_SIZE]) {
    int n = 0;
    for (int i = 0; i < S20_FB_SIZE; i++) {
        uint8_t b = fb[i];
        while (b) {
            n += (b & 1);
            b >>= 1;
        }
    }
    return n;
}

int main(void) {
    s20_hal_t hal = {
        .present = test_present,
        .get_ticks_ms = test_get_ticks,
        .get_local_time = test_get_local_time,
        .user = NULL,
    };
    s20_ui_t ui;
    s20_ui_init(&ui, &hal);

    /* 1. Splash -> Idle */
    assert(ui.screen == S20_SCREEN_SPLASH);
    s20_ui_tick(&ui);
    assert(count_lit_pixels(ui.lcd.fb) > 50);

    fake_ticks = 2000;
    s20_ui_tick(&ui);
    assert(ui.screen == S20_SCREEN_IDLE);
    dump_ascii("IDLE SCREEN (EN)", ui.lcd.fb);

    uint8_t prev_fb[S20_FB_SIZE];
    memcpy(prev_fb, ui.lcd.fb, S20_FB_SIZE);
    fake_hour = 8;
    fake_min = 9;
    s20_ui_tick(&ui);
    assert(memcmp(prev_fb, ui.lcd.fb, S20_FB_SIZE) != 0);

    /* 2. Open Main Menu -> item 1 (Phone book) -> down to item 2 (Messages) */
    s20_ui_handle_key(&ui, S20_KEY_SELECT);
    assert(ui.screen == S20_SCREEN_MENU);
    assert(ui.stack_depth == 1);
    assert(ui.stack[0].menu_id == 0);
    assert(ui.stack[0].selected == 1);

    s20_ui_handle_key(&ui, S20_KEY_DOWN);
    assert(ui.stack[0].selected == 2);
    fake_ticks += 250;
    s20_ui_tick(&ui);
    dump_ascii("MAIN MENU: 2 Messages", ui.lcd.fb);

    /* 3. Enter Messages submenu */
    s20_ui_handle_key(&ui, S20_KEY_SELECT);
    assert(ui.stack_depth == 2);
    s20_ui_tick(&ui);
    dump_ascii("SUBMENU 2-1 Messages", ui.lcd.fb);

    /* 4. Switch to Russian and check Cyrillic rendering */
    s20_ui_set_language(&ui, S20_LANG_RU);
    s20_ui_tick(&ui);
    dump_ascii("SUBMENU 2-1 Messages (RU)", ui.lcd.fb);

    /* 5. Back twice -> Idle */
    s20_ui_handle_key(&ui, S20_KEY_BACK);
    assert(ui.stack_depth == 1);
    s20_ui_handle_key(&ui, S20_KEY_BACK);
    assert(ui.screen == S20_SCREEN_IDLE);

    /* 6. Test Calculator (12.5 * 4 = 50 via '#' & '*', then 50 / 2 = 25 via '/' & '=') */
    s20_ui_handle_key(&ui, S20_KEY_SELECT);
    s20_ui_handle_key(&ui, S20_KEY_9);
    assert(ui.screen == S20_SCREEN_CALCULATOR);
    s20_ui_handle_key(&ui, S20_KEY_1);
    s20_ui_handle_key(&ui, S20_KEY_2);
    s20_ui_handle_key(&ui, S20_KEY_HASH); /* '.' */
    s20_ui_handle_key(&ui, S20_KEY_5);
    s20_ui_handle_key(&ui, S20_KEY_STAR); /* '+' */
    s20_ui_handle_key(&ui, S20_KEY_STAR); /* '-' */
    s20_ui_handle_key(&ui, S20_KEY_STAR); /* '*' */
    assert(ui.calc_has_op && ui.calc_op == 2);
    s20_ui_handle_key(&ui, S20_KEY_4);
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Options menu -> Equals (cursor 0) */
    assert(ui.screen == S20_SCREEN_CALC_OPTIONS);
    s20_ui_tick(&ui);
    dump_ascii("CALC OPTIONS", ui.lcd.fb);
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Execute Equals */
    assert(ui.screen == S20_SCREEN_CALCULATOR);
    assert(fabs(ui.calc_cur - 50.0) < 1e-6);

    s20_ui_handle_key(&ui, S20_KEY_OP_DIV);
    s20_ui_handle_key(&ui, S20_KEY_2);
    s20_ui_handle_key(&ui, S20_KEY_OP_EQ);
    assert(fabs(ui.calc_cur - 25.0) < 1e-6);

    /* 7. Test Tones menu (5 -> 1 Ringing tone) + REPS audio synthesis across all 35 tones */
    while (ui.screen != S20_SCREEN_IDLE) {
        s20_ui_handle_key(&ui, S20_KEY_BACK);
    }
    assert(ui.screen == S20_SCREEN_IDLE);
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Main menu */
    s20_ui_handle_key(&ui, S20_KEY_5);      /* 5 Tones */
    s20_ui_handle_key(&ui, S20_KEY_1);      /* 5-1 Ringing tone */
    assert(ui.screen == S20_SCREEN_TONE_LIST);
    assert(s20_audio_is_playing(&ui.audio));
    s20_ui_tick(&ui);
    dump_ascii("TONES 5-1 Ringing tone", ui.lcd.fb);

    int16_t pcm[4096];
    for (int t = 0; t < S20_NUM_TONES; t++) {
        s20_audio_play_tone(&ui.audio, t, false);
        assert(s20_audio_is_playing(&ui.audio));
        s20_audio_mix(&ui.audio, pcm, 4096);
        int non_zero = 0;
        for (int i = 0; i < 4096; i++) {
            if (pcm[i] != 0) non_zero++;
        }
        assert(non_zero > 100);
    }

    s20_ui_handle_key(&ui, S20_KEY_BACK);
    assert(ui.screen == S20_SCREEN_MENU);
    s20_ui_handle_key(&ui, S20_KEY_4);      /* 5-4 Composer */
    assert(ui.screen == S20_SCREEN_COMPOSER);
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Play composer notes */
    assert(s20_audio_is_playing(&ui.audio));

    /* 8. Test Games subsystem (Menu -> 8 Games -> Snake II, Space Impact, Bantumi, Pairs II) */
    while (ui.screen != S20_SCREEN_IDLE) {
        s20_ui_handle_key(&ui, S20_KEY_BACK);
    }
    s20_ui_handle_key(&ui, S20_KEY_SELECT);
    s20_ui_handle_key(&ui, S20_KEY_8); /* 8 Games */
    assert(ui.screen == S20_SCREEN_GAMES);
    s20_ui_tick(&ui);
    assert(count_lit_pixels(ui.lcd.fb) > 50);

    /* 8a. Snake II */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Snake II menu */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* New game */
    assert(ui.games.view == S20_GVIEW_PLAYING && ui.games.game == S20_GAME_SNAKE);
    fake_ticks += 300;
    s20_ui_tick(&ui);
    s20_ui_handle_key(&ui, S20_KEY_DOWN);
    fake_ticks += 300;
    s20_ui_tick(&ui);
    dump_ascii("GAME: SNAKE II", ui.lcd.fb);
    assert(count_lit_pixels(ui.lcd.fb) > 100);
    s20_ui_handle_key(&ui, S20_KEY_BACK);
    s20_ui_handle_key(&ui, S20_KEY_BACK);

    /* 8b. Space Impact */
    s20_ui_handle_key(&ui, S20_KEY_DOWN);   /* Space impact */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Game menu */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* New game */
    assert(ui.games.view == S20_GVIEW_PLAYING && ui.games.game == S20_GAME_SPACE);
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Fire main */
    s20_ui_handle_key(&ui, S20_KEY_4);      /* Fire bonus missile */
    for (int f = 0; f < 10; f++) {
        fake_ticks += 70;
        s20_ui_tick(&ui);
    }
    dump_ascii("GAME: SPACE IMPACT", ui.lcd.fb);
    assert(ui.games.si_wave_tick >= 10 && count_lit_pixels(ui.lcd.fb) > 50);
    s20_ui_handle_key(&ui, S20_KEY_BACK);
    s20_ui_handle_key(&ui, S20_KEY_BACK);

    /* 8c. Bantumi */
    s20_ui_handle_key(&ui, S20_KEY_DOWN);   /* Bantumi */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Game menu */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* 1 player */
    assert(ui.games.view == S20_GVIEW_PLAYING && ui.games.game == S20_GAME_BANTUMI);
    s20_ui_handle_key(&ui, S20_KEY_RIGHT);
    s20_ui_handle_key(&ui, S20_KEY_SELECT);
    fake_ticks += 500;
    s20_ui_tick(&ui);
    int total_beans = 0;
    for (int p = 0; p < 14; p++) total_beans += ui.games.bn_pits[p];
    assert(total_beans == 48);
    dump_ascii("GAME: BANTUMI", ui.lcd.fb);
    s20_ui_handle_key(&ui, S20_KEY_BACK);
    s20_ui_handle_key(&ui, S20_KEY_BACK);

    /* 8d. Pairs II */
    s20_ui_handle_key(&ui, S20_KEY_DOWN);   /* Pairs II */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Game menu */
    s20_ui_handle_key(&ui, S20_KEY_SELECT); /* Time trial */
    assert(ui.games.view == S20_GVIEW_PLAYING && ui.games.game == S20_GAME_PAIRS);
    s20_ui_handle_key(&ui, S20_KEY_5);
    s20_ui_handle_key(&ui, S20_KEY_RIGHT);
    s20_ui_handle_key(&ui, S20_KEY_5);
    fake_ticks += 1000;
    s20_ui_tick(&ui);
    dump_ascii("GAME: PAIRS II", ui.lcd.fb);
    assert(ui.games.pr_tries == 1 && count_lit_pixels(ui.lcd.fb) > 100);

    printf("All smoke tests passed! tones=%d game_sprites=%d present_calls=%d\n",
           S20_NUM_TONES,
           S20_SNAKE_SPRITE_CNT + S20_SI_SPRITE_CNT + S20_BANTUMI_SPRITE_CNT +
               S20_PAIRS_FLIP_CNT + S20_PAIRS_CARD_CNT + S20_PAIRS_MISC_CNT,
           present_calls);
    return 0;
}
