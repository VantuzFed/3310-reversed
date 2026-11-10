#include "s20_games.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Localized PPM text IDs */
#define TID_SELECT    0x0424
#define TID_BACK      0x040E
#define TID_OK        0x041F
#define TID_GAMES     0x0258
#define TID_SNAKE2    0x0271
#define TID_SPACE     0x0272
#define TID_BANTUMI   0x0254
#define TID_PAIRS2    0x0269
#define TID_SETTINGS  0x0265
#define TID_NEW_GAME  0x0263
#define TID_CONTINUE  0x025B
#define TID_1PLAYER   0x0259
#define TID_2PLAYERS  0x025A
#define TID_TIMED     0x0256
#define TID_PUZZLE    0x0257
#define TID_LEVEL     0x025F
#define TID_LEVEL_HDR 0x024D
#define TID_MAZES     0x0248
#define TID_NO_MAZE   0x0253
#define TID_MAZE1     0x024E
#define TID_TOPSCORE  0x025C
#define TID_INSTR     0x025D
#define TID_SOUNDS    0x0266
#define TID_LIGHTS    0x0260
#define TID_SHAKES    0x0341
#define TID_ON        0x0268
#define TID_OFF       0x0267
#define TID_TOP_FMT   0x024C
#define TID_GO_TOP    0x0273
#define TID_GO_LOST   0x0274
#define TID_GO_SCORE  0x0275
#define TID_GO_WON    0x0276

#define TID_INSTR_BANTUMI     0x0563
#define TID_INSTR_PAIRS_TIMED 0x0564
#define TID_INSTR_PAIRS_PUZZ  0x0565
#define TID_INSTR_SNAKE       0x0566
#define TID_INSTR_SPACE       0x0567

typedef enum {
    ACT_CONTINUE = 0,
    ACT_NEW_GAME,
    ACT_1PLAYER,
    ACT_2PLAYERS,
    ACT_TIMED,
    ACT_PUZZLE,
    ACT_LEVEL,
    ACT_MAZES,
    ACT_TOPSCORE,
    ACT_INSTR
} gmenu_act_t;

typedef struct {
    uint16_t tid;
    gmenu_act_t act;
} gmenu_entry_t;

static uint32_t next_rng(s20_games_t *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    if (g->rng == 0) g->rng = 0x12345678u;
    return g->rng;
}

static void game_beep(const s20_games_t *g, s20_audio_t *aud, uint8_t vol, uint16_t freq, uint16_t dur_ms) {
    if (!g->sounds || !aud || vol == 0) return;
    s20_audio_beep(aud, freq, dur_ms, vol);
}

static void draw_sprite(s20_lcd_t *lcd, int x, int y, const s20_bitmap_t *bmp, s20_draw_mode_t mode) {
    s20_lcd_draw_bitmap_blob(lcd, x, y, bmp, s20_games_bmp_data, mode);
}

static void format_tid_num(char *dst, size_t cap, uint16_t tid, s20_lang_t lang, int32_t num) {
    const char *tpl = s20_get_string(tid, lang);
    size_t di = 0;
    for (size_t si = 0; tpl[si] && di + 1 < cap; si++) {
        if (tpl[si] == '%' && (tpl[si + 1] == 'N' || tpl[si + 1] == 'U')) {
            char nb[24];
            snprintf(nb, sizeof(nb), "%ld", (long)num);
            for (size_t k = 0; nb[k] && di + 1 < cap; k++) dst[di++] = nb[k];
            si++;
        } else {
            dst[di++] = tpl[si];
        }
    }
    dst[di] = '\0';
}

static int build_game_menu(const s20_games_t *g, gmenu_entry_t out[8]) {
    int n = 0;
    if (g->paused[g->game]) {
        out[n++] = (gmenu_entry_t){ TID_CONTINUE, ACT_CONTINUE };
    }
    switch (g->game) {
    case S20_GAME_SNAKE:
        out[n++] = (gmenu_entry_t){ TID_NEW_GAME, ACT_NEW_GAME };
        out[n++] = (gmenu_entry_t){ TID_LEVEL, ACT_LEVEL };
        out[n++] = (gmenu_entry_t){ TID_MAZES, ACT_MAZES };
        out[n++] = (gmenu_entry_t){ TID_TOPSCORE, ACT_TOPSCORE };
        out[n++] = (gmenu_entry_t){ TID_INSTR, ACT_INSTR };
        break;
    case S20_GAME_SPACE:
        out[n++] = (gmenu_entry_t){ TID_NEW_GAME, ACT_NEW_GAME };
        out[n++] = (gmenu_entry_t){ TID_TOPSCORE, ACT_TOPSCORE };
        out[n++] = (gmenu_entry_t){ TID_INSTR, ACT_INSTR };
        break;
    case S20_GAME_BANTUMI:
        out[n++] = (gmenu_entry_t){ TID_1PLAYER, ACT_1PLAYER };
        out[n++] = (gmenu_entry_t){ TID_2PLAYERS, ACT_2PLAYERS };
        out[n++] = (gmenu_entry_t){ TID_LEVEL, ACT_LEVEL };
        out[n++] = (gmenu_entry_t){ TID_TOPSCORE, ACT_TOPSCORE };
        out[n++] = (gmenu_entry_t){ TID_INSTR, ACT_INSTR };
        break;
    case S20_GAME_PAIRS:
        out[n++] = (gmenu_entry_t){ TID_TIMED, ACT_TIMED };
        out[n++] = (gmenu_entry_t){ TID_PUZZLE, ACT_PUZZLE };
        out[n++] = (gmenu_entry_t){ TID_LEVEL, ACT_LEVEL };
        out[n++] = (gmenu_entry_t){ TID_TOPSCORE, ACT_TOPSCORE };
        out[n++] = (gmenu_entry_t){ TID_INSTR, ACT_INSTR };
        break;
    }
    return n;
}

static int game_score_slot(const s20_games_t *g) {
    if (g->game == S20_GAME_PAIRS) return g->pairs_timed ? 3 : 4;
    return (int)g->game;
}

static void finish_game(s20_games_t *g, int32_t score, bool won, s20_audio_t *aud, uint8_t vol) {
    int slot = game_score_slot(g);
    g->paused[g->game] = false;
    g->last_score = score;
    g->won_game = won;
    g->new_record = (score > g->top_score[slot]);
    if (g->new_record) g->top_score[slot] = score;
    g->view = S20_GVIEW_GAMEOVER;
    game_beep(g, aud, vol, won ? 1319 : 220, won ? 180 : 220);
}

/* =========================================================================
 * 1. SNAKE II (20x9 grid, 6 ROM mazes, 39 ROM sprites, 9 ROM speeds)
 * ========================================================================= */

static bool snake_is_wall(const s20_games_t *g, int x, int y) {
    const s20_snake_maze_t *mz = &s20_snake_mazes[g->snake_maze % 6];
    for (int i = 0; i < mz->seg_count; i++) {
        int x0 = mz->segs[i].x0, y0 = mz->segs[i].y0;
        int x1 = mz->segs[i].x1, y1 = mz->segs[i].y1;
        if (x0 == 255 || x1 == 255) continue;
        int lo_x = x0 < x1 ? x0 : x1, hi_x = x0 > x1 ? x0 : x1;
        int lo_y = y0 < y1 ? y0 : y1, hi_y = y0 > y1 ? y0 : y1;
        if (x >= lo_x && x <= hi_x && y >= lo_y && y <= hi_y) return true;
    }
    return false;
}

static bool snake_occupies(const s20_games_t *g, int x, int y, bool check_tail) {
    int end = check_tail ? g->sn_len : g->sn_len - 1;
    for (int i = 0; i < end; i++) {
        if (g->sn_body[i].x == x && g->sn_body[i].y == y) return true;
    }
    return false;
}

static void snake_spawn_food(s20_games_t *g) {
    for (int tries = 0; tries < 256; tries++) {
        int x = (int)(next_rng(g) % 20);
        int y = (int)(next_rng(g) % 9);
        if (!snake_is_wall(g, x, y) && !snake_occupies(g, x, y, true) &&
            (!g->sn_bonus_active || ((x != g->sn_bonus.x && x != g->sn_bonus.x + 1) || y != g->sn_bonus.y))) {
            g->sn_food.x = (int8_t)x;
            g->sn_food.y = (int8_t)y;
            return;
        }
    }
}

