#include "s20_menu.h"
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
#include <time.h>

#define SCALE 8
#define PAD   16
#define WIN_W (S20_LCD_WIDTH * SCALE + PAD * 2)
#define WIN_H (S20_LCD_HEIGHT * SCALE + PAD * 2)

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t pixels[S20_LCD_WIDTH * S20_LCD_HEIGHT];
    bool pixel_grid;
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

static void sdl_present(const uint8_t fb[S20_FB_SIZE], bool backlight, void *user) {
    sdl_ctx_t *ctx = (sdl_ctx_t *)user;

    uint32_t bg_col = backlight ? 0xFFC7F0D8u : 0xFF8AA394u;
    uint32_t fg_col = backlight ? 0xFF1F3824u : 0xFF2B3D2Eu;
    uint32_t bezel  = 0xFF1E2428u;

    for (int y = 0; y < S20_LCD_HEIGHT; y++) {
        for (int x = 0; x < S20_LCD_WIDTH; x++) {
            uint8_t byte = fb[(y >> 3) * S20_LCD_WIDTH + x];
            bool on = (byte & (1u << (y & 7))) != 0;
            ctx->pixels[y * S20_LCD_WIDTH + x] = on ? fg_col : bg_col;
        }
    }

    SDL_UpdateTexture(ctx->tex, NULL, ctx->pixels, S20_LCD_WIDTH * sizeof(uint32_t));
    SDL_SetRenderDrawColor(ctx->ren,
                           (bezel >> 16) & 0xFF, (bezel >> 8) & 0xFF, bezel & 0xFF, 255);
    SDL_RenderClear(ctx->ren);

    SDL_Rect dst = { PAD, PAD, S20_LCD_WIDTH * SCALE, S20_LCD_HEIGHT * SCALE };
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

    SDL_RenderPresent(ctx->ren);
}

static s20_key_t map_sdl_key(SDL_Keycode sym, SDL_Keymod mod) {
    /* Support '*' and '#' on standard keyboards (Shift+8 / Shift+3) */
    if ((mod & KMOD_SHIFT) && sym == SDLK_8) return S20_KEY_STAR;
    if ((mod & KMOD_SHIFT) && sym == SDLK_3) return S20_KEY_HASH;
    if ((mod & KMOD_SHIFT) && sym == SDLK_EQUALS) return S20_KEY_OP_ADD;

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
    case SDLK_ASTERISK:  return S20_KEY_STAR;
    case SDLK_HASH:      return S20_KEY_HASH;
    case SDLK_PLUS:
    case SDLK_KP_PLUS:   return S20_KEY_OP_ADD;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:  return S20_KEY_OP_SUB;
    case SDLK_KP_MULTIPLY: return S20_KEY_OP_MUL;
    case SDLK_SLASH:
    case SDLK_KP_DIVIDE: return S20_KEY_OP_DIV;
    case SDLK_PERIOD:
    case SDLK_COMMA:
    case SDLK_KP_PERIOD: return S20_KEY_OP_DOT;
    case SDLK_EQUALS:
    case SDLK_KP_EQUALS: return S20_KEY_OP_EQ;
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
    ctx.win = SDL_CreateWindow(
        "Nokia 3310 (NHM-5 v06.39) Series 20 | Arrows/Enter/Esc/0-9 | L=Lang F1=Light F2=Grid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
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
    SDL_RenderSetLogicalSize(ctx.ren, WIN_W, WIN_H);
    ctx.tex = SDL_CreateTexture(ctx.ren, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, S20_LCD_WIDTH, S20_LCD_HEIGHT);

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
                } else if (ev.key.keysym.sym == SDLK_l) {
                    s20_ui_set_language(&ui, (ui.lang == S20_LANG_EN) ? S20_LANG_RU : S20_LANG_EN);
                } else {
                    s20_key_t k = map_sdl_key(ev.key.keysym.sym, (SDL_Keymod)ev.key.keysym.mod);
                    if (k != S20_KEY_NONE) {
                        s20_ui_handle_key(&ui, k);
                    }
                }
            }
        }

        s20_ui_tick(&ui);
        SDL_Delay(16);
    }

    if (ctx.audio_dev) SDL_CloseAudioDevice(ctx.audio_dev);
    SDL_DestroyTexture(ctx.tex);
    SDL_DestroyRenderer(ctx.ren);
    SDL_DestroyWindow(ctx.win);
    SDL_Quit();
    return 0;
}
