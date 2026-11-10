#include "s20_menu.h"
#include "s20_phone_skin.h"
#include "s20_platform.h"
#ifdef _WIN32
#  define SDL_MAIN_HANDLED
#endif
#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  else
#    include <SDL.h>
#  endif
#else
#  include <SDL.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SCALE 8
#define PAD   16
#define WIN_W (S20_LCD_WIDTH * SCALE + PAD * 2)
#define WIN_H (S20_LCD_HEIGHT * SCALE + PAD * 2)

#define PHONE_WIN_W 375
#define PHONE_WIN_H 854

typedef struct {
    int cx, cy;
    int rx, ry;
    s20_key_t key;
} phone_btn_t;

static const phone_btn_t g_phone_btns[] = {
    { 252,  616, 90, 23, S20_KEY_SELECT }, /* Blue Navi key */
    { 129,  658, 44, 38, S20_KEY_BACK },   /* C */
    { 388,  652, 42, 36, S20_KEY_UP },     /* > (Up) */
    { 316,  695, 42, 36, S20_KEY_DOWN },   /* < (Down) */
    { 110,  768, 43, 26, S20_KEY_1 },
    { 250,  787, 46, 23, S20_KEY_2 },
    { 392,  769, 43, 25, S20_KEY_3 },
    { 115,  848, 43, 26, S20_KEY_4 },
    { 250,  869, 46, 23, S20_KEY_5 },
    { 386,  849, 43, 25, S20_KEY_6 },
    { 120,  928, 43, 26, S20_KEY_7 },
    { 250,  948, 46, 22, S20_KEY_8 },
    { 381,  929, 43, 25, S20_KEY_9 },
    { 126, 1008, 43, 26, S20_KEY_STAR },
    { 250, 1029, 46, 23, S20_KEY_0 },
    { 376, 1009, 43, 25, S20_KEY_HASH },
};
#define NUM_PHONE_BTNS ((int)(sizeof(g_phone_btns) / sizeof(g_phone_btns[0])))

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_Texture *skin_tex;
    uint32_t pixels[S20_LCD_WIDTH * S20_LCD_HEIGHT];
    bool pixel_grid;
    bool phone_mode;
    bool debug_buttons;
    int mouse_sx;
    int mouse_sy;
    int hover_btn;
    int pressed_btn;
    uint32_t next_repeat_tick;
    SDL_AudioDeviceID audio_dev;
} sdl_ctx_t;

static uint32_t sdl_get_ticks(void *user) {
    (void)user;
    return SDL_GetTicks();
}

static bool sdl_get_local_time(uint8_t *hour, uint8_t *minute, uint8_t *second, void *user) {
    (void)user;
    time_t now = time(NULL);
#if defined(_MSC_VER)
    struct tm tm_buf;
    if (localtime_s(&tm_buf, &now) != 0) return false;
    struct tm *tm_now = &tm_buf;
#else
    struct tm *tm_now = localtime(&now);
    if (!tm_now) return false;
#endif
    if (hour) *hour = (uint8_t)tm_now->tm_hour;
    if (minute) *minute = (uint8_t)tm_now->tm_min;
    if (second) *second = (uint8_t)tm_now->tm_sec;
    return true;
}

static void sdl_audio_lock(void *user) {
    sdl_ctx_t *ctx = (sdl_ctx_t *)user;
    if (ctx->audio_dev) SDL_LockAudioDevice(ctx->audio_dev);
}

static void sdl_audio_unlock(void *user) {
    sdl_ctx_t *ctx = (sdl_ctx_t *)user;
    if (ctx->audio_dev) SDL_UnlockAudioDevice(ctx->audio_dev);
}

static void sdl_audio_callback(void *userdata, Uint8 *stream, int len) {
    s20_ui_t *ui = (s20_ui_t *)userdata;
    int samples = len / (int)sizeof(int16_t);
    s20_audio_mix(&ui->audio, (int16_t *)stream, samples);
}