static void snake_spawn_bonus(s20_games_t *g) {
    for (int tries = 0; tries < 128; tries++) {
        int x = (int)(next_rng(g) % 19);
        int y = (int)(next_rng(g) % 9);
        if (!snake_is_wall(g, x, y) && !snake_is_wall(g, x + 1, y) &&
            !snake_occupies(g, x, y, true) && !snake_occupies(g, x + 1, y, true) &&
            !(g->sn_food.y == y && (g->sn_food.x == x || g->sn_food.x == x + 1))) {
            g->sn_bonus.x = (int8_t)x;
            g->sn_bonus.y = (int8_t)y;
            g->sn_bonus_kind = (uint8_t)(next_rng(g) % 5);
            g->sn_bonus_timer = 20;
            g->sn_bonus_active = true;
            return;
        }
    }
}

static void snake_start(s20_games_t *g, uint32_t now_ms) {
    const s20_snake_maze_t *mz = &s20_snake_mazes[g->snake_maze % 6];
    g->sn_len = 6;
    g->sn_dir = 0;
    g->sn_qlen = 0;
    memset(g->sn_belly, 0, sizeof(g->sn_belly));
    int hx = (int)mz->sx + 4;
    if (hx >= 18) hx = 8;
    for (int i = 0; i < g->sn_len; i++) {
        int sx = (hx - i + 20) % 20;
        g->sn_body[i].x = (int8_t)sx;
        g->sn_body[i].y = (int8_t)mz->sy;
    }
    g->sn_food.x = (int8_t)(mz->fx < 20 ? mz->fx : 14);
    g->sn_food.y = (int8_t)(mz->fy < 9 ? mz->fy : 3);
    if (snake_is_wall(g, g->sn_food.x, g->sn_food.y) ||
        snake_occupies(g, g->sn_food.x, g->sn_food.y, true)) {
        snake_spawn_food(g);
    }
    g->sn_bonus_active = false;
    g->sn_food_eaten = 0;
    g->sn_score = 0;
    g->sn_last_step = now_ms;
    g->paused[S20_GAME_SNAKE] = true;
    g->view = S20_GVIEW_PLAYING;
}

static void snake_queue_dir(s20_games_t *g, uint8_t d) {
    uint8_t ref = (g->sn_qlen > 0) ? g->sn_qdir[g->sn_qlen - 1] : g->sn_dir;
    if (d == ref || ((d ^ 2) == ref)) return; /* ignore same/reverse */
    if (g->sn_qlen < 2) g->sn_qdir[g->sn_qlen++] = d;
}

static void snake_step(s20_games_t *g, s20_audio_t *aud, uint8_t vol) {
    if (g->sn_qlen > 0) {
        g->sn_dir = g->sn_qdir[0];
        g->sn_qdir[0] = g->sn_qdir[1];
        g->sn_qlen--;
    }
    static const int dx[4] = { 1, 0, -1, 0 };
    static const int dy[4] = { 0, 1, 0, -1 };
    int nx = (g->sn_body[0].x + dx[g->sn_dir] + 20) % 20;
    int ny = (g->sn_body[0].y + dy[g->sn_dir] + 9) % 9;

    bool ate_food = (nx == g->sn_food.x && ny == g->sn_food.y);
    bool ate_bonus = g->sn_bonus_active && ny == g->sn_bonus.y &&
                     (nx == g->sn_bonus.x || nx == g->sn_bonus.x + 1);
    bool grew = g->sn_belly[g->sn_len - 1];

    if (snake_is_wall(g, nx, ny) || snake_occupies(g, nx, ny, grew)) {
        finish_game(g, g->sn_score, false, aud, vol);
        return;
    }

    if (grew && g->sn_len < 179) {
        g->sn_body[g->sn_len] = g->sn_body[g->sn_len - 1];
        g->sn_belly[g->sn_len] = false;
        g->sn_len++;
    }
    for (int i = g->sn_len - 1; i > 0; i--) {
        g->sn_body[i] = g->sn_body[i - 1];
        g->sn_belly[i] = g->sn_belly[i - 1];
    }
    g->sn_body[0].x = (int8_t)nx;
    g->sn_body[0].y = (int8_t)ny;
    g->sn_belly[0] = (ate_food || ate_bonus);

    if (g->sn_bonus_active) {
        if (ate_bonus) {
            g->sn_score += (int32_t)(g->snake_level * 5 + g->sn_bonus_timer * 2);
            g->sn_bonus_active = false;
            game_beep(g, aud, vol, 1568, 55);
        } else if (g->sn_bonus_timer > 0) {
            g->sn_bonus_timer--;
            if (g->sn_bonus_timer == 0) g->sn_bonus_active = false;
        }
    }
    if (ate_food) {
        g->sn_score += (int32_t)g->snake_level;
        g->sn_food_eaten++;
        game_beep(g, aud, vol, 1175, 30);
        if (g->sn_len >= 178) {
            finish_game(g, g->sn_score, true, aud, vol);
            return;
        }
        snake_spawn_food(g);
        if ((g->sn_food_eaten % 5) == 0 && !g->sn_bonus_active) {
            snake_spawn_bonus(g);
        }
    }
}

static uint8_t snake_dir_between(s20_pt8_t a, s20_pt8_t b) {
    int dx = b.x - a.x;
    int dy = b.y - a.y;
    if (dx == 1 || dx < -1) return 0;
    if (dx == -1 || dx > 1) return 2;
    if (dy == 1 || dy < -1) return 1;
    return 3;
}

static void snake_render(const s20_games_t *g, s20_lcd_t *lcd) {
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%04ld", (long)g->sn_score);
    s20_lcd_draw_text(lcd, 2, 1, S20_FONT_TINY_PLAIN, sbuf, S20_DRAW_SET);
    s20_lcd_fill_rect(lcd, 0, 8, 84, 1, S20_DRAW_SET);
    if (g->snake_maze == 0) {
        /* Dotted border in No-maze wrap-around mode */
        for (int x = 1; x <= 82; x += 2) {
            s20_lcd_set_pixel(lcd, x, 9, S20_DRAW_SET);
            s20_lcd_set_pixel(lcd, x, 46, S20_DRAW_SET);
        }
        for (int y = 9; y <= 46; y += 2) {
            s20_lcd_set_pixel(lcd, 1, y, S20_DRAW_SET);
            s20_lcd_set_pixel(lcd, 82, y, S20_DRAW_SET);
        }
    } else {
        s20_lcd_draw_rect_outline(lcd, 1, 9, 82, 38, S20_DRAW_SET);
    }

    if (g->sn_bonus_active) {
        draw_sprite(lcd, 56, 2, &s20_snake_sprites[1 + g->sn_bonus_kind], S20_DRAW_SET);
        snprintf(sbuf, sizeof(sbuf), "%02u", (unsigned)g->sn_bonus_timer);
        s20_lcd_draw_text(lcd, 67, 1, S20_FONT_TINY_PLAIN, sbuf, S20_DRAW_SET);
    }

    for (int gy = 0; gy < 9; gy++) {
        for (int gx = 0; gx < 20; gx++) {
            if (snake_is_wall(g, gx, gy)) {
                int px = 2 + gx * 4, py = 10 + gy * 4;
                s20_lcd_fill_rect(lcd, px, py, 4, 4, S20_DRAW_SET);
                s20_lcd_set_pixel(lcd, px + 1, py + 1, S20_DRAW_CLR);
                s20_lcd_set_pixel(lcd, px + 2, py + 2, S20_DRAW_CLR);
            }
        }
    }

    /* Food */
    draw_sprite(lcd, 2 + g->sn_food.x * 4, 10 + g->sn_food.y * 4, &s20_snake_sprites[0], S20_DRAW_SET);
    if (g->sn_bonus_active) {
        draw_sprite(lcd, 2 + g->sn_bonus.x * 4, 10 + g->sn_bonus.y * 4,
                    &s20_snake_sprites[1 + g->sn_bonus_kind], S20_DRAW_SET);
    }

    /* Snake segments: directions 0=R, 1=D, 2=L, 3=U */
    static const uint8_t head_spr[4]  = { 17, 16, 15, 18 };
    static const uint8_t mouth_spr[4] = { 19, 20, 21, 22 };
    static const uint8_t tail_spr[4]  = { 33, 32, 31, 34 };
    static const int dx[4] = { 1, 0, -1, 0 };
    static const int dy[4] = { 0, 1, 0, -1 };
    int nx = (g->sn_body[0].x + dx[g->sn_dir] + 20) % 20;
    int ny = (g->sn_body[0].y + dy[g->sn_dir] + 9) % 9;
    bool mouth_open = (nx == g->sn_food.x && ny == g->sn_food.y) ||
                      (g->sn_bonus_active && ny == g->sn_bonus.y && (nx == g->sn_bonus.x || nx == g->sn_bonus.x + 1));

    for (int i = 0; i < g->sn_len; i++) {
        int px = 2 + g->sn_body[i].x * 4;
        int py = 10 + g->sn_body[i].y * 4;
        int spr = 23;
        if (i == 0) {
            spr = mouth_open ? mouth_spr[g->sn_dir & 3] : head_spr[g->sn_dir & 3];
        } else if (i == g->sn_len - 1) {
            uint8_t d = snake_dir_between(g->sn_body[i], g->sn_body[i - 1]);
            spr = tail_spr[d & 3];
        } else {
            uint8_t d = snake_dir_between(g->sn_body[i], g->sn_body[i - 1]);
            spr = (g->sn_belly[i] ? 27 : 23) + (int)d;
        }
        draw_sprite(lcd, px, py, &s20_snake_sprites[spr], S20_DRAW_SET);
    }
}

