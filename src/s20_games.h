#ifndef S20_GAMES_H
#define S20_GAMES_H

#include "s20_assets.h"
#include "s20_audio.h"
#include "s20_games_data.h"
#include "s20_lcd.h"
#include "s20_platform.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    S20_GAME_SNAKE = 0,
    S20_GAME_SPACE = 1,
    S20_GAME_BANTUMI = 2,
    S20_GAME_PAIRS = 3
} s20_game_id_t;

typedef enum {
    S20_GVIEW_MAIN = 0,
    S20_GVIEW_GAME_MENU,
    S20_GVIEW_SETTINGS,
    S20_GVIEW_SETTING_TOGGLE,
    S20_GVIEW_LEVEL,
    S20_GVIEW_MAZES,
    S20_GVIEW_TOPSCORE,
    S20_GVIEW_INSTRUCTIONS,
    S20_GVIEW_PLAYING,
    S20_GVIEW_GAMEOVER
} s20_gview_t;

typedef struct {
    int8_t x, y;
} s20_pt8_t;

typedef struct {
    bool active;
    float x, y;
    float vx, vy;
    uint8_t kind; /* 0=player bullet, 1=enemy bullet, 2=missile, 3=laser, 4=wide */
} s20_si_shot_t;

typedef struct {
    bool active;
    float x, y;
    uint8_t spr_base;
    uint8_t hp;
    uint8_t path_id;
    uint8_t path_pos;
    uint8_t shoot_cd;
    uint8_t drop_kind; /* 0=none, 1=heart, 2=laser, 3=missile, 4=wide */
} s20_si_enemy_t;

typedef struct {
    bool active;
    float x, y;
    uint8_t kind; /* 1=heart, 2=laser, 3=missile, 4=wide */
} s20_si_pickup_t;

typedef struct {
    bool active;
    int x, y;
    uint8_t frame;
    uint8_t timer;
} s20_si_fx_t;

typedef struct {
    s20_gview_t view;
    s20_game_id_t game;

    /* Menus */
    int main_cursor;
    int game_cursor;
    int set_cursor;
    int toggle_cursor;
    int setting_idx; /* 0=Sounds, 1=Lights, 2=Shakes */
    int level_cursor;
    int maze_cursor;
    int instr_scroll;
    uint16_t instr_tid;

    /* Settings */
    bool sounds;
    bool lights;
    bool shakes;

    /* Per-game persistent config & top scores */
    uint8_t snake_level; /* 1..9 */
    uint8_t snake_maze;  /* 0..5 */
    uint8_t bantumi_level; /* 1..5 */
    uint8_t pairs_level;   /* 1..7 */
    bool pairs_timed;
    int32_t top_score[5]; /* 0=Snake, 1=Space, 2=Bantumi, 3=PairsTimed, 4=PairsPuzzle */
    bool paused[4];

    /* Game Over banner state */
    int32_t last_score;
    bool won_game;
    bool new_record;
    uint32_t rng;

    /* ---- Snake II state ---- */
    s20_pt8_t sn_body[180];
    int sn_len;
    bool sn_belly[180];
    uint8_t sn_dir;     /* 0=R, 1=D, 2=L, 3=U */
    uint8_t sn_qdir[2];
    int sn_qlen;
    s20_pt8_t sn_food;
    bool sn_bonus_active;
    s20_pt8_t sn_bonus;
    uint8_t sn_bonus_kind; /* 0..4 -> sprite 1..5 */
    uint8_t sn_bonus_timer;
    uint8_t sn_food_eaten;
    int32_t sn_score;
    uint32_t sn_last_step;

    /* ---- Space Impact state ---- */
    float si_px, si_py;
    int si_lives;
    uint8_t si_bonus_wep;  /* 2=laser, 3=missile, 4=wide */
    uint8_t si_bonus_ammo;
    int32_t si_score;
    uint8_t si_stage;      /* 0..7 */
    uint16_t si_wave_tick;
    uint8_t si_fire_cd;
    uint8_t si_invuln;
    bool si_boss_active;
    float si_boss_x, si_boss_y;
    int8_t si_boss_vy;
    int si_boss_hp, si_boss_max_hp;
    uint8_t si_boss_cd;
    s20_si_shot_t si_shots[24];
    s20_si_enemy_t si_enemies[12];
    s20_si_pickup_t si_pickups[4];
    s20_si_fx_t si_fx[8];
    uint32_t si_last_tick;

    /* ---- Bantumi state ---- */
    uint8_t bn_pits[14]; /* 0..5 P1, 6 P1 store, 7..12 P2, 13 P2 store */
    bool bn_two_player;
    uint8_t bn_turn; /* 0=P1, 1=P2 */
    int bn_cursor;   /* 0..5 for P1, 7..12 for P2 */
    uint8_t bn_arrow_frame;
    uint32_t bn_ai_tick;
    char bn_status[24];

    /* ---- Pairs II state ---- */
    uint8_t pr_cards[28];
    uint8_t pr_state[28]; /* 0=hidden, 1=flipped, 2=matched */
    int pr_cols, pr_rows, pr_total;
    int pr_cx, pr_cy;
    int pr_first_idx;
    int pr_second_idx;
    uint32_t pr_mismatch_tick;
    uint8_t pr_spark_frame;
    int pr_time_left;
    int pr_time_max;
    int pr_tries;
    int32_t pr_score;
    uint32_t pr_last_tick;
} s20_games_t;

void s20_games_init(s20_games_t *g, uint32_t seed);
void s20_games_enter(s20_games_t *g);
bool s20_games_handle_key(s20_games_t *g, s20_key_t key, s20_audio_t *aud, uint8_t vol, s20_lang_t lang);
void s20_games_tick(s20_games_t *g, uint32_t now_ms, s20_audio_t *aud, uint8_t vol);
void s20_games_render(const s20_games_t *g, s20_lcd_t *lcd, s20_lang_t lang, uint32_t now_ms);

#endif /* S20_GAMES_H */