static uint32_t rgb565_to_argb(uint16_t c) {
    if (c == S20_SKIN_TRANSPARENT) return 0x00000000u;
    uint32_t r = ((c >> 11) & 0x1Fu);
    uint32_t g = ((c >> 5) & 0x3Fu);
    uint32_t b = (c & 0x1Fu);
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static SDL_Texture *create_phone_skin_texture(SDL_Renderer *ren) {
    size_t total = (size_t)S20_SKIN_W * (size_t)S20_SKIN_H;
    uint32_t *argb = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (!argb) return NULL;

    size_t out_i = 0;
    size_t in_i = 0;
    while (in_i < (size_t)S20_SKIN_PACKED_LEN && out_i < total) {
        uint16_t hdr = s20_phone_skin_packed[in_i++];
        if (hdr & 0x8000u) {
            uint16_t cnt = hdr & 0x7FFFu;
            uint32_t px = rgb565_to_argb(s20_phone_skin_packed[in_i++]);
            for (uint16_t k = 0; k < cnt && out_i < total; k++) {
                argb[out_i++] = px;
            }
        } else {
            uint16_t cnt = hdr;
            for (uint16_t k = 0; k < cnt && out_i < total && in_i < (size_t)S20_SKIN_PACKED_LEN; k++) {
                argb[out_i++] = rgb565_to_argb(s20_phone_skin_packed[in_i++]);
            }
        }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, S20_SKIN_W, S20_SKIN_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (tex) {
        SDL_UpdateTexture(tex, NULL, argb, S20_SKIN_W * (int)sizeof(uint32_t));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    free(argb);
    return tex;
}

static void fill_btn_ellipse(SDL_Renderer *ren, const phone_btn_t *b,
                             uint8_t r, uint8_t g, uint8_t bl, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, bl, a);
    int rxx = b->rx * b->rx;
    int ryy = b->ry * b->ry;
    for (int dy = -b->ry; dy <= b->ry; dy++) {
        int span_sq = rxx * (ryy - dy * dy);
        if (span_sq <= 0) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) * ryy <= span_sq) dx++;
        SDL_RenderDrawLine(ren, b->cx - dx, b->cy + dy, b->cx + dx, b->cy + dy);
    }
}

static void draw_debug_buttons(SDL_Renderer *ren, const sdl_ctx_t *ctx) {
    for (int i = 0; i < NUM_PHONE_BTNS; i++) {
        const phone_btn_t *b = &g_phone_btns[i];
        bool active = (i == ctx->hover_btn || i == ctx->pressed_btn);
        fill_btn_ellipse(ren, b, active ? 0 : 255, active ? 220 : 60, active ? 120 : 60, 95);
        /* Center crosshair */
        SDL_SetRenderDrawColor(ren, 255, 255, 0, 230);
        SDL_RenderDrawLine(ren, b->cx - 6, b->cy, b->cx + 6, b->cy);
        SDL_RenderDrawLine(ren, b->cx, b->cy - 6, b->cx, b->cy + 6);
    }
    /* Mouse cursor marker in skin coordinates */
    SDL_SetRenderDrawColor(ren, 0, 255, 255, 255);
    SDL_Rect cur = { ctx->mouse_sx - 3, ctx->mouse_sy - 3, 7, 7 };
    SDL_RenderFillRect(ren, &cur);
}