/* =========================================================================
 * 2. SPACE IMPACT (8 stages, ROM bosses, terrains, sine paths & sprites)
 * ========================================================================= */

static const uint8_t si_boss_spr[8] = { 22, 26, 30, 34, 38, 42, 46, 50 };
static const uint8_t si_enemy_spr[6] = { 54, 59, 61, 63, 68, 72 };

static void si_spawn_fx(s20_games_t *g, int x, int y) {
    for (int i = 0; i < 8; i++) {
        if (!g->si_fx[i].active) {
            g->si_fx[i].active = true;
            g->si_fx[i].x = x;
            g->si_fx[i].y = y;
            g->si_fx[i].frame = 0;
            g->si_fx[i].timer = 6;
            return;
        }
    }
}

static void si_spawn_shot(s20_games_t *g, float x, float y, float vx, float vy, uint8_t kind) {
    for (int i = 0; i < 24; i++) {
        if (!g->si_shots[i].active) {
            g->si_shots[i].active = true;
            g->si_shots[i].x = x;
            g->si_shots[i].y = y;
            g->si_shots[i].vx = vx;
            g->si_shots[i].vy = vy;
            g->si_shots[i].kind = kind;
            return;
        }
    }
}

static void si_start_stage(s20_games_t *g, uint8_t stage) {
    g->si_stage = stage % 8;
    g->si_wave_tick = 0;
    g->si_boss_active = false;
    memset(g->si_shots, 0, sizeof(g->si_shots));
    memset(g->si_enemies, 0, sizeof(g->si_enemies));
    memset(g->si_pickups, 0, sizeof(g->si_pickups));
    memset(g->si_fx, 0, sizeof(g->si_fx));
}

static void si_start(s20_games_t *g, uint32_t now_ms) {
    g->si_px = 4.0f;
    g->si_py = 22.0f;
    g->si_lives = 3;
    g->si_bonus_wep = 3; /* missile */
    g->si_bonus_ammo = 3;
    g->si_score = 0;
    g->si_fire_cd = 0;
    g->si_invuln = 0;
    si_start_stage(g, 0);
    g->si_last_tick = now_ms;
    g->paused[S20_GAME_SPACE] = true;
    g->view = S20_GVIEW_PLAYING;
}

static void si_damage_player(s20_games_t *g, s20_audio_t *aud, uint8_t vol) {
    if (g->si_invuln > 0) return;
    si_spawn_fx(g, (int)g->si_px, (int)g->si_py);
    g->si_lives--;
    g->si_invuln = 25;
    game_beep(g, aud, vol, 196, 120);
    if (g->si_lives <= 0) {
        finish_game(g, g->si_score, false, aud, vol);
    }
}

static void si_fire_bonus(s20_games_t *g, s20_audio_t *aud, uint8_t vol) {
    if (g->si_bonus_ammo == 0 || g->si_fire_cd > 0) return;
    g->si_bonus_ammo--;
    g->si_fire_cd = 4;
    float sx = g->si_px + 10.0f;
    float sy = g->si_py + 2.0f;
    if (g->si_bonus_wep == 2) { /* laser beam */
        si_spawn_shot(g, sx, sy - 1.0f, 4.0f, 0.0f, 3);
    } else if (g->si_bonus_wep == 4) { /* 3-way ray */
        si_spawn_shot(g, sx, sy, 3.2f, -0.7f, 4);
        si_spawn_shot(g, sx, sy, 3.5f, 0.0f, 4);
        si_spawn_shot(g, sx, sy, 3.2f, 0.7f, 4);
    } else { /* missile */
        si_spawn_shot(g, sx, sy - 1.0f, 2.8f, 0.0f, 2);
    }
    game_beep(g, aud, vol, 988, 40);
}

