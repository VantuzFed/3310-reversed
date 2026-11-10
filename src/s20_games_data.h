#ifndef S20_GAMES_DATA_H
#define S20_GAMES_DATA_H

#include "s20_assets.h"
#include <stdint.h>

#define S20_SNAKE_SPRITE_CNT   39
#define S20_SI_SPRITE_CNT      83
#define S20_BANTUMI_SPRITE_CNT 15
#define S20_PAIRS_FLIP_CNT     8
#define S20_PAIRS_CARD_CNT     28
#define S20_PAIRS_MISC_CNT     9

typedef struct {
    uint8_t x0, y0, x1, y1;
} s20_snake_wall_seg_t;

typedef struct {
    uint8_t sx, sy;
    uint8_t fx, fy;
    uint8_t seg_count;
    s20_snake_wall_seg_t segs[10];
} s20_snake_maze_t;

extern const uint8_t s20_games_bmp_data[];
extern const s20_bitmap_t s20_snake_sprites[S20_SNAKE_SPRITE_CNT];
extern const uint8_t s20_snake_speeds[9];
extern const s20_snake_maze_t s20_snake_mazes[6];

extern const s20_bitmap_t s20_si_sprites[S20_SI_SPRITE_CNT];
extern const uint8_t s20_si_terrains[7][32];
extern const int8_t s20_si_paths[4][84];

extern const s20_bitmap_t s20_bantumi_sprites[S20_BANTUMI_SPRITE_CNT];

extern const s20_bitmap_t s20_pairs_flip_sprites[S20_PAIRS_FLIP_CNT];
extern const s20_bitmap_t s20_pairs_card_sprites[S20_PAIRS_CARD_CNT];
extern const s20_bitmap_t s20_pairs_misc_sprites[S20_PAIRS_MISC_CNT];

#endif