static void sdl_present(const uint8_t fb[S20_FB_SIZE], bool backlight, void *user) {
    sdl_ctx_t *ctx = (sdl_ctx_t *)user;

    uint32_t bg_col = backlight ? 0xFFBBE8B0u : 0xFF7D9A68u;
    uint32_t fg_col = backlight ? 0xFF192816u : 0xFF23301Eu;
    uint32_t bezel  = ctx->phone_mode ? 0xFF14181Eu : 0xFF1E2428u;

    for (int y = 0; y < S20_LCD_HEIGHT; y++) {
        for (int x = 0; x < S20_LCD_WIDTH; x++) {
            uint8_t byte = fb[(y >> 3) * S20_LCD_WIDTH + x];
            bool on = (byte & (1u << (y & 7))) != 0;
            ctx->pixels[y * S20_LCD_WIDTH + x] = on ? fg_col : bg_col;
        }
    }

    SDL_UpdateTexture(ctx->tex, NULL, ctx->pixels, S20_LCD_WIDTH * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(ctx->ren,
                           (bezel >> 16) & 0xFF, (bezel >> 8) & 0xFF, bezel & 0xFF, 255);
    SDL_RenderClear(ctx->ren);

    SDL_Rect dst;
    if (ctx->phone_mode && ctx->skin_tex) {
        SDL_SetRenderDrawColor(ctx->ren,
                               (bg_col >> 16) & 0xFF, (bg_col >> 8) & 0xFF, bg_col & 0xFF, 255);
        SDL_Rect glass = { S20_SKIN_CUT_X, S20_SKIN_CUT_Y, S20_SKIN_CUT_W, S20_SKIN_CUT_H };
        SDL_RenderFillRect(ctx->ren, &glass);

        dst = (SDL_Rect){ S20_SKIN_LCD_X, S20_SKIN_LCD_Y, S20_SKIN_LCD_W, S20_SKIN_LCD_H };
        SDL_RenderCopy(ctx->ren, ctx->tex, NULL, &dst);

        if (ctx->pixel_grid) {
            SDL_SetRenderDrawBlendMode(ctx->ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx->ren, 0, 0, 0, 24);
            for (int x = 1; x < S20_LCD_WIDTH; x++) {
                int sx = dst.x + (x * dst.w) / S20_LCD_WIDTH;
                SDL_RenderDrawLine(ctx->ren, sx, dst.y, sx, dst.y + dst.h - 1);
            }
            for (int y = 1; y < S20_LCD_HEIGHT; y++) {
                int sy = dst.y + (y * dst.h) / S20_LCD_HEIGHT;
                SDL_RenderDrawLine(ctx->ren, dst.x, sy, dst.x + dst.w - 1, sy);
            }
        }

        SDL_RenderCopy(ctx->ren, ctx->skin_tex, NULL, NULL);

        if (ctx->debug_buttons) {
            draw_debug_buttons(ctx->ren, ctx);
        } else {
            if (ctx->hover_btn >= 0 && ctx->hover_btn != ctx->pressed_btn) {
                fill_btn_ellipse(ctx->ren, &g_phone_btns[ctx->hover_btn], 255, 255, 255, 40);
            }
            if (ctx->pressed_btn >= 0) {
                fill_btn_ellipse(ctx->ren, &g_phone_btns[ctx->pressed_btn], 20, 40, 75, 95);
            }
        }
    } else {
        dst = (SDL_Rect){ PAD, PAD, S20_LCD_WIDTH * SCALE, S20_LCD_HEIGHT * SCALE };
        SDL_RenderCopy(ctx->ren, ctx->tex, NULL, &dst);

        if (ctx->pixel_grid) {
            SDL_SetRenderDrawBlendMode(ctx->ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx->ren, 0, 0, 0, 28);
            for (int x = 1; x < S20_LCD_WIDTH; x++) {
                int sx = PAD + x * SCALE;
                SDL_RenderDrawLine(ctx->ren, sx, PAD, sx, PAD + S20_LCD_HEIGHT * SCALE);
            }
            for (int y = 1; y < S20_LCD_HEIGHT; y++) {
                int sy = PAD + y * SCALE;
                SDL_RenderDrawLine(ctx->ren, PAD, sy, PAD + S20_LCD_WIDTH * SCALE, sy);
            }
        }
    }

    SDL_RenderPresent(ctx->ren);
}

static void apply_view_mode(sdl_ctx_t *ctx, bool phone_mode) {
    ctx->phone_mode = phone_mode && (ctx->skin_tex != NULL);
    ctx->hover_btn = -1;
    ctx->pressed_btn = -1;
    if (ctx->phone_mode) {
        SDL_SetWindowSize(ctx->win, PHONE_WIN_W, PHONE_WIN_H);
        SDL_RenderSetLogicalSize(ctx->ren, S20_SKIN_W, S20_SKIN_H);
    } else {
        SDL_SetWindowSize(ctx->win, WIN_W, WIN_H);
        SDL_RenderSetLogicalSize(ctx->ren, WIN_W, WIN_H);
    }
}

static int hit_test_phone_btn(int sx, int sy) {
    int best = -1;
    float best_d = 1e9f;
    for (int i = 0; i < NUM_PHONE_BTNS; i++) {
        float dx = (float)(sx - g_phone_btns[i].cx) / (float)g_phone_btns[i].rx;
        float dy = (float)(sy - g_phone_btns[i].cy) / (float)g_phone_btns[i].ry;
        float d = dx * dx + dy * dy;
        if (d <= 1.0f && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static s20_key_t map_sdl_key(SDL_Keycode sym, SDL_Keymod mod) {
    if ((mod & KMOD_SHIFT) && sym == SDLK_8) return S20_KEY_STAR;
    if ((mod & KMOD_SHIFT) && sym == SDLK_3) return S20_KEY_HASH;

    switch (sym) {
    case SDLK_UP:        return S20_KEY_UP;
    case SDLK_DOWN:      return S20_KEY_DOWN;
    case SDLK_LEFT:      return S20_KEY_LEFT;
    case SDLK_RIGHT:     return S20_KEY_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:     return S20_KEY_SELECT;
    case SDLK_BACKSPACE:
    case SDLK_ESCAPE:
    case SDLK_c:         return S20_KEY_BACK;
    case SDLK_0: case SDLK_KP_0: return S20_KEY_0;
    case SDLK_1: case SDLK_KP_1: return S20_KEY_1;
    case SDLK_2: case SDLK_KP_2: return S20_KEY_2;
    case SDLK_3: case SDLK_KP_3: return S20_KEY_3;
    case SDLK_4: case SDLK_KP_4: return S20_KEY_4;
    case SDLK_5: case SDLK_KP_5: return S20_KEY_5;
    case SDLK_6: case SDLK_KP_6: return S20_KEY_6;
    case SDLK_7: case SDLK_KP_7: return S20_KEY_7;
    case SDLK_8: case SDLK_KP_8: return S20_KEY_8;
    case SDLK_9: case SDLK_KP_9: return S20_KEY_9;
    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY: return S20_KEY_STAR;
    case SDLK_HASH:      return S20_KEY_HASH;
    default:             return S20_KEY_NONE;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef _WIN32
    SDL_SetMainReady();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    sdl_ctx_t ctx = { 0 };
    ctx.pixel_grid = true;
    ctx.phone_mode = true;
    ctx.hover_btn = -1;
    ctx.pressed_btn = -1;
    ctx.win = SDL_CreateWindow(
        "Nokia 3310 | Click keys or Keyboard | F1=Light F2=Grid F3=Phone F4=Debug L=Lang",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PHONE_WIN_W, PHONE_WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!ctx.win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    ctx.ren = SDL_CreateRenderer(ctx.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ctx.ren) {
        ctx.ren = SDL_CreateRenderer(ctx.win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ctx.ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx.win);
        SDL_Quit();
        return 1;
    }
    ctx.tex = SDL_CreateTexture(ctx.ren, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, S20_LCD_WIDTH, S20_LCD_HEIGHT);
    ctx.skin_tex = create_phone_skin_texture(ctx.ren);
    apply_view_mode(&ctx, true);

    s20_hal_t hal = {
        .present = sdl_present,
        .get_ticks_ms = sdl_get_ticks,
        .get_local_time = sdl_get_local_time,
        .audio_lock = sdl_audio_lock,
        .audio_unlock = sdl_audio_unlock,
        .user = &ctx,
    };

    s20_ui_t ui;
    s20_ui_init(&ui, &hal);

    SDL_AudioSpec want = { 0 }, have = { 0 };
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = sdl_audio_callback;
    want.userdata = &ui;
    ctx.audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (ctx.audio_dev != 0) {
        s20_audio_set_sample_rate(&ui.audio, have.freq);
        SDL_PauseAudioDevice(ctx.audio_dev, 0);
    } else {
        fprintf(stderr, "Warning: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    }

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_F1) {
                    ui.backlight = !ui.backlight;
                } else if (ev.key.keysym.sym == SDLK_F2) {
                    ctx.pixel_grid = !ctx.pixel_grid;
                } else if (ev.key.keysym.sym == SDLK_F3) {
                    apply_view_mode(&ctx, !ctx.phone_mode);
                } else if (ev.key.keysym.sym == SDLK_F4) {
                    ctx.debug_buttons = !ctx.debug_buttons;
                } else if (ev.key.keysym.sym == SDLK_l) {
                    s20_ui_set_language(&ui, (ui.lang == S20_LANG_EN) ? S20_LANG_RU : S20_LANG_EN);
                } else {
                    s20_key_t k = map_sdl_key(ev.key.keysym.sym, (SDL_Keymod)ev.key.keysym.mod);
                    if (k != S20_KEY_NONE) {
                        s20_ui_handle_key(&ui, k);
                    }
                }
            } else if (ctx.phone_mode && ev.type == SDL_MOUSEMOTION) {
                ctx.mouse_sx = ev.motion.x;
                ctx.mouse_sy = ev.motion.y;
                ctx.hover_btn = hit_test_phone_btn(ctx.mouse_sx, ctx.mouse_sy);
            } else if (ctx.phone_mode && ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                ctx.mouse_sx = ev.button.x;
                ctx.mouse_sy = ev.button.y;
                int idx = hit_test_phone_btn(ctx.mouse_sx, ctx.mouse_sy);
                ctx.pressed_btn = idx;
                if (idx >= 0) {
                    s20_ui_handle_key(&ui, g_phone_btns[idx].key);
                    ctx.next_repeat_tick = SDL_GetTicks() + 340;
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                ctx.pressed_btn = -1;
            }
        }

        if (ctx.phone_mode && ctx.pressed_btn >= 0) {
            uint32_t now = SDL_GetTicks();
            if ((int32_t)(now - ctx.next_repeat_tick) >= 0) {
                s20_ui_handle_key(&ui, g_phone_btns[ctx.pressed_btn].key);
                ctx.next_repeat_tick = now + 130;
            }
        }

        s20_ui_tick(&ui);
        SDL_Delay(16);
    }

    if (ctx.audio_dev) SDL_CloseAudioDevice(ctx.audio_dev);
    if (ctx.skin_tex) SDL_DestroyTexture(ctx.skin_tex);
    SDL_DestroyTexture(ctx.tex);
    SDL_DestroyRenderer(ctx.ren);
    SDL_DestroyWindow(ctx.win);
    SDL_Quit();
    return 0;
}