static void si_step(s20_games_t *g, s20_audio_t *aud, uint8_t vol) {
    g->si_wave_tick++;
    if (g->si_fire_cd > 0) g->si_fire_cd--;
    if (g->si_invuln > 0) g->si_invuln--;

    /* Spawn waves until tick 220, then summon stage boss */
    if (!g->si_boss_active) {
        if (g->si_wave_tick < 220 && (g->si_wave_tick % 24) == 6) {
            uint8_t et = (uint8_t)((g->si_stage + (g->si_wave_tick / 24)) % 6);
            uint8_t path = (uint8_t)((next_rng(g)) % 4);
            int count = 2 + (int)(next_rng(g) % 2);
            for (int k = 0; k < count; k++) {
                for (int i = 0; i < 12; i++) {
                    if (!g->si_enemies[i].active) {
                        g->si_enemies[i].active = true;
                        g->si_enemies[i].x = 84.0f + k * 10.0f;
                        g->si_enemies[i].y = 14.0f + (float)((next_rng(g)) % 20);
                        g->si_enemies[i].spr_base = si_enemy_spr[et];
                        g->si_enemies[i].hp = (uint8_t)(1 + g->si_stage / 3);
                        g->si_enemies[i].path_id = path;
                        g->si_enemies[i].path_pos = (uint8_t)((k * 12) % 84);
                        g->si_enemies[i].shoot_cd = (uint8_t)(16 + (next_rng(g) % 25));
                        g->si_enemies[i].drop_kind = (k == 0 && (g->si_wave_tick % 72) == 6)
                                                         ? (uint8_t)(1 + (next_rng(g) % 4))
                                                         : 0;
                        break;
                    }
                }
            }
        } else if (g->si_wave_tick >= 230) {
            g->si_boss_active = true;
            g->si_boss_x = 84.0f;
            g->si_boss_y = 14.0f;
            g->si_boss_vy = 1;
            g->si_boss_max_hp = 14 + (int)g->si_stage * 8;
            g->si_boss_hp = g->si_boss_max_hp;
            g->si_boss_cd = 20;
        }
    }

    /* Update enemies */
    for (int i = 0; i < 12; i++) {
        s20_si_enemy_t *e = &g->si_enemies[i];
        if (!e->active) continue;
        e->x -= (1.1f + 0.08f * g->si_stage);
        e->path_pos = (uint8_t)((e->path_pos + 1) % 84);
        int dy = s20_si_paths[e->path_id][e->path_pos];
        if (e->path_id == 1) e->y += dy * 0.25f;
        else e->y = (float)(8 + ((dy < 0 ? 0 : dy) % 26));
        if (e->y < 8.0f) e->y = 8.0f;
        if (e->y > 38.0f) e->y = 38.0f;

        if (e->shoot_cd > 0) e->shoot_cd--;
        else if (e->x < 78.0f) {
            si_spawn_shot(g, e->x - 2.0f, e->y + 2.0f, -2.5f, 0.0f, 1);
            e->shoot_cd = (uint8_t)(24 + (next_rng(g) % 24));
        }
        if (e->x < -12.0f) e->active = false;
        if (e->active && e->x < g->si_px + 9 && e->x + 8 > g->si_px &&
            e->y < g->si_py + 6 && e->y + 6 > g->si_py) {
            e->active = false;
            si_damage_player(g, aud, vol);
        }
    }

    /* Update Boss */
    if (g->si_boss_active) {
        const s20_bitmap_t *bb = &s20_si_sprites[si_boss_spr[g->si_stage]];
        float target_x = (float)(82 - bb->width);
        if (g->si_boss_x > target_x) g->si_boss_x -= 0.9f;
        g->si_boss_y += g->si_boss_vy * 0.65f;
        if (g->si_boss_y < 8.0f) { g->si_boss_y = 8.0f; g->si_boss_vy = 1; }
        if (g->si_boss_y + bb->height > 46.0f) {
            g->si_boss_y = (float)(46 - bb->height);
            g->si_boss_vy = -1;
        }
        if (g->si_boss_cd > 0) g->si_boss_cd--;
        else {
            si_spawn_shot(g, g->si_boss_x - 2.0f, g->si_boss_y + bb->height * 0.35f, -2.7f, -0.35f, 1);
            si_spawn_shot(g, g->si_boss_x - 2.0f, g->si_boss_y + bb->height * 0.65f, -2.7f, 0.35f, 1);
            g->si_boss_cd = (uint8_t)(16 - (g->si_stage > 6 ? 6 : g->si_stage));
        }
    }

    /* Update shots & collisions */
    for (int i = 0; i < 24; i++) {
        s20_si_shot_t *s = &g->si_shots[i];
        if (!s->active) continue;
        s->x += s->vx;
        s->y += s->vy;
        if (s->x < -8.0f || s->x > 86.0f || s->y < 6.0f || s->y > 47.0f) {
            s->active = false;
            continue;
        }
        if (s->kind == 1) {
            if (s->x >= g->si_px && s->x <= g->si_px + 9 && s->y >= g->si_py && s->y <= g->si_py + 6) {
                s->active = false;
                si_damage_player(g, aud, vol);
            }
            continue;
        }
        int dmg = (s->kind == 2 || s->kind == 3) ? 3 : 1;
        float sw = (s->kind == 3) ? 14.0f : 4.0f;
        for (int e = 0; e < 12; e++) {
            s20_si_enemy_t *en = &g->si_enemies[e];
            if (!en->active) continue;
            if (s->x + sw >= en->x && s->x <= en->x + 9 && s->y + 4 >= en->y && s->y <= en->y + 7) {
                if (s->kind != 3) s->active = false;
                if (en->hp <= dmg) {
                    en->active = false;
                    g->si_score += 10;
                    si_spawn_fx(g, (int)en->x, (int)en->y);
                    game_beep(g, aud, vol, 523, 25);
                    if (en->drop_kind) {
                        for (int p = 0; p < 4; p++) {
                            if (!g->si_pickups[p].active) {
                                g->si_pickups[p].active = true;
                                g->si_pickups[p].x = en->x;
                                g->si_pickups[p].y = en->y;
                                g->si_pickups[p].kind = en->drop_kind;
                                break;
                            }
                        }
                    }
                } else {
                    en->hp -= (uint8_t)dmg;
                }
                if (!s->active) break;
            }
        }
        if (s->active && g->si_boss_active) {
            const s20_bitmap_t *bb = &s20_si_sprites[si_boss_spr[g->si_stage]];
            if (s->x + sw >= g->si_boss_x && s->x <= g->si_boss_x + bb->width &&
                s->y + 4 >= g->si_boss_y && s->y <= g->si_boss_y + bb->height) {
                if (s->kind != 3) s->active = false;
                g->si_boss_hp -= dmg;
                if (g->si_boss_hp <= 0) {
                    g->si_score += 100 * (int32_t)(g->si_stage + 1);
                    si_spawn_fx(g, (int)g->si_boss_x + 4, (int)g->si_boss_y + 4);
                    game_beep(g, aud, vol, 1397, 120);
                    if (g->si_stage >= 7) {
                        finish_game(g, g->si_score, true, aud, vol);
                        return;
                    }
                    si_start_stage(g, (uint8_t)(g->si_stage + 1));
                    return;
                }
            }
        }
    }

    /* Update pickups & explosions */
    for (int i = 0; i < 4; i++) {
        s20_si_pickup_t *pk = &g->si_pickups[i];
        if (!pk->active) continue;
        pk->x -= 1.0f;
        if (pk->x < -6.0f) pk->active = false;
        else if (pk->x < g->si_px + 10 && pk->x + 5 > g->si_px &&
                 pk->y < g->si_py + 7 && pk->y + 5 > g->si_py) {
            if (pk->kind == 1 && g->si_lives < 5) g->si_lives++;
            else {
                g->si_bonus_wep = pk->kind;
                if (g->si_bonus_ammo < 9) g->si_bonus_ammo += 2;
            }
            pk->active = false;
            game_beep(g, aud, vol, 1480, 45);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (g->si_fx[i].active) {
            if (g->si_fx[i].timer > 0) g->si_fx[i].timer--;
            if (g->si_fx[i].timer == 0) g->si_fx[i].active = false;
            else g->si_fx[i].frame = (uint8_t)((6 - g->si_fx[i].timer) / 2);
        }
    }
}

static void si_render(const s20_games_t *g, s20_lcd_t *lcd) {
    bool inv = (g->si_stage & 1) != 0;
    s20_draw_mode_t fg = inv ? S20_DRAW_CLR : S20_DRAW_SET;
    if (inv) s20_lcd_clear(lcd, true);

    /* Top HUD (hearts + bonus weapon + score) */
    for (int i = 0; i < g->si_lives && i < 5; i++) {
        draw_sprite(lcd, 1 + i * 6, 1, &s20_si_sprites[78], fg);
    }
    if (g->si_bonus_ammo > 0) {
        int bspr = (g->si_bonus_wep == 2) ? 79 : (g->si_bonus_wep == 4 ? 81 : 80);
        draw_sprite(lcd, 35, 1, &s20_si_sprites[bspr], fg);
        char abuf[8];
        snprintf(abuf, sizeof(abuf), "%u", (unsigned)g->si_bonus_ammo);
        s20_lcd_draw_text(lcd, 41, 0, S20_FONT_TINY_PLAIN, abuf, fg);
    }
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%05ld", (long)g->si_score);
    s20_lcd_draw_text_aligned(lcd, 48, 0, 35, 7, S20_FONT_TINY_PLAIN, sbuf, S20_ALIGN_RIGHT, fg, 0, true);
    s20_lcd_fill_rect(lcd, 0, 6, 84, 1, fg);

    /* Scrolling terrain at bottom from ROM heightmap */
    const uint8_t *ter = s20_si_terrains[g->si_stage % 7];
    int shift = (int)(g->si_wave_tick / 2);
    for (int x = 0; x < 84; x++) {
        uint8_t h = ter[(x + shift) & 31];
        if (h > 0 && h <= 6) {
            s20_lcd_fill_rect(lcd, x, 48 - h, 1, h, fg);
        }
    }

    /* Player ship (blink when invulnerable) */
    if ((g->si_invuln & 2) == 0) {
        draw_sprite(lcd, (int)g->si_px, (int)g->si_py, &s20_si_sprites[0], fg);
    }

    /* Enemies */
    int anim = (g->si_wave_tick >> 2) & 1;
    for (int i = 0; i < 12; i++) {
        if (g->si_enemies[i].active) {
            int sp = g->si_enemies[i].spr_base + (anim && g->si_enemies[i].spr_base != 54 ? 1 : 0);
            draw_sprite(lcd, (int)g->si_enemies[i].x, (int)g->si_enemies[i].y, &s20_si_sprites[sp], fg);
        }
    }

    /* Boss */
    if (g->si_boss_active) {
        int bbase = si_boss_spr[g->si_stage] + (anim ? 2 : 0);
        draw_sprite(lcd, (int)g->si_boss_x, (int)g->si_boss_y, &s20_si_sprites[bbase], fg);
        int hp_w = (g->si_boss_hp * 20) / (g->si_boss_max_hp > 0 ? g->si_boss_max_hp : 1);
        s20_lcd_fill_rect(lcd, 62, 7, hp_w, 2, fg);
    }

    /* Shots, pickups, explosions */
    for (int i = 0; i < 24; i++) {
        const s20_si_shot_t *s = &g->si_shots[i];
        if (!s->active) continue;
        if (s->kind == 1) draw_sprite(lcd, (int)s->x, (int)s->y, &s20_si_sprites[16], fg);
        else if (s->kind == 2) draw_sprite(lcd, (int)s->x, (int)s->y, &s20_si_sprites[1], fg);
        else if (s->kind == 3) s20_lcd_fill_rect(lcd, (int)s->x, (int)s->y + 1, 14, 2, fg);
        else if (s->kind == 4) draw_sprite(lcd, (int)s->x, (int)s->y, &s20_si_sprites[9], fg);
        else s20_lcd_fill_rect(lcd, (int)s->x, (int)s->y, 3, 1, fg);
    }
    for (int i = 0; i < 4; i++) {
        if (g->si_pickups[i].active) {
            int sp = 77 + g->si_pickups[i].kind;
            draw_sprite(lcd, (int)g->si_pickups[i].x, (int)g->si_pickups[i].y, &s20_si_sprites[sp], fg);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (g->si_fx[i].active) {
            int sp = (g->si_fx[i].frame == 0) ? 55 : 56;
            draw_sprite(lcd, g->si_fx[i].x, g->si_fx[i].y, &s20_si_sprites[sp], fg);
        }
    }
}

/* =========================================================================
 * 3. BANTUMI (Mancala with authentic ROM board + rotating arrows + AI)
 * ========================================================================= */

static void bn_select_valid(s20_games_t *g) {
    int base = (g->bn_turn == 0) ? 0 : 7;
    if (g->bn_cursor >= base && g->bn_cursor < base + 6 && g->bn_pits[g->bn_cursor] > 0) return;
    for (int i = 0; i < 6; i++) {
        if (g->bn_pits[base + i] > 0) {
            g->bn_cursor = base + i;
            return;
        }
    }
}

static void bn_start(s20_games_t *g, bool two_player, uint32_t now_ms) {
    for (int i = 0; i < 14; i++) g->bn_pits[i] = 4;
    g->bn_pits[6] = 0;
    g->bn_pits[13] = 0;
    g->bn_two_player = two_player;
    g->bn_turn = 0;
    g->bn_cursor = 0;
    g->bn_arrow_frame = 0;
    g->bn_ai_tick = now_ms;
    g->bn_status[0] = '\0';
    g->paused[S20_GAME_BANTUMI] = true;
    g->view = S20_GVIEW_PLAYING;
}

static int bn_side_sum(const uint8_t pits[14], int side) {
    int base = (side == 0) ? 0 : 7, sum = 0;
    for (int i = 0; i < 6; i++) sum += pits[base + i];
    return sum;
}

static bool bn_apply_move(uint8_t pits[14], int side, int pit, bool *out_capture) {
    int own_store = (side == 0) ? 6 : 13;
    int opp_store = (side == 0) ? 13 : 6;
    int base = (side == 0) ? 0 : 7;
    uint8_t beans = pits[pit];
    pits[pit] = 0;
    int cur = pit;
    while (beans > 0) {
        cur = (cur + 1) % 14;
        if (cur == opp_store) continue;
        pits[cur]++;
        beans--;
    }
    if (out_capture) *out_capture = false;
    if (cur == own_store) return true; /* Extra turn */
    if (cur >= base && cur < base + 6 && pits[cur] == 1) {
        int opp = 12 - cur;
        if (pits[opp] > 0) {
            pits[own_store] = (uint8_t)(pits[own_store] + pits[opp] + 1);
            pits[cur] = 0;
            pits[opp] = 0;
            if (out_capture) *out_capture = true;
        }
    }
    return false;
}

static int bn_eval_ai(const uint8_t pits[14], int depth) {
    int best = -10000;
    for (int i = 7; i <= 12; i++) {
        if (pits[i] == 0) continue;
        uint8_t sim[14];
        memcpy(sim, pits, sizeof(sim));
        bool cap = false;
        bool extra = bn_apply_move(sim, 1, i, &cap);
        int score = ((int)sim[13] - (int)sim[6]) * 4 + (extra ? 3 : 0) + (cap ? 2 : 0);
        if (depth > 1 && extra) score += bn_eval_ai(sim, depth - 1) / 2;
        if (score > best) best = score;
    }
    return (best == -10000) ? ((int)pits[13] - (int)pits[6]) * 4 : best;
}

static int bn_pick_ai_pit(s20_games_t *g) {
    if (g->bantumi_level == 1) {
        int cands[6], n = 0;
        for (int i = 7; i <= 12; i++) if (g->bn_pits[i] > 0) cands[n++] = i;
        return n > 0 ? cands[next_rng(g) % (uint32_t)n] : 7;
    }
    int best_pit = -1, best_score = -100000;
    int depth = (g->bantumi_level >= 4) ? 2 : 1;
    for (int i = 7; i <= 12; i++) {
        if (g->bn_pits[i] == 0) continue;
        uint8_t sim[14];
        memcpy(sim, g->bn_pits, sizeof(sim));
        bool cap = false;
        bool extra = bn_apply_move(sim, 1, i, &cap);
        int sc = ((int)sim[13] - (int)sim[6]) * 4 + (extra ? 5 : 0) + (cap ? 4 : 0);
        if (depth > 1 && extra) sc += bn_eval_ai(sim, depth);
        if (sc > best_score) { best_score = sc; best_pit = i; }
    }
    return best_pit >= 0 ? best_pit : 7;
}

static void bn_play_pit(s20_games_t *g, int pit, s20_audio_t *aud, uint8_t vol, uint32_t now_ms) {
    if (g->bn_pits[pit] == 0) return;
    bool cap = false;
    bool extra = bn_apply_move(g->bn_pits, g->bn_turn, pit, &cap);
    g->bn_arrow_frame = (uint8_t)((g->bn_arrow_frame + 1) & 7);
    game_beep(g, aud, vol, extra ? 1319 : (cap ? 1047 : 784), 45);

    if (bn_side_sum(g->bn_pits, 0) == 0 || bn_side_sum(g->bn_pits, 1) == 0) {
        g->bn_pits[6] = (uint8_t)(g->bn_pits[6] + bn_side_sum(g->bn_pits, 0));
        g->bn_pits[13] = (uint8_t)(g->bn_pits[13] + bn_side_sum(g->bn_pits, 1));
        for (int i = 0; i < 6; i++) g->bn_pits[i] = g->bn_pits[7 + i] = 0;
        finish_game(g, (int32_t)g->bn_pits[6], g->bn_pits[6] >= g->bn_pits[13], aud, vol);
        return;
    }
    if (!extra) g->bn_turn ^= 1;
    bn_select_valid(g);
    g->bn_ai_tick = now_ms;
}

static void bn_render(const s20_games_t *g, s20_lcd_t *lcd) {
    static const int pit_x[6] = { 4, 17, 30, 44, 57, 70 };
    draw_sprite(lcd, 0, 0, &s20_bantumi_sprites[0], S20_DRAW_SET);

    /* Center rotating turn arrow inside middle bar */
    s20_lcd_fill_rect(lcd, 34, 16, 16, 16, S20_DRAW_SET);
    draw_sprite(lcd, 34, 16, &s20_bantumi_sprites[7 + (g->bn_arrow_frame & 7)], S20_DRAW_CLR);

    /* Turn label left/right of center arrow */
    const char *turn_txt = (g->bn_turn == 0) ? "P1" : (g->bn_two_player ? "P2" : "AI");
    s20_lcd_draw_text(lcd, 20, 21, S20_FONT_TINY_PLAIN, turn_txt, S20_DRAW_CLR);

    char buf[8];
    /* Top row: P2 pits 12..7 left->right (white bowls -> dark text) */
    for (int col = 0; col < 6; col++) {
        int pit = 12 - col;
        snprintf(buf, sizeof(buf), "%u", (unsigned)g->bn_pits[pit]);
        s20_lcd_draw_text_aligned(lcd, pit_x[col], 4, 10, 7, S20_FONT_TINY_PLAIN,
                                  buf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        if (g->bn_turn == 1 && g->bn_cursor == pit) {
            s20_lcd_draw_rect_outline(lcd, pit_x[col] - 1, 2, 11, 10, S20_DRAW_XOR);
        }
    }
    /* Bottom row: P1 pits 0..5 left->right (dark bowls -> white text) */
    for (int col = 0; col < 6; col++) {
        int pit = col;
        snprintf(buf, sizeof(buf), "%u", (unsigned)g->bn_pits[pit]);
        s20_lcd_draw_text_aligned(lcd, pit_x[col], 37, 10, 7, S20_FONT_TINY_PLAIN,
                                  buf, S20_ALIGN_CENTER, S20_DRAW_CLR, 0, true);
        if (g->bn_turn == 0 && g->bn_cursor == pit) {
            s20_lcd_draw_rect_outline(lcd, pit_x[col], 37, 10, 8, S20_DRAW_CLR);
        }
    }
    /* Left big pot (P2 store 13, white interior) & Right big pot (P1 store 6, dark interior) */
    snprintf(buf, sizeof(buf), "%u", (unsigned)g->bn_pits[13]);
    s20_lcd_draw_text_aligned(lcd, 5, 20, 9, 8, S20_FONT_TINY_PLAIN, buf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
    snprintf(buf, sizeof(buf), "%u", (unsigned)g->bn_pits[6]);
    s20_lcd_draw_text_aligned(lcd, 70, 20, 9, 8, S20_FONT_TINY_PLAIN, buf, S20_ALIGN_CENTER, S20_DRAW_CLR, 0, true);
}

/* =========================================================================
 * 4. PAIRS II (Time trial & Puzzle, 28 ROM 8x11 icons + dynamite fuse)
 * ========================================================================= */

static void pairs_setup_level(s20_games_t *g, uint8_t lvl, uint32_t now_ms) {
    static const uint8_t cols_tbl[7] = { 4, 4, 4, 5, 6, 6, 7 };
    static const uint8_t rows_tbl[7] = { 2, 3, 4, 4, 4, 4, 4 };
    int idx = (lvl < 1 ? 0 : (lvl > 7 ? 6 : lvl - 1));
    g->pr_cols = cols_tbl[idx];
    g->pr_rows = rows_tbl[idx];
    g->pr_total = g->pr_cols * g->pr_rows;

    for (int i = 0; i < g->pr_total / 2; i++) {
        g->pr_cards[i * 2] = (uint8_t)(i % S20_PAIRS_CARD_CNT);
        g->pr_cards[i * 2 + 1] = (uint8_t)(i % S20_PAIRS_CARD_CNT);
    }
    for (int i = g->pr_total - 1; i > 0; i--) {
        int j = (int)(next_rng(g) % (uint32_t)(i + 1));
        uint8_t t = g->pr_cards[i];
        g->pr_cards[i] = g->pr_cards[j];
        g->pr_cards[j] = t;
    }
    memset(g->pr_state, 0, sizeof(g->pr_state));
    g->pr_cx = 0;
    g->pr_cy = 0;
    g->pr_first_idx = -1;
    g->pr_second_idx = -1;
    g->pr_mismatch_tick = 0;
    g->pr_time_max = 45 + (g->pr_total * 2) - idx * 4;
    g->pr_time_left = g->pr_time_max;
    g->pr_last_tick = now_ms;
}

static void pairs_start(s20_games_t *g, bool timed, uint32_t now_ms) {
    g->pairs_timed = timed;
    g->pr_tries = 0;
    g->pr_score = 0;
    pairs_setup_level(g, g->pairs_level, now_ms);
    g->paused[S20_GAME_PAIRS] = true;
    g->view = S20_GVIEW_PLAYING;
}

static void pairs_flip(s20_games_t *g, s20_audio_t *aud, uint8_t vol, uint32_t now_ms) {
    if (g->pr_second_idx >= 0) {
        g->pr_state[g->pr_first_idx] = 0;
        g->pr_state[g->pr_second_idx] = 0;
        g->pr_first_idx = -1;
        g->pr_second_idx = -1;
    }
    int pos = g->pr_cy * g->pr_cols + g->pr_cx;
    if (pos < 0 || pos >= g->pr_total || g->pr_state[pos] != 0) return;
    g->pr_state[pos] = 1;
    game_beep(g, aud, vol, 880, 30);

    if (g->pr_first_idx < 0) {
        g->pr_first_idx = pos;
        return;
    }
    g->pr_tries++;
    if (g->pr_cards[g->pr_first_idx] == g->pr_cards[pos]) {
        g->pr_state[g->pr_first_idx] = 2;
        g->pr_state[pos] = 2;
        g->pr_first_idx = -1;
        g->pr_score += 20;
        game_beep(g, aud, vol, 1319, 55);

        bool all_done = true;
        for (int i = 0; i < g->pr_total; i++) {
            if (g->pr_state[i] != 2) { all_done = false; break; }
        }
        if (all_done) {
            if (g->pairs_timed) g->pr_score += g->pr_time_left * 2;
            else g->pr_score += (g->pr_total * 10 > g->pr_tries * 5) ? (g->pr_total * 10 - g->pr_tries * 5) : 10;
            if (g->pairs_timed && g->pairs_level < 7) {
                g->pairs_level++;
                pairs_setup_level(g, g->pairs_level, now_ms);
            } else {
                finish_game(g, g->pr_score, true, aud, vol);
            }
        }
    } else {
        g->pr_second_idx = pos;
        g->pr_mismatch_tick = now_ms;
    }
}

static void pairs_render(const s20_games_t *g, s20_lcd_t *lcd) {
    int start_x = 1;
    int start_y = (48 - g->pr_rows * 12) / 2;
    for (int r = 0; r < g->pr_rows; r++) {
        for (int c = 0; c < g->pr_cols; c++) {
            int idx = r * g->pr_cols + c;
            int px = start_x + c * 9;
            int py = start_y + r * 12;
            if (g->pr_state[idx] == 0) {
                draw_sprite(lcd, px + 1, py + 1, &s20_pairs_misc_sprites[8], S20_DRAW_SET);
            } else if (g->pr_state[idx] == 1) {
                draw_sprite(lcd, px + 1, py, &s20_pairs_card_sprites[g->pr_cards[idx]], S20_DRAW_SET);
            } else {
                s20_lcd_set_pixel(lcd, px + 4, py + 5, S20_DRAW_SET);
            }
            if (r == g->pr_cy && c == g->pr_cx) {
                s20_lcd_draw_rect_outline(lcd, px, py, 9, 11, S20_DRAW_XOR);
            }
        }
    }
    /* Right HUD */
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", (long)g->pr_score);
    s20_lcd_draw_text_aligned(lcd, 64, 1, 19, 7, S20_FONT_TINY_PLAIN, buf, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
    if (g->pairs_timed) {
        snprintf(buf, sizeof(buf), "%d", g->pr_time_left);
        s20_lcd_draw_text_aligned(lcd, 64, 9, 19, 7, S20_FONT_TINY_PLAIN, buf, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        draw_sprite(lcd, 70, 27, &s20_pairs_misc_sprites[0], S20_DRAW_SET);
        draw_sprite(lcd, 70, 21, &s20_pairs_misc_sprites[1 + (g->pr_spark_frame % 3)], S20_DRAW_SET);
    } else {
        snprintf(buf, sizeof(buf), "T:%d", g->pr_tries);
        s20_lcd_draw_text_aligned(lcd, 64, 12, 19, 7, S20_FONT_TINY_PLAIN, buf, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
    }
}

/* =========================================================================
 * PUBLIC API & MENU ROUTING
 * ========================================================================= */

void s20_games_init(s20_games_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x3310BEEFu;
    g->sounds = true;
    g->lights = true;
    g->shakes = true;
    g->snake_level = 4;
    g->snake_maze = 1;
    g->bantumi_level = 2;
    g->pairs_level = 3;
    g->pairs_timed = true;
}

void s20_games_enter(s20_games_t *g) {
    g->view = S20_GVIEW_MAIN;
    g->main_cursor = 0;
}

static void launch_game_action(s20_games_t *g, gmenu_act_t act, uint32_t now_ms) {
    switch (act) {
    case ACT_CONTINUE:
        g->view = S20_GVIEW_PLAYING;
        g->sn_last_step = now_ms;
        g->si_last_tick = now_ms;
        g->bn_ai_tick = now_ms;
        g->pr_last_tick = now_ms;
        break;
    case ACT_NEW_GAME:
        if (g->game == S20_GAME_SNAKE) snake_start(g, now_ms);
        else if (g->game == S20_GAME_SPACE) si_start(g, now_ms);
        break;
    case ACT_1PLAYER: bn_start(g, false, now_ms); break;
    case ACT_2PLAYERS: bn_start(g, true, now_ms); break;
    case ACT_TIMED: pairs_start(g, true, now_ms); break;
    case ACT_PUZZLE: pairs_start(g, false, now_ms); break;
    case ACT_LEVEL:
        g->level_cursor = (g->game == S20_GAME_SNAKE) ? g->snake_level :
                          (g->game == S20_GAME_BANTUMI) ? g->bantumi_level : g->pairs_level;
        g->view = S20_GVIEW_LEVEL;
        break;
    case ACT_MAZES:
        g->maze_cursor = g->snake_maze;
        g->view = S20_GVIEW_MAZES;
        break;
    case ACT_TOPSCORE:
        g->view = S20_GVIEW_TOPSCORE;
        break;
    case ACT_INSTR:
        g->instr_scroll = 0;
        g->instr_tid = (g->game == S20_GAME_SNAKE) ? TID_INSTR_SNAKE :
                       (g->game == S20_GAME_SPACE) ? TID_INSTR_SPACE :
                       (g->game == S20_GAME_BANTUMI) ? TID_INSTR_BANTUMI :
                       (g->pairs_timed ? TID_INSTR_PAIRS_TIMED : TID_INSTR_PAIRS_PUZZ);
        g->view = S20_GVIEW_INSTRUCTIONS;
        break;
    }
}

bool s20_games_handle_key(s20_games_t *g, s20_key_t key, s20_audio_t *aud, uint8_t vol, s20_lang_t lang) {
    (void)lang;
    uint32_t now_ms = g->sn_last_step; /* tick timestamp updated on s20_games_tick */
    if (g->view == S20_GVIEW_PLAYING) {
        if (key == S20_KEY_BACK) {
            g->view = S20_GVIEW_GAME_MENU;
            g->game_cursor = 0;
            return true;
        }
        switch (g->game) {
        case S20_GAME_SNAKE:
            if (key == S20_KEY_UP || key == S20_KEY_2) snake_queue_dir(g, 3);
            else if (key == S20_KEY_DOWN || key == S20_KEY_8) snake_queue_dir(g, 1);
            else if (key == S20_KEY_LEFT || key == S20_KEY_4) snake_queue_dir(g, 2);
            else if (key == S20_KEY_RIGHT || key == S20_KEY_6) snake_queue_dir(g, 0);
            else if (key == S20_KEY_SELECT) { g->view = S20_GVIEW_GAME_MENU; g->game_cursor = 0; }
            break;
        case S20_GAME_SPACE:
            if (key == S20_KEY_UP || key == S20_KEY_2) {
                g->si_py -= 3.0f; if (g->si_py < 8.0f) g->si_py = 8.0f;
            } else if (key == S20_KEY_DOWN || key == S20_KEY_8 || key == S20_KEY_0) {
                g->si_py += 3.0f; if (g->si_py > 39.0f) g->si_py = 39.0f;
            } else if (key == S20_KEY_LEFT || key == S20_KEY_STAR) {
                g->si_px -= 4.0f; if (g->si_px < 1.0f) g->si_px = 1.0f;
            } else if (key == S20_KEY_RIGHT || key == S20_KEY_HASH) {
                g->si_px += 4.0f; if (g->si_px > 68.0f) g->si_px = 68.0f;
            } else if (key == S20_KEY_SELECT || key == S20_KEY_5 || key == S20_KEY_1 || key == S20_KEY_3) {
                if (g->si_fire_cd == 0) {
                    si_spawn_shot(g, g->si_px + 10.0f, g->si_py + 3.0f, 3.6f, 0.0f, 0);
                    g->si_fire_cd = 2;
                    game_beep(g, aud, vol, 1047, 20);
                }
            } else if (key == S20_KEY_4 || key == S20_KEY_6) {
                si_fire_bonus(g, aud, vol);
            }
            break;
        case S20_GAME_BANTUMI:
            if (g->bn_turn == 1 && !g->bn_two_player) break;
            if (key == S20_KEY_LEFT || key == S20_KEY_4 || key == S20_KEY_RIGHT || key == S20_KEY_6) {
                int step = (key == S20_KEY_RIGHT || key == S20_KEY_6) ? 1 : -1;
                if (g->bn_turn == 1) step = -step;
                int base = (g->bn_turn == 0) ? 0 : 7;
                for (int tries = 0; tries < 6; tries++) {
                    g->bn_cursor = base + ((g->bn_cursor - base + step + 6) % 6);
                    if (g->bn_pits[g->bn_cursor] > 0) break;
                }
            } else if (key == S20_KEY_SELECT || key == S20_KEY_5) {
                bn_play_pit(g, g->bn_cursor, aud, vol, now_ms);
            }
            break;
        case S20_GAME_PAIRS:
            if (key == S20_KEY_LEFT || key == S20_KEY_4) g->pr_cx = (g->pr_cx - 1 + g->pr_cols) % g->pr_cols;
            else if (key == S20_KEY_RIGHT || key == S20_KEY_6) g->pr_cx = (g->pr_cx + 1) % g->pr_cols;
            else if (key == S20_KEY_UP || key == S20_KEY_2) g->pr_cy = (g->pr_cy - 1 + g->pr_rows) % g->pr_rows;
            else if (key == S20_KEY_DOWN || key == S20_KEY_8) g->pr_cy = (g->pr_cy + 1) % g->pr_rows;
            else if (key == S20_KEY_SELECT || key == S20_KEY_5) pairs_flip(g, aud, vol, now_ms);
            break;
        }
        return true;
    }

    switch (g->view) {
    case S20_GVIEW_MAIN:
        if (key == S20_KEY_UP) g->main_cursor = (g->main_cursor + 4) % 5;
        else if (key == S20_KEY_DOWN) g->main_cursor = (g->main_cursor + 1) % 5;
        else if (key == S20_KEY_BACK) return false;
        else if (key == S20_KEY_SELECT) {
            if (g->main_cursor == 4) { g->set_cursor = 0; g->view = S20_GVIEW_SETTINGS; }
            else { g->game = (s20_game_id_t)g->main_cursor; g->game_cursor = 0; g->view = S20_GVIEW_GAME_MENU; }
        }
        break;
    case S20_GVIEW_SETTINGS:
        if (key == S20_KEY_UP) g->set_cursor = (g->set_cursor + 2) % 3;
        else if (key == S20_KEY_DOWN) g->set_cursor = (g->set_cursor + 1) % 3;
        else if (key == S20_KEY_BACK) g->view = S20_GVIEW_MAIN;
        else if (key == S20_KEY_SELECT) {
            g->setting_idx = g->set_cursor;
            bool cur = (g->setting_idx == 0) ? g->sounds : (g->setting_idx == 1 ? g->lights : g->shakes);
            g->toggle_cursor = cur ? 1 : 0;
            g->view = S20_GVIEW_SETTING_TOGGLE;
        }
        break;
    case S20_GVIEW_SETTING_TOGGLE:
        if (key == S20_KEY_UP || key == S20_KEY_DOWN) g->toggle_cursor ^= 1;
        else if (key == S20_KEY_BACK) g->view = S20_GVIEW_SETTINGS;
        else if (key == S20_KEY_SELECT) {
            bool on = (g->toggle_cursor == 1);
            if (g->setting_idx == 0) g->sounds = on;
            else if (g->setting_idx == 1) g->lights = on;
            else g->shakes = on;
            g->view = S20_GVIEW_SETTINGS;
        }
        break;
    case S20_GVIEW_GAME_MENU: {
        gmenu_entry_t entries[8];
        int cnt = build_game_menu(g, entries);
        if (key == S20_KEY_UP) g->game_cursor = (g->game_cursor - 1 + cnt) % cnt;
        else if (key == S20_KEY_DOWN) g->game_cursor = (g->game_cursor + 1) % cnt;
        else if (key == S20_KEY_BACK) g->view = S20_GVIEW_MAIN;
        else if (key == S20_KEY_SELECT) launch_game_action(g, entries[g->game_cursor].act, now_ms);
        break;
    }
    case S20_GVIEW_LEVEL: {
        int max_l = (g->game == S20_GAME_SNAKE) ? 9 : (g->game == S20_GAME_BANTUMI ? 5 : 7);
        if (key == S20_KEY_UP || key == S20_KEY_RIGHT) {
            if (g->level_cursor < max_l) g->level_cursor++;
        } else if (key == S20_KEY_DOWN || key == S20_KEY_LEFT) {
            if (g->level_cursor > 1) g->level_cursor--;
        } else if (key == S20_KEY_SELECT) {
            if (g->game == S20_GAME_SNAKE) g->snake_level = (uint8_t)g->level_cursor;
            else if (g->game == S20_GAME_BANTUMI) g->bantumi_level = (uint8_t)g->level_cursor;
            else g->pairs_level = (uint8_t)g->level_cursor;
            g->view = S20_GVIEW_GAME_MENU;
        } else if (key == S20_KEY_BACK) {
            g->view = S20_GVIEW_GAME_MENU;
        }
        break;
    }
    case S20_GVIEW_MAZES:
        if (key == S20_KEY_UP) g->maze_cursor = (g->maze_cursor + 5) % 6;
        else if (key == S20_KEY_DOWN) g->maze_cursor = (g->maze_cursor + 1) % 6;
        else if (key == S20_KEY_SELECT) { g->snake_maze = (uint8_t)g->maze_cursor; g->view = S20_GVIEW_GAME_MENU; }
        else if (key == S20_KEY_BACK) g->view = S20_GVIEW_GAME_MENU;
        break;
    case S20_GVIEW_INSTRUCTIONS:
        if (key == S20_KEY_DOWN && g->instr_scroll < 160) g->instr_scroll += 10;
        else if (key == S20_KEY_UP && g->instr_scroll > 0) g->instr_scroll -= 10;
        else if (key == S20_KEY_BACK || key == S20_KEY_SELECT) g->view = S20_GVIEW_GAME_MENU;
        break;
    case S20_GVIEW_TOPSCORE:
    case S20_GVIEW_GAMEOVER:
        if (key == S20_KEY_BACK || key == S20_KEY_SELECT) g->view = S20_GVIEW_GAME_MENU;
        break;
    default:
        break;
    }
    return true;
}

void s20_games_tick(s20_games_t *g, uint32_t now_ms, s20_audio_t *aud, uint8_t vol) {
    if (g->view != S20_GVIEW_PLAYING) {
        g->sn_last_step = now_ms;
        return;
    }
    switch (g->game) {
    case S20_GAME_SNAKE: {
        uint32_t step_ms = (uint32_t)s20_snake_speeds[(g->snake_level - 1) % 9] * 10u;
        if (now_ms - g->sn_last_step >= step_ms) {
            g->sn_last_step = now_ms;
            snake_step(g, aud, vol);
        }
        break;
    }
    case S20_GAME_SPACE:
        if (now_ms - g->si_last_tick >= 65u) {
            g->si_last_tick = now_ms;
            si_step(g, aud, vol);
        }
        break;
    case S20_GAME_BANTUMI:
        if (g->bn_turn == 1 && !g->bn_two_player && now_ms - g->bn_ai_tick >= 450u) {
            int ai_pit = bn_pick_ai_pit(g);
            bn_play_pit(g, ai_pit, aud, vol, now_ms);
        }
        break;
    case S20_GAME_PAIRS:
        if (g->pr_second_idx >= 0 && now_ms - g->pr_mismatch_tick >= 650u) {
            g->pr_state[g->pr_first_idx] = 0;
            g->pr_state[g->pr_second_idx] = 0;
            g->pr_first_idx = -1;
            g->pr_second_idx = -1;
        }
        if (g->pairs_timed && now_ms - g->pr_last_tick >= 1000u) {
            g->pr_last_tick = now_ms;
            g->pr_spark_frame++;
            if (g->pr_time_left > 0) g->pr_time_left--;
            else finish_game(g, g->pr_score, false, aud, vol);
        }
        break;
    }
}

static void render_list_menu(s20_lcd_t *lcd, const char *hdr, const char **labels, int count, int cursor, const char *softkey) {
    if (hdr && hdr[0]) {
        s20_lcd_draw_text_aligned(lcd, 2, 0, 80, 9, S20_FONT_SMALL_BOLD, hdr, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
    }
    int vis = hdr && hdr[0] ? 2 : 3;
    int top = 0;
    if (cursor >= vis) top = cursor - vis + 1;
    int y0 = hdr && hdr[0] ? 10 : 1;
    for (int i = 0; i < vis && (top + i) < count; i++) {
        int idx = top + i;
        int y = y0 + i * 11;
        bool sel = (idx == cursor);
        if (sel) s20_lcd_fill_rect(lcd, 0, y, 79, 11, S20_DRAW_SET);
        s20_lcd_draw_text_aligned(lcd, 3, y + 1, 74, 9, sel ? S20_FONT_SMALL_BOLD : S20_FONT_SMALL_PLAIN,
                                  labels[idx], S20_ALIGN_LEFT, sel ? S20_DRAW_CLR : S20_DRAW_SET, 0, true);
    }
    s20_lcd_draw_scrollbar(lcd, 81, y0, 34 - y0, count, cursor);
    s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD, softkey, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
}

void s20_games_render(const s20_games_t *g, s20_lcd_t *lcd, s20_lang_t lang, uint32_t now_ms) {
    (void)now_ms;
    s20_lcd_clear(lcd, false);

    if (g->view == S20_GVIEW_PLAYING) {
        switch (g->game) {
        case S20_GAME_SNAKE: snake_render(g, lcd); break;
        case S20_GAME_SPACE: si_render(g, lcd); break;
        case S20_GAME_BANTUMI: bn_render(g, lcd); break;
        case S20_GAME_PAIRS: pairs_render(g, lcd); break;
        }
        return;
    }

    switch (g->view) {
    case S20_GVIEW_MAIN: {
        const char *items[5] = {
            s20_get_string(TID_SNAKE2, lang),
            s20_get_string(TID_SPACE, lang),
            s20_get_string(TID_BANTUMI, lang),
            s20_get_string(TID_PAIRS2, lang),
            s20_get_string(TID_SETTINGS, lang),
        };
        render_list_menu(lcd, s20_get_string(TID_GAMES, lang), items, 5, g->main_cursor, s20_get_string(TID_SELECT, lang));
        break;
    }
    case S20_GVIEW_SETTINGS: {
        const char *items[3] = {
            s20_get_string(TID_SOUNDS, lang),
            s20_get_string(TID_LIGHTS, lang),
            s20_get_string(TID_SHAKES, lang),
        };
        render_list_menu(lcd, s20_get_string(TID_SETTINGS, lang), items, 3, g->set_cursor, s20_get_string(TID_SELECT, lang));
        break;
    }
    case S20_GVIEW_SETTING_TOGGLE: {
        const char *hdr = s20_get_string(g->setting_idx == 0 ? TID_SOUNDS : (g->setting_idx == 1 ? TID_LIGHTS : TID_SHAKES), lang);
        const char *items[2] = { s20_get_string(TID_OFF, lang), s20_get_string(TID_ON, lang) };
        render_list_menu(lcd, hdr, items, 2, g->toggle_cursor, s20_get_string(TID_OK, lang));
        break;
    }
    case S20_GVIEW_GAME_MENU: {
        gmenu_entry_t entries[8];
        int cnt = build_game_menu(g, entries);
        const char *labels[8];
        for (int i = 0; i < cnt; i++) labels[i] = s20_get_string(entries[i].tid, lang);
        static const uint16_t game_tids[4] = { TID_SNAKE2, TID_SPACE, TID_BANTUMI, TID_PAIRS2 };
        render_list_menu(lcd, s20_get_string(game_tids[g->game], lang), labels, cnt, g->game_cursor, s20_get_string(TID_SELECT, lang));
        break;
    }
    case S20_GVIEW_LEVEL: {
        int max_l = (g->game == S20_GAME_SNAKE) ? 9 : (g->game == S20_GAME_BANTUMI ? 5 : 7);
        s20_lcd_draw_text_aligned(lcd, 2, 2, 80, 10, S20_FONT_SMALL_BOLD,
                                  s20_get_string(TID_LEVEL_HDR, lang), S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        char lbuf[8];
        snprintf(lbuf, sizeof(lbuf), "%d", g->level_cursor);
        s20_lcd_draw_text_aligned(lcd, 2, 13, 80, 13, S20_FONT_LARGE_BOLD, lbuf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        int bar_w = (g->level_cursor * 60) / max_l;
        s20_lcd_draw_rect_outline(lcd, 11, 28, 62, 5, S20_DRAW_SET);
        s20_lcd_fill_rect(lcd, 12, 29, bar_w, 3, S20_DRAW_SET);
        s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD,
                                  s20_get_string(TID_OK, lang), S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        break;
    }
    case S20_GVIEW_MAZES: {
        const char *labels[6] = {
            s20_get_string(TID_NO_MAZE, lang),
            s20_get_string(TID_MAZE1 + 0, lang),
            s20_get_string(TID_MAZE1 + 1, lang),
            s20_get_string(TID_MAZE1 + 2, lang),
            s20_get_string(TID_MAZE1 + 3, lang),
            s20_get_string(TID_MAZE1 + 4, lang),
        };
        render_list_menu(lcd, s20_get_string(TID_MAZES, lang), labels, 6, g->maze_cursor, s20_get_string(TID_OK, lang));
        break;
    }
    case S20_GVIEW_TOPSCORE: {
        char buf[64];
        format_tid_num(buf, sizeof(buf), TID_TOP_FMT, lang, g->top_score[game_score_slot(g)]);
        s20_lcd_draw_text_aligned(lcd, 3, 6, 78, 28, S20_FONT_SMALL_BOLD, buf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, false);
        s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD,
                                  s20_get_string(TID_BACK, lang), S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        break;
    }
    case S20_GVIEW_INSTRUCTIONS: {
        const char *txt = s20_get_string(g->instr_tid, lang);
        s20_lcd_draw_text_aligned(lcd, 2, 2 - g->instr_scroll, 79, 200, S20_FONT_SMALL_PLAIN,
                                  txt, S20_ALIGN_LEFT, S20_DRAW_SET, 0, false);
        s20_lcd_fill_rect(lcd, 0, 34, 84, 14, S20_DRAW_CLR);
        s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD,
                                  s20_get_string(TID_BACK, lang), S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        break;
    }
    case S20_GVIEW_GAMEOVER: {
        char buf[64];
        if (g->game == S20_GAME_BANTUMI) {
            snprintf(buf, sizeof(buf), "%s", s20_get_string(g->won_game ? TID_GO_WON : TID_GO_LOST, lang));
        } else {
            format_tid_num(buf, sizeof(buf), g->new_record ? TID_GO_TOP : TID_GO_SCORE, lang, g->last_score);
        }
        s20_lcd_draw_text_aligned(lcd, 2, 3, 80, 31, S20_FONT_SMALL_BOLD, buf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, false);
        s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD,
                                  s20_get_string(TID_OK, lang), S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        break;
    }
    default:
        break;
    }
}
