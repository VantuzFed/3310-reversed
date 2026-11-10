#include "s20_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Text IDs extracted from NHM5NY06.39E */
#define TID_MENU      0x041C
#define TID_SELECT    0x0424
#define TID_BACK      0x040E
#define TID_OK        0x041F
#define TID_OPTIONS   0x0420
#define TID_CLEAR     0x0410
#define TID_EXIT      0x0416
#define TID_PLAY      0x01E0
#define TID_RINGING   0x039F
#define TID_ASCENDING 0x039B
#define TID_RING_ONCE 0x039E
#define TID_BEEP_ONCE 0x039C
#define TID_OFF       0x00FD
#define TID_STANDARD  0x04A1
#define TID_SPECIAL   0x04A0

#define TID_CALC_ADD      0x011E
#define TID_CALC_DIVIDE   0x0121
#define TID_CALC_DOMESTIC 0x0123
#define TID_CALC_RATE_HDR 0x0124
#define TID_CALC_FOREIGN  0x0126
#define TID_CALC_MULT     0x0127
#define TID_CALC_EQUALS   0x012A
#define TID_CALC_RATE     0x012B
#define TID_CALC_SUB      0x012C

static void lock_audio(const s20_ui_t *ui) {
    if (ui->hal && ui->hal->audio_lock) ui->hal->audio_lock(ui->hal->user);
}

static void unlock_audio(const s20_ui_t *ui) {
    if (ui->hal && ui->hal->audio_unlock) ui->hal->audio_unlock(ui->hal->user);
}

static void audio_stop_safe(s20_ui_t *ui) {
    lock_audio(ui);
    s20_audio_stop(&ui->audio);
    unlock_audio(ui);
}

static void audio_play_tone_safe(s20_ui_t *ui, int tone_idx, uint8_t vol, bool ascending, bool loop) {
    lock_audio(ui);
    s20_audio_set_volume(&ui->audio, vol, ascending);
    s20_audio_play_tone(&ui->audio, tone_idx, loop);
    unlock_audio(ui);
}

static void audio_beep_safe(s20_ui_t *ui, uint16_t freq, uint16_t dur_ms, uint8_t vol) {
    if (vol == 0) return;
    lock_audio(ui);
    s20_audio_beep(&ui->audio, freq, dur_ms, vol);
    unlock_audio(ui);
}

static void play_keypad_tone(s20_ui_t *ui, s20_key_t key) {
    if (ui->keypad_level == 0 || ui->keyguard) return;
    if (ui->screen == S20_SCREEN_TONE_LIST || ui->screen == S20_SCREEN_TONE_VOLUME ||
        ui->screen == S20_SCREEN_COMPOSER ||
        (ui->screen == S20_SCREEN_GAMES && ui->games.view == S20_GVIEW_PLAYING)) {
        return;
    }
    static const uint16_t dtmf_like[12] = {
        941, 697, 770, 852, 941, 1047, 1175, 1209, 1336, 1477, 880, 1000
    };
    uint16_t freq = 1047;
    if (key >= S20_KEY_0 && key <= S20_KEY_9) freq = dtmf_like[key - S20_KEY_0];
    else if (key == S20_KEY_STAR) freq = dtmf_like[10];
    else if (key == S20_KEY_HASH) freq = dtmf_like[11];
    else if (key == S20_KEY_BACK) freq = 880;
    else if (key == S20_KEY_SELECT) freq = 1175;
    audio_beep_safe(ui, freq, 32, ui->keypad_level);
}

static uint32_t get_ticks(const s20_ui_t *ui) {
    return (ui->hal && ui->hal->get_ticks_ms) ? ui->hal->get_ticks_ms(ui->hal->user) : 0;
}

static bool is_item_visible(const s20_ui_t *ui, int16_t menu_id, int item_idx) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return false;
    const s20_menu_t *m = &s20_menus[menu_id];
    if (item_idx < 1 || item_idx > m->count) return false;
    const s20_menu_item_t *it = &m->items[item_idx];

    if (it->text_id == 0xFFFF) return false;
    if (menu_id == 0 && it->text_id == 0x000A && !ui->netmonitor_enabled) {
        return false;
    }
    return true;
}

static int find_first_visible(const s20_ui_t *ui, int16_t menu_id) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return 0;
    const s20_menu_t *m = &s20_menus[menu_id];
    for (int i = 1; i <= m->count; i++) {
        if (is_item_visible(ui, menu_id, i)) return i;
    }
    return 0;
}

static int find_next_visible(const s20_ui_t *ui, int16_t menu_id, int cur, int dir) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return 0;
    const s20_menu_t *m = &s20_menus[menu_id];
    if (m->count == 0) return 0;
    int idx = cur;
    for (int step = 0; step < m->count; step++) {
        idx += dir;
        if (idx > m->count) idx = 1;
        if (idx < 1) idx = m->count;
        if (is_item_visible(ui, menu_id, idx)) return idx;
    }
    return cur;
}

static int count_visible(const s20_ui_t *ui, int16_t menu_id) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return 0;
    const s20_menu_t *m = &s20_menus[menu_id];
    int n = 0;
    for (int i = 1; i <= m->count; i++) {
        if (is_item_visible(ui, menu_id, i)) n++;
    }
    return n;
}

static int visible_ordinal(const s20_ui_t *ui, int16_t menu_id, int item_idx) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return 1;
    int n = 0;
    for (int i = 1; i <= item_idx && i <= s20_menus[menu_id].count; i++) {
        if (is_item_visible(ui, menu_id, i)) n++;
    }
    return n > 0 ? n : 1;
}

static bool is_radio_menu(const s20_menu_t *m) {
    return (m->f2 & 0x0A00) != 0 && m->count > 0 && m->items[1].u12 == 0x0042;
}

static void show_popup(s20_ui_t *ui, const char *title, const char *body,
                       uint16_t bmp_id, s20_screen_t ret_screen, uint32_t duration_ms) {
    snprintf(ui->popup_title, sizeof(ui->popup_title), "%s", title ? title : "");
    snprintf(ui->popup_body, sizeof(ui->popup_body), "%s", body ? body : "");
    ui->popup_bmp_id = bmp_id;
    ui->popup_return_screen = ret_screen;
    ui->popup_expire_tick = duration_ms ? (get_ticks(ui) + duration_ms) : 0;
    ui->screen = S20_SCREEN_POPUP;
}

static void enter_menu(s20_ui_t *ui, int16_t menu_id) {
    if (menu_id < 0 || menu_id >= S20_NUM_MENUS) return;
    if (ui->stack_depth >= S20_MAX_MENU_STACK) return;

    int first = find_first_visible(ui, menu_id);
    if (first == 0) return;

    const s20_menu_t *m = &s20_menus[menu_id];
    if (is_radio_menu(m)) {
        int r = ui->radio_state[menu_id];
        if (r >= 1 && r <= m->count && is_item_visible(ui, menu_id, r)) {
            first = r;
        }
    }

    s20_menu_frame_t *fr = &ui->stack[ui->stack_depth++];
    fr->menu_id = menu_id;
    fr->selected = (uint8_t)first;
    fr->scroll_top = 0;
    ui->anim_frame = 0;
    ui->last_anim_tick = get_ticks(ui);
    ui->menu_enter_tick = ui->last_anim_tick;
    ui->screen = S20_SCREEN_MENU;
}

static void ensure_scroll_visible(const s20_ui_t *ui, s20_menu_frame_t *fr) {
    int vis_idx = visible_ordinal(ui, fr->menu_id, fr->selected) - 1;
    if (vis_idx < fr->scroll_top) {
        fr->scroll_top = (uint8_t)vis_idx;
    } else if (vis_idx >= fr->scroll_top + 3) {
        fr->scroll_top = (uint8_t)(vis_idx - 2);
    }
}

static uint16_t comp_note_freq(const s20_comp_note_t *n) {
    /* 1..7 -> c, d, e, f, g, a, b in semitone offset from A4 (index 60 in s20_tone_freqs) */
    static const int8_t semi[7] = { 3, 5, 7, 8, 10, 12, 14 }; /* C5=63 -> C4-ish, index 60=A4 */
    if (n->pitch < 1 || n->pitch > 7) return 0;
    int idx = 60 + semi[n->pitch - 1] + (n->sharp ? 1 : 0) + ((int)n->octave - 1) * 12;
    if (idx < 60) idx = 60;
    if (idx >= S20_NUM_TONE_FREQS - 1) idx = S20_NUM_TONE_FREQS - 2;
    return s20_tone_freqs[idx];
}

static void init_default_composer_melody(s20_ui_t *ui) {
    /* Classic short riff preloaded in Composer */
    static const s20_comp_note_t def[6] = {
        { 3, 2, false, 8 }, { 2, 2, false, 8 },
        { 4, 1, true,  4 }, { 5, 1, true,  4 },
        { 1, 2, true,  8 }, { 7, 1, false, 8 }
    };
    memcpy(ui->comp_notes, def, sizeof(def));
    ui->comp_len = 6;
    ui->comp_oct = 1;
    ui->comp_dur_div = 8;
    ui->comp_sharp = false;
}

void s20_ui_init(s20_ui_t *ui, const s20_hal_t *hal) {
    memset(ui, 0, sizeof(*ui));
    ui->hal = hal;
    s20_audio_init(&ui->audio, 44100);
    ui->screen = S20_SCREEN_SPLASH;
    ui->lang = S20_LANG_EN;
    ui->backlight = true;
    ui->signal_level = 5;
    ui->battery_level = 5;
    ui->active_tone_idx = 12; /* Nokia tune */
    ui->tone_volume = 3;
    ui->call_alert = 0;       /* Ringing */
    ui->msg_alert = 0;        /* Standard */
    ui->keypad_level = 1;
    ui->warning_tones = true;
    ui->calc_rate = 1.0;
    ui->menu_enter_tick = get_ticks(ui);
    init_default_composer_melody(ui);
    uint32_t seed = 0x3310BEEFu ^ ui->menu_enter_tick;
    uint8_t hh = 0, mm = 0, ss = 0;
    if (hal && hal->get_local_time && hal->get_local_time(&hh, &mm, &ss, hal->user)) {
        seed ^= ((uint32_t)hh * 3600u + (uint32_t)mm * 60u + (uint32_t)ss) * 2654435761u;
    }
    s20_games_init(&ui->games, seed);

    for (int i = 0; i < S20_NUM_MENUS; i++) {
        ui->radio_state[i] = 1;
    }
}

void s20_ui_set_language(s20_ui_t *ui, s20_lang_t lang) {
    ui->lang = lang;
}

static void preview_alert_mode(s20_ui_t *ui, s20_alert_mode_t mode, int cursor) {
    if (mode == S20_ALERT_CALL) {
        if (cursor == 0) { /* Ringing */
            audio_play_tone_safe(ui, ui->active_tone_idx, ui->tone_volume, false, true);
        } else if (cursor == 1) { /* Ascending */
            audio_play_tone_safe(ui, ui->active_tone_idx, ui->tone_volume, true, true);
        } else if (cursor == 2) { /* Ring once */
            audio_play_tone_safe(ui, ui->active_tone_idx, ui->tone_volume, false, false);
        } else if (cursor == 3) { /* Beep once */
            audio_beep_safe(ui, 1480, 160, ui->tone_volume);
        } else {
            audio_stop_safe(ui);
        }
    } else if (mode == S20_ALERT_MSG) {
        if (cursor == 0) { /* Standard SMS beep */
            static const uint16_t f[4] = { 2093, 0, 2093, 0 };
            static const uint16_t d[4] = { 120, 80, 120, 200 };
            lock_audio(ui);
            s20_audio_set_volume(&ui->audio, ui->tone_volume, false);
            s20_audio_play_custom(&ui->audio, f, d, 4, false);
            unlock_audio(ui);
        } else if (cursor == 1) { /* Special SMS */
            static const uint16_t f[6] = { 1319, 1568, 2093, 0, 1568, 2093 };
            static const uint16_t d[6] = { 90, 90, 140, 60, 90, 180 };
            lock_audio(ui);
            s20_audio_set_volume(&ui->audio, ui->tone_volume, false);
            s20_audio_play_custom(&ui->audio, f, d, 6, false);
            unlock_audio(ui);
        } else if (cursor == 2) {
            audio_beep_safe(ui, 1760, 140, ui->tone_volume);
        } else if (cursor == 3) {
            static const uint16_t f[3] = { 1047, 1319, 1760 };
            static const uint16_t d[3] = { 120, 120, 180 };
            lock_audio(ui);
            s20_audio_set_volume(&ui->audio, ui->tone_volume, true);
            s20_audio_play_custom(&ui->audio, f, d, 3, false);
            unlock_audio(ui);
        } else {
            audio_stop_safe(ui);
        }
    } else if (mode == S20_ALERT_KEYPAD) {
        if (cursor > 0) {
            audio_beep_safe(ui, 1047, 60, (uint8_t)cursor);
        } else {
            audio_stop_safe(ui);
        }
    }
}

static void execute_menu_item(s20_ui_t *ui, int16_t menu_id, int item_idx) {
    const s20_menu_t *m = &s20_menus[menu_id];
    const s20_menu_item_t *it = &m->items[item_idx];
    const char *label = s20_get_string(it->text_id, ui->lang);

    /* Submenu (0x0387) */
    if (it->hnd_fn == 0x0387 && it->submenu_id >= 0) {
        enter_menu(ui, it->submenu_id);
        return;
    }

    /* Tones -> Ringing tone (0x05e0, arg 0x0e) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x000E) {
        ui->tone_cursor = ui->active_tone_idx;
        ui->tone_scroll = (ui->tone_cursor > 2) ? (ui->tone_cursor - 2) : 0;
        ui->screen = S20_SCREEN_TONE_LIST;
        audio_play_tone_safe(ui, ui->tone_cursor, ui->tone_volume, false, true);
        return;
    }

    /* Tones -> Ringing volume (0x05e0, arg 0x0d) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x000D) {
        ui->vol_cursor = ui->tone_volume;
        ui->screen = S20_SCREEN_TONE_VOLUME;
        audio_play_tone_safe(ui, ui->active_tone_idx, ui->vol_cursor, false, true);
        return;
    }

    /* Tones -> Incoming call alert (0x05e0, arg 0x4e) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x004E) {
        ui->alert_mode = S20_ALERT_CALL;
        ui->alert_cursor = ui->call_alert;
        ui->alert_scroll = (ui->alert_cursor > 2) ? (ui->alert_cursor - 2) : 0;
        ui->screen = S20_SCREEN_TONE_ALERT;
        preview_alert_mode(ui, S20_ALERT_CALL, ui->alert_cursor);
        return;
    }

    /* Tones -> Composer (0x05e0, arg 0x26) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x0026) {
        ui->screen = S20_SCREEN_COMPOSER;
        return;
    }

    /* Tones -> Message alert tone (0x05e0, arg 0x7c) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x007C) {
        ui->alert_mode = S20_ALERT_MSG;
        ui->alert_cursor = ui->msg_alert;
        ui->alert_scroll = (ui->alert_cursor > 2) ? (ui->alert_cursor - 2) : 0;
        ui->screen = S20_SCREEN_TONE_ALERT;
        preview_alert_mode(ui, S20_ALERT_MSG, ui->alert_cursor);
        return;
    }

    /* Tones -> Keypad tones (0x05e0, arg 0x0c) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x000C) {
        ui->alert_mode = S20_ALERT_KEYPAD;
        ui->alert_cursor = ui->keypad_level;
        ui->alert_scroll = (ui->alert_cursor > 2) ? (ui->alert_cursor - 2) : 0;
        ui->screen = S20_SCREEN_TONE_ALERT;
        return;
    }

    /* Warning tones (hnd_fn == 0x1a2e) */
    if (it->hnd_fn == 0x1A2E) {
        ui->radio_state[menu_id] = (uint8_t)item_idx;
        ui->warning_tones = (it->arg != 0xFF);
        if (ui->warning_tones) audio_beep_safe(ui, 1319, 100, ui->tone_volume);
        show_popup(ui, s20_get_string(m->items[0].text_id, ui->lang), label, 0x004E, S20_SCREEN_MENU, 1000);
        return;
    }

    /* Language selector (0x0394) */
    if (it->hnd_fn == 0x0394) {
        ui->lang_cursor = (int)ui->lang;
        ui->screen = S20_SCREEN_LANGUAGE;
        return;
    }

    /* Lights (6-2-5, hnd_fn == 0x1a2f) */
    if (it->hnd_fn == 0x1A2F) {
        ui->radio_state[menu_id] = (uint8_t)item_idx;
        ui->backlight = (it->arg != 0);
        show_popup(ui, label, ui->backlight ? "Lights ON" : "Lights AUTO", 0x004E, S20_SCREEN_MENU, 1200);
        return;
    }

    /* Radio option commit */
    if (is_radio_menu(m)) {
        ui->radio_state[menu_id] = (uint8_t)item_idx;
        show_popup(ui, s20_get_string(m->items[0].text_id, ui->lang), label, 0x004E, S20_SCREEN_MENU, 1100);
        return;
    }

    /* Built-in Calculator (Main Menu item 9 -> arg 0x6b) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x006B) {
        ui->calc_acc = 0.0;
        ui->calc_cur = 0.0;
        ui->calc_len = 0;
        ui->calc_buf[0] = '\0';
        ui->calc_op = 0;
        ui->calc_has_acc = false;
        ui->calc_has_op = false;
        ui->calc_fresh_result = false;
        ui->calc_error = false;
        ui->screen = S20_SCREEN_CALCULATOR;
        return;
    }

    /* Built-in Games (Main Menu item 8 -> arg 0x72) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x0072) {
        s20_games_enter(&ui->games);
        ui->screen = S20_SCREEN_GAMES;
        return;
    }

    /* Built-in Stopwatch (Clock -> Stopwatch arg 0x50) */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x0050) {
        ui->sw_running = false;
        ui->sw_accum_ms = 0;
        ui->sw_start_tick = get_ticks(ui);
        ui->screen = S20_SCREEN_STOPWATCH;
        return;
    }

    /* Welcome note */
    if (it->hnd_fn == 0x05E0 && it->arg == 0x0059) {
        show_popup(ui, label, "Nokia 3310\nSeries 20", 0x004E, S20_SCREEN_MENU, 0);
        return;
    }

    /* Default action preview popup */
    char info[128];
    snprintf(info, sizeof(info), "fn:%04X arg:%02X", it->hnd_fn, (unsigned)(it->arg & 0xFF));
    if (ui->warning_tones) audio_beep_safe(ui, 988, 70, ui->tone_volume);
    show_popup(ui, label, info, 0x0050, S20_SCREEN_MENU, 0);
}

static void handle_menu_key(s20_ui_t *ui, s20_key_t key) {
    if (ui->stack_depth <= 0) {
        ui->screen = S20_SCREEN_IDLE;
        return;
    }
    s20_menu_frame_t *fr = &ui->stack[ui->stack_depth - 1];
    const s20_menu_t *m = &s20_menus[fr->menu_id];

    switch (key) {
    case S20_KEY_UP:
        fr->selected = (uint8_t)find_next_visible(ui, fr->menu_id, fr->selected, -1);
        ensure_scroll_visible(ui, fr);
        ui->anim_frame = 0;
        ui->last_anim_tick = get_ticks(ui);
        ui->menu_enter_tick = ui->last_anim_tick;
        break;
    case S20_KEY_DOWN:
        fr->selected = (uint8_t)find_next_visible(ui, fr->menu_id, fr->selected, 1);
        ensure_scroll_visible(ui, fr);
        ui->anim_frame = 0;
        ui->last_anim_tick = get_ticks(ui);
        ui->menu_enter_tick = ui->last_anim_tick;
        break;
    case S20_KEY_SELECT:
        execute_menu_item(ui, fr->menu_id, fr->selected);
        break;
    case S20_KEY_BACK:
        ui->stack_depth--;
        if (ui->stack_depth <= 0) {
            ui->stack_depth = 0;
            ui->screen = S20_SCREEN_IDLE;
        } else {
            ui->anim_frame = 0;
            ui->last_anim_tick = get_ticks(ui);
            ui->menu_enter_tick = ui->last_anim_tick;
        }
        break;
    case S20_KEY_1: case S20_KEY_2: case S20_KEY_3:
    case S20_KEY_4: case S20_KEY_5: case S20_KEY_6:
    case S20_KEY_7: case S20_KEY_8: case S20_KEY_9: {
        int target_ord = (int)(key - S20_KEY_1) + 1;
        int n = 0;
        for (int i = 1; i <= m->count; i++) {
            if (is_item_visible(ui, fr->menu_id, i)) {
                n++;
                if (n == target_ord) {
                    fr->selected = (uint8_t)i;
                    ensure_scroll_visible(ui, fr);
                    execute_menu_item(ui, fr->menu_id, i);
                    break;
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

static void handle_tone_list_key(s20_ui_t *ui, s20_key_t key) {
    if (key == S20_KEY_UP) {
        ui->tone_cursor = (ui->tone_cursor + S20_NUM_TONES - 1) % S20_NUM_TONES;
        if (ui->tone_cursor < ui->tone_scroll) ui->tone_scroll = ui->tone_cursor;
        if (ui->tone_cursor >= ui->tone_scroll + 3) ui->tone_scroll = ui->tone_cursor - 2;
        ui->menu_enter_tick = get_ticks(ui);
        audio_play_tone_safe(ui, ui->tone_cursor, ui->tone_volume, false, true);
    } else if (key == S20_KEY_DOWN) {
        ui->tone_cursor = (ui->tone_cursor + 1) % S20_NUM_TONES;
        if (ui->tone_cursor < ui->tone_scroll) ui->tone_scroll = ui->tone_cursor;
        if (ui->tone_cursor >= ui->tone_scroll + 3) ui->tone_scroll = ui->tone_cursor - 2;
        ui->menu_enter_tick = get_ticks(ui);
        audio_play_tone_safe(ui, ui->tone_cursor, ui->tone_volume, false, true);
    } else if (key == S20_KEY_SELECT) {
        ui->active_tone_idx = (uint8_t)ui->tone_cursor;
        audio_stop_safe(ui);
        show_popup(ui, s20_tones[ui->active_tone_idx].name, "Selected", 0x004E, S20_SCREEN_MENU, 1000);
    } else if (key == S20_KEY_BACK) {
        audio_stop_safe(ui);
        ui->screen = S20_SCREEN_MENU;
    }
}

static void handle_tone_volume_key(s20_ui_t *ui, s20_key_t key) {
    if (key == S20_KEY_UP || key == S20_KEY_DOWN) {
        if (key == S20_KEY_DOWN && ui->vol_cursor > 1) ui->vol_cursor--;
        else if (key == S20_KEY_UP && ui->vol_cursor < 5) ui->vol_cursor++;
        audio_play_tone_safe(ui, ui->active_tone_idx, ui->vol_cursor, false, true);
    } else if (key == S20_KEY_SELECT) {
        ui->tone_volume = ui->vol_cursor;
        audio_stop_safe(ui);
        char b[24];
        snprintf(b, sizeof(b), "Level %u", (unsigned)ui->tone_volume);
        show_popup(ui, s20_get_string(0x0243, ui->lang), b, 0x004E, S20_SCREEN_MENU, 900);
    } else if (key == S20_KEY_BACK) {
        audio_stop_safe(ui);
        ui->screen = S20_SCREEN_MENU;
    }
}

static int alert_option_count(s20_alert_mode_t m) {
    return (m == S20_ALERT_KEYPAD) ? 4 : 5;
}

static void handle_tone_alert_key(s20_ui_t *ui, s20_key_t key) {
    int cnt = alert_option_count(ui->alert_mode);
    if (key == S20_KEY_UP) {
        ui->alert_cursor = (ui->alert_cursor + cnt - 1) % cnt;
        if (ui->alert_cursor < ui->alert_scroll) ui->alert_scroll = ui->alert_cursor;
        if (ui->alert_cursor >= ui->alert_scroll + 3) ui->alert_scroll = ui->alert_cursor - 2;
        preview_alert_mode(ui, ui->alert_mode, ui->alert_cursor);
    } else if (key == S20_KEY_DOWN) {
        ui->alert_cursor = (ui->alert_cursor + 1) % cnt;
        if (ui->alert_cursor < ui->alert_scroll) ui->alert_scroll = ui->alert_cursor;
        if (ui->alert_cursor >= ui->alert_scroll + 3) ui->alert_scroll = ui->alert_cursor - 2;
        preview_alert_mode(ui, ui->alert_mode, ui->alert_cursor);
    } else if (key == S20_KEY_SELECT) {
        if (ui->alert_mode == S20_ALERT_CALL) ui->call_alert = (uint8_t)ui->alert_cursor;
        else if (ui->alert_mode == S20_ALERT_MSG) ui->msg_alert = (uint8_t)ui->alert_cursor;
        else ui->keypad_level = (uint8_t)ui->alert_cursor;
        audio_stop_safe(ui);
        ui->screen = S20_SCREEN_MENU;
    } else if (key == S20_KEY_BACK) {
        audio_stop_safe(ui);
        ui->screen = S20_SCREEN_MENU;
    }
}

static void handle_composer_key(s20_ui_t *ui, s20_key_t key) {
    if (key >= S20_KEY_0 && key <= S20_KEY_7) {
        if (ui->comp_len < S20_COMP_MAX_NOTES) {
            s20_comp_note_t n = {
                .pitch = (uint8_t)(key - S20_KEY_0),
                .octave = ui->comp_oct,
                .sharp = (key != S20_KEY_0 && key != S20_KEY_3 && key != S20_KEY_7) ? ui->comp_sharp : false,
                .dur_div = ui->comp_dur_div,
            };
            ui->comp_notes[ui->comp_len++] = n;
            uint16_t f = comp_note_freq(&n);
            if (f > 0) audio_beep_safe(ui, f, (uint16_t)(1600 / n.dur_div), ui->tone_volume);
        }
    } else if (key == S20_KEY_STAR) {
        ui->comp_oct = (uint8_t)((ui->comp_oct % 3) + 1);
    } else if (key == S20_KEY_HASH) {
        ui->comp_sharp = !ui->comp_sharp;
    } else if (key == S20_KEY_UP || key == S20_KEY_DOWN) {
        static const uint8_t divs[4] = { 2, 4, 8, 16 };
        int cur = 2;
        for (int i = 0; i < 4; i++) if (divs[i] == ui->comp_dur_div) cur = i;
        cur = (cur + (key == S20_KEY_UP ? 3 : 1)) & 3;
        ui->comp_dur_div = divs[cur];
    } else if (key == S20_KEY_SELECT) {
        if (ui->audio.mode == S20_AUD_CUSTOM) {
            audio_stop_safe(ui);
        } else if (ui->comp_len > 0) {
            uint16_t freqs[S20_COMP_MAX_NOTES];
            uint16_t durs[S20_COMP_MAX_NOTES];
            for (int i = 0; i < ui->comp_len; i++) {
                freqs[i] = comp_note_freq(&ui->comp_notes[i]);
                durs[i] = (uint16_t)(1800 / ui->comp_notes[i].dur_div);
            }
            lock_audio(ui);
            s20_audio_set_volume(&ui->audio, ui->tone_volume, false);
            s20_audio_play_custom(&ui->audio, freqs, durs, ui->comp_len, false);
            unlock_audio(ui);
        }
    } else if (key == S20_KEY_BACK) {
        if (s20_audio_is_playing(&ui->audio)) {
            audio_stop_safe(ui);
        } else if (ui->comp_len > 0) {
            ui->comp_len--;
        } else {
            ui->screen = S20_SCREEN_MENU;
        }
    }
}

static void calc_clear(s20_ui_t *ui) {
    ui->calc_acc = 0.0;
    ui->calc_cur = 0.0;
    ui->calc_len = 0;
    ui->calc_buf[0] = '\0';
    ui->calc_has_acc = false;
    ui->calc_has_op = false;
    ui->calc_fresh_result = false;
    ui->calc_error = false;
}

static bool calc_apply_binary(double a, double b, uint8_t op, double *out) {
    if (op == 0) *out = a + b;
    else if (op == 1) *out = a - b;
    else if (op == 2) *out = a * b;
    else {
        if (b == 0.0) return false;
        *out = a / b;
    }
    return isfinite(*out);
}

static void calc_eval(s20_ui_t *ui) {
    if (!ui->calc_has_acc || !ui->calc_has_op) return;
    double res = 0.0;
    if (!calc_apply_binary(ui->calc_acc, ui->calc_cur, ui->calc_op, &res)) {
        calc_clear(ui);
        ui->calc_error = true;
        return;
    }
    ui->calc_cur = res;
    ui->calc_acc = res;
    ui->calc_has_acc = false;
    ui->calc_has_op = false;
    ui->calc_len = 0;
    ui->calc_buf[0] = '\0';
    ui->calc_fresh_result = true;
}

static void calc_set_op(s20_ui_t *ui, uint8_t op) {
    if (ui->calc_has_acc && ui->calc_has_op && ui->calc_len > 0) {
        double res = 0.0;
        if (!calc_apply_binary(ui->calc_acc, ui->calc_cur, ui->calc_op, &res)) {
            calc_clear(ui);
            ui->calc_error = true;
            return;
        }
        ui->calc_acc = res;
        ui->calc_cur = res;
    } else {
        ui->calc_acc = ui->calc_cur;
        ui->calc_has_acc = true;
    }
    ui->calc_op = op;
    ui->calc_has_op = true;
    ui->calc_len = 0;
    ui->calc_buf[0] = '\0';
    ui->calc_fresh_result = false;
}

void s20_ui_handle_key(s20_ui_t *ui, s20_key_t key) {
    if (key == S20_KEY_NONE) return;

    play_keypad_tone(ui, key);

    if (ui->screen == S20_SCREEN_SPLASH) {
        ui->screen = S20_SCREEN_IDLE;
        return;
    }

    if (ui->screen == S20_SCREEN_POPUP) {
        audio_stop_safe(ui);
        ui->screen = ui->popup_return_screen;
        return;
    }

    if (ui->screen == S20_SCREEN_IDLE) {
        if (key == S20_KEY_SELECT && !ui->keyguard) {
            if (ui->dial_len > 0) {
                if (strcmp(ui->dial_buf, "*#0000#") == 0) {
                    show_popup(ui, "V 06.39", "23-12-04\nNHM-5", 0x004E, S20_SCREEN_IDLE, 0);
                } else if (strcmp(ui->dial_buf, "*#3310#") == 0) {
                    ui->netmonitor_enabled = !ui->netmonitor_enabled;
                    show_popup(ui, "Net monitor", ui->netmonitor_enabled ? "Enabled" : "Disabled",
                               0x004E, S20_SCREEN_IDLE, 1500);
                } else {
                    audio_play_tone_safe(ui, ui->active_tone_idx, ui->tone_volume, (ui->call_alert == 1), (ui->call_alert <= 1));
                    show_popup(ui, s20_get_string(0x0024, ui->lang), ui->dial_buf, 0x0050, S20_SCREEN_IDLE, 0);
                }
                ui->dial_len = 0;
                ui->dial_buf[0] = '\0';
                return;
            }
            ui->stack_depth = 0;
            enter_menu(ui, 0);
            return;
        }
        if (key == S20_KEY_BACK) {
            if (ui->dial_len > 0) {
                ui->dial_len--;
                ui->dial_buf[ui->dial_len] = '\0';
            }
            return;
        }
        if (key == S20_KEY_STAR && ui->dial_len == 0) {
            ui->keyguard = !ui->keyguard;
            return;
        }
        if (!ui->keyguard && ((key >= S20_KEY_0 && key <= S20_KEY_9) ||
                              key == S20_KEY_STAR || key == S20_KEY_HASH)) {
            if ((size_t)ui->dial_len + 1u < sizeof(ui->dial_buf)) {
                char ch = '0';
                if (key == S20_KEY_STAR) ch = '*';
                else if (key == S20_KEY_HASH) ch = '#';
                else ch = (char)('0' + (key - S20_KEY_0));
                ui->dial_buf[ui->dial_len++] = ch;
                ui->dial_buf[ui->dial_len] = '\0';
            }
        }
        return;
    }

    if (ui->screen == S20_SCREEN_MENU) {
        handle_menu_key(ui, key);
        return;
    }
    if (ui->screen == S20_SCREEN_TONE_LIST) {
        handle_tone_list_key(ui, key);
        return;
    }
    if (ui->screen == S20_SCREEN_TONE_VOLUME) {
        handle_tone_volume_key(ui, key);
        return;
    }
    if (ui->screen == S20_SCREEN_TONE_ALERT) {
        handle_tone_alert_key(ui, key);
        return;
    }
    if (ui->screen == S20_SCREEN_COMPOSER) {
        handle_composer_key(ui, key);
        return;
    }

    if (ui->screen == S20_SCREEN_LANGUAGE) {
        if (key == S20_KEY_UP || key == S20_KEY_DOWN) {
            ui->lang_cursor = (ui->lang_cursor == 0) ? 1 : 0;
        } else if (key == S20_KEY_SELECT) {
            ui->lang = (ui->lang_cursor == 1) ? S20_LANG_RU : S20_LANG_EN;
            ui->screen = S20_SCREEN_MENU;
        } else if (key == S20_KEY_BACK) {
            ui->screen = S20_SCREEN_MENU;
        }
        return;
    }

    if (ui->screen == S20_SCREEN_GAMES) {
        if (!s20_games_handle_key(&ui->games, key, &ui->audio,
                                  ui->games.sounds ? ui->tone_volume : 0, ui->lang)) {
            ui->screen = S20_SCREEN_MENU;
        }
        return;
    }

    if (ui->screen == S20_SCREEN_CALC_RATE) {
        if (key >= S20_KEY_0 && key <= S20_KEY_9) {
            if (ui->calc_len < 10) {
                ui->calc_buf[ui->calc_len++] = (char)('0' + (key - S20_KEY_0));
                ui->calc_buf[ui->calc_len] = '\0';
            }
        } else if (key == S20_KEY_HASH) {
            if (!strchr(ui->calc_buf, '.') && ui->calc_len < 9) {
                if (ui->calc_len == 0) ui->calc_buf[ui->calc_len++] = '0';
                ui->calc_buf[ui->calc_len++] = '.';
                ui->calc_buf[ui->calc_len] = '\0';
            }
        } else if (key == S20_KEY_BACK) {
            if (ui->calc_len > 0) {
                ui->calc_buf[--ui->calc_len] = '\0';
            } else {
                ui->screen = S20_SCREEN_CALCULATOR;
            }
        } else if (key == S20_KEY_SELECT) {
            double r = (ui->calc_len > 0) ? strtod(ui->calc_buf, NULL) : 1.0;
            if (r > 0.0 && isfinite(r)) ui->calc_rate = r;
            ui->calc_len = 0;
            ui->calc_buf[0] = '\0';
            ui->screen = S20_SCREEN_CALCULATOR;
        }
        return;
    }

    if (ui->screen == S20_SCREEN_CALC_OPTIONS) {
        if (key == S20_KEY_UP) {
            ui->calc_opt_cursor = (ui->calc_opt_cursor + 8) % 9;
        } else if (key == S20_KEY_DOWN) {
            ui->calc_opt_cursor = (ui->calc_opt_cursor + 1) % 9;
        } else if (key == S20_KEY_BACK) {
            ui->screen = S20_SCREEN_CALCULATOR;
        } else if (key == S20_KEY_SELECT) {
            int opt = ui->calc_opt_cursor;
            ui->screen = S20_SCREEN_CALCULATOR;
            if (opt == 0) {
                calc_eval(ui);
            } else if (opt >= 1 && opt <= 4) {
                calc_set_op(ui, (uint8_t)(opt - 1));
            } else if (opt == 5) {
                calc_clear(ui);
            } else if (opt == 6) {
                snprintf(ui->calc_buf, sizeof(ui->calc_buf), "%.6g", ui->calc_rate);
                ui->calc_len = (uint8_t)strlen(ui->calc_buf);
                ui->screen = S20_SCREEN_CALC_RATE;
            } else if (opt == 7 || opt == 8) {
                double r = (ui->calc_rate > 0.0) ? ui->calc_rate : 1.0;
                ui->calc_cur = (opt == 7) ? (ui->calc_cur * r) : (ui->calc_cur / r);
                ui->calc_acc = ui->calc_cur;
                ui->calc_has_acc = false;
                ui->calc_has_op = false;
                ui->calc_len = 0;
                ui->calc_buf[0] = '\0';
                ui->calc_fresh_result = true;
            }
        }
        return;
    }

    if (ui->screen == S20_SCREEN_CALCULATOR) {
        if (key >= S20_KEY_0 && key <= S20_KEY_9) {
            if (ui->calc_fresh_result || ui->calc_error) {
                ui->calc_len = 0;
                ui->calc_buf[0] = '\0';
                ui->calc_fresh_result = false;
                ui->calc_error = false;
            }
            if (ui->calc_len == 1 && ui->calc_buf[0] == '0') {
                ui->calc_len = 0;
            }
            if (ui->calc_len < 10) {
                ui->calc_buf[ui->calc_len++] = (char)('0' + (key - S20_KEY_0));
                ui->calc_buf[ui->calc_len] = '\0';
                ui->calc_cur = strtod(ui->calc_buf, NULL);
            }
        } else if (key == S20_KEY_HASH) {
            if (ui->calc_fresh_result || ui->calc_error) {
                ui->calc_len = 0;
                ui->calc_buf[0] = '\0';
                ui->calc_fresh_result = false;
                ui->calc_error = false;
            }
            if (!strchr(ui->calc_buf, '.') && ui->calc_len < 9) {
                if (ui->calc_len == 0) ui->calc_buf[ui->calc_len++] = '0';
                ui->calc_buf[ui->calc_len++] = '.';
                ui->calc_buf[ui->calc_len] = '\0';
                ui->calc_cur = strtod(ui->calc_buf, NULL);
            }
        } else if (key == S20_KEY_UP || key == S20_KEY_DOWN) {
            if (ui->calc_len > 0) {
                if (ui->calc_buf[0] == '-') {
                    memmove(ui->calc_buf, ui->calc_buf + 1, ui->calc_len);
                    ui->calc_len--;
                } else if (ui->calc_len < 10) {
                    memmove(ui->calc_buf + 1, ui->calc_buf, ui->calc_len + 1);
                    ui->calc_buf[0] = '-';
                    ui->calc_len++;
                }
                ui->calc_cur = strtod(ui->calc_buf, NULL);
            } else if (!ui->calc_has_op) {
                ui->calc_cur = -ui->calc_cur;
            }
        } else if (key == S20_KEY_STAR) {
            if (ui->calc_error) calc_clear(ui);
            if (ui->calc_has_op && ui->calc_len == 0) {
                ui->calc_op = (uint8_t)((ui->calc_op + 1) & 3);
            } else {
                calc_set_op(ui, 0);
            }
        } else if (key == S20_KEY_SELECT) {
            ui->calc_opt_cursor = 0;
            ui->screen = S20_SCREEN_CALC_OPTIONS;
        } else if (key == S20_KEY_BACK) {
            if (ui->calc_error) {
                calc_clear(ui);
            } else if (ui->calc_len > 0) {
                ui->calc_buf[--ui->calc_len] = '\0';
                ui->calc_cur = (ui->calc_len > 0 && !(ui->calc_len == 1 && ui->calc_buf[0] == '-'))
                                   ? strtod(ui->calc_buf, NULL)
                                   : 0.0;
            } else if (ui->calc_has_op) {
                ui->calc_has_op = false;
                ui->calc_has_acc = false;
                ui->calc_cur = ui->calc_acc;
            } else if (ui->calc_cur != 0.0) {
                ui->calc_cur = 0.0;
                ui->calc_fresh_result = false;
            } else {
                ui->screen = S20_SCREEN_MENU;
            }
        }
        return;
    }

    if (ui->screen == S20_SCREEN_STOPWATCH) {
        if (key == S20_KEY_SELECT) {
            uint32_t now = get_ticks(ui);
            if (ui->sw_running) {
                ui->sw_accum_ms += now - ui->sw_start_tick;
                ui->sw_running = false;
            } else {
                ui->sw_start_tick = now;
                ui->sw_running = true;
            }
        } else if (key == S20_KEY_BACK) {
            if (ui->sw_running || ui->sw_accum_ms > 0) {
                ui->sw_running = false;
                ui->sw_accum_ms = 0;
            } else {
                ui->screen = S20_SCREEN_MENU;
            }
        }
        return;
    }
}

void s20_ui_tick(s20_ui_t *ui) {
    uint32_t now = get_ticks(ui);

    if (ui->screen == S20_SCREEN_SPLASH && (now - ui->menu_enter_tick) > 1500) {
        ui->screen = S20_SCREEN_IDLE;
    }

    if (ui->screen == S20_SCREEN_POPUP && ui->popup_expire_tick != 0 &&
        (int32_t)(now - ui->popup_expire_tick) >= 0) {
        ui->screen = ui->popup_return_screen;
    }

    if (ui->screen == S20_SCREEN_GAMES) {
        lock_audio(ui);
        s20_games_tick(&ui->games, now, &ui->audio, ui->games.sounds ? ui->tone_volume : 0);
        unlock_audio(ui);
    }

    if (ui->screen == S20_SCREEN_MENU && ui->stack_depth > 0 && ui->stack[ui->stack_depth - 1].menu_id == 0) {
        s20_menu_frame_t *fr = &ui->stack[0];
        uint8_t aid = s20_menus[0].items[fr->selected].anim_id;
        if (aid < S20_NUM_ANIMS && s20_anims[aid].frame_count > 1) {
            if (now - ui->last_anim_tick >= 200) {
                ui->last_anim_tick = now;
                ui->anim_frame = (uint8_t)((ui->anim_frame + 1) % s20_anims[aid].frame_count);
            }
        }
    }

    s20_ui_render(ui);
    if (ui->hal && ui->hal->present) {
        ui->hal->present(ui->lcd.fb, ui->backlight, ui->hal->user);
    }
}

static void draw_softkey(s20_lcd_t *lcd, const char *text) {
    s20_lcd_draw_text_aligned(lcd, 0, 35, 84, 13, S20_FONT_LARGE_BOLD,
                              text, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
}

static void draw_signal_battery(const s20_ui_t *ui, s20_lcd_t *lcd) {
    int sig_bars = ui->signal_level > 4 ? 4 : ui->signal_level;
    for (int i = 0; i < sig_bars; i++) {
        int bh = 5 + i * 5;
        int by = 27 - bh;
        s20_lcd_fill_rect(lcd, 0, by, 4, 4, S20_DRAW_SET);
    }
    s20_lcd_fill_rect(lcd, 0, 29, 5, 1, S20_DRAW_SET);
    s20_lcd_fill_rect(lcd, 1, 30, 3, 1, S20_DRAW_SET);
    s20_lcd_fill_rect(lcd, 2, 31, 1, 4, S20_DRAW_SET);

    int bat_bars = ui->battery_level > 4 ? 4 : ui->battery_level;
    for (int i = 0; i < bat_bars; i++) {
        int bh = 5 + i * 5;
        int by = 27 - bh;
        s20_lcd_fill_rect(lcd, 80, by, 4, 4, S20_DRAW_SET);
    }
    s20_lcd_draw_rect_outline(lcd, 79, 29, 5, 6, S20_DRAW_SET);
    s20_lcd_fill_rect(lcd, 81, 28, 1, 1, S20_DRAW_SET);
}

static void render_idle(const s20_ui_t *ui, s20_lcd_t *lcd) {
    draw_signal_battery(ui, lcd);

    uint8_t hh = 0, mm = 0, ss = 0;
    if (ui->hal && ui->hal->get_local_time &&
        ui->hal->get_local_time(&hh, &mm, &ss, ui->hal->user)) {
        hh %= 24;
        mm %= 60;
    } else {
        uint32_t sec = get_ticks(ui) / 1000;
        uint32_t min = (12 * 60 + 34 + (sec / 60)) % (24 * 60);
        hh = (uint8_t)(min / 60);
        mm = (uint8_t)(min % 60);
    }
    char clk[8];
    snprintf(clk, sizeof(clk), "%02u:%02u", (unsigned)hh, (unsigned)mm);
    s20_lcd_draw_text_aligned(lcd, 6, 1, 72, 7, S20_FONT_TINY_PLAIN, clk,
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);

    if (ui->dial_len > 0) {
        s20_lcd_draw_text_aligned(lcd, 6, 14, 72, 16, S20_FONT_LARGE_BOLD,
                                  ui->dial_buf, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, s20_get_string(TID_CLEAR, ui->lang));
    } else if (ui->keyguard) {
        s20_lcd_draw_text_aligned(lcd, 6, 15, 72, 14, S20_FONT_SMALL_BOLD,
                                  "NOKIA 3310", S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, "* Unlock");
    } else {
        s20_lcd_draw_text_aligned(lcd, 6, 14, 72, 14, S20_FONT_LARGE_BOLD,
                                  "NOKIA", S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, s20_get_string(TID_MENU, ui->lang));
    }
}

static void build_breadcrumb(const s20_ui_t *ui, char *buf, size_t cap) {
    buf[0] = '\0';
    size_t pos = 0;
    for (int d = 0; d < ui->stack_depth; d++) {
        int ord = visible_ordinal(ui, ui->stack[d].menu_id, ui->stack[d].selected);
        int n = snprintf(buf + pos, cap - pos, "%s%d", d > 0 ? "-" : "", ord);
        if (n > 0 && pos + (size_t)n < cap) pos += (size_t)n;
    }
}

static void render_list_row(const s20_ui_t *ui, s20_lcd_t *lcd, int row, const char *txt, bool is_sel) {
    int ry = 8 + row * 9;
    if (is_sel) {
        s20_lcd_fill_rect(lcd, 0, ry, 78, 9, S20_DRAW_SET);
        int w = s20_text_first_line_width(S20_FONT_SMALL_BOLD, txt);
        int scroll_px = 0;
        if (w > 74) {
            uint32_t dt = get_ticks(ui) - ui->menu_enter_tick;
            if (dt > 700) {
                int max_s = w - 72;
                int step = (int)((dt - 700) / 80);
                scroll_px = step % (max_s + 16);
                if (scroll_px > max_s) scroll_px = max_s;
            }
        }
        s20_lcd_draw_text_aligned(lcd, 2, ry + 1, 74, 8, S20_FONT_SMALL_BOLD,
                                  txt, S20_ALIGN_LEFT, S20_DRAW_CLR, scroll_px, true);
    } else {
        s20_lcd_draw_text_aligned(lcd, 2, ry + 1, 74, 8, S20_FONT_SMALL_PLAIN,
                                  txt, S20_ALIGN_LEFT, S20_DRAW_SET, 0, true);
    }
}

static void render_menu(const s20_ui_t *ui, s20_lcd_t *lcd) {
    if (ui->stack_depth <= 0) return;
    const s20_menu_frame_t *fr = &ui->stack[ui->stack_depth - 1];
    const s20_menu_t *m = &s20_menus[fr->menu_id];
    const s20_menu_item_t *sel_item = &m->items[fr->selected];

    char crumb[32];
    build_breadcrumb(ui, crumb, sizeof(crumb));
    s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, crumb,
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);

    if (fr->menu_id == 0) {
        uint8_t aid = sel_item->anim_id;
        if (aid < S20_NUM_ANIMS && s20_anims[aid].frame_count > 0) {
            uint8_t fidx = ui->anim_frame % s20_anims[aid].frame_count;
            uint16_t bmp_id = s20_anims[aid].frames[fidx];
            const s20_bitmap_t *bmp = s20_get_bitmap(bmp_id);
            if (bmp) {
                int bx = (S20_LCD_WIDTH - bmp->width) / 2;
                s20_lcd_draw_bitmap_raw(lcd, bx, 8, bmp, S20_DRAW_SET);
            }
        }
        const char *title = s20_get_string(sel_item->text_id, ui->lang);
        int tw = s20_text_first_line_width(S20_FONT_LARGE_BOLD, title);
        s20_font_id_t tf = (tw <= 82) ? S20_FONT_LARGE_BOLD : S20_FONT_SMALL_BOLD;
        int ty = (tf == S20_FONT_LARGE_BOLD) ? 23 : 26;
        int th = (tf == S20_FONT_LARGE_BOLD) ? 13 : 9;
        s20_lcd_draw_text_aligned(lcd, 1, ty, 82, th, tf, title,
                                  S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, s20_get_string(TID_SELECT, ui->lang));
        return;
    }

    int total_vis = count_visible(ui, fr->menu_id);
    int cur_vis = visible_ordinal(ui, fr->menu_id, fr->selected) - 1;
    bool radio = is_radio_menu(m);

    int vis_idx = 0;
    int row = 0;
    for (int i = 1; i <= m->count && row < 3; i++) {
        if (!is_item_visible(ui, fr->menu_id, i)) continue;
        if (vis_idx >= fr->scroll_top) {
            bool is_sel = (i == fr->selected);
            const char *txt = s20_get_string(m->items[i].text_id, ui->lang);
            char line_buf[96];
            if (radio) {
                snprintf(line_buf, sizeof(line_buf), "%s %s",
                         (ui->radio_state[fr->menu_id] == i) ? "*" : " ", txt);
            } else {
                snprintf(line_buf, sizeof(line_buf), "%s", txt);
            }
            render_list_row(ui, lcd, row, line_buf, is_sel);
            row++;
        }
        vis_idx++;
    }

    s20_lcd_draw_scrollbar(lcd, 79, 7, 28, total_vis, cur_vis);
    draw_softkey(lcd, s20_get_string(radio ? TID_OK : TID_SELECT, ui->lang));
}

static void render_tone_list(const s20_ui_t *ui, s20_lcd_t *lcd) {
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "5-1 (%d/%d)", ui->tone_cursor + 1, S20_NUM_TONES);
    s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, hdr,
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
    for (int r = 0; r < 3; r++) {
        int idx = ui->tone_scroll + r;
        if (idx >= S20_NUM_TONES) break;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", (idx == ui->active_tone_idx) ? "*" : " ", s20_tones[idx].name);
        render_list_row(ui, lcd, r, buf, idx == ui->tone_cursor);
    }
    s20_lcd_draw_scrollbar(lcd, 79, 7, 28, S20_NUM_TONES, ui->tone_cursor);
    draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
}

static void render_tone_volume(const s20_ui_t *ui, s20_lcd_t *lcd) {
    s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, "5-2",
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
    /* 5 stepped volume bars (authentic Series 20 staircase) */
    for (int i = 0; i < 5; i++) {
        int bx = 12 + i * 13;
        int bh = 5 + i * 4;
        int by = 30 - bh;
        if (i < (int)ui->vol_cursor) {
            s20_lcd_fill_rect(lcd, bx, by, 9, bh, S20_DRAW_SET);
        } else {
            s20_lcd_draw_rect_outline(lcd, bx, by, 9, bh, S20_DRAW_SET);
        }
    }
    draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
}

static void render_tone_alert(const s20_ui_t *ui, s20_lcd_t *lcd) {
    const char *call_opts[5] = {
        s20_get_string(TID_RINGING, ui->lang),
        s20_get_string(TID_ASCENDING, ui->lang),
        s20_get_string(TID_RING_ONCE, ui->lang),
        s20_get_string(TID_BEEP_ONCE, ui->lang),
        s20_get_string(TID_OFF, ui->lang)
    };
    const char *msg_opts[5] = {
        s20_get_string(TID_STANDARD, ui->lang),
        s20_get_string(TID_SPECIAL, ui->lang),
        s20_get_string(TID_BEEP_ONCE, ui->lang),
        s20_get_string(TID_ASCENDING, ui->lang),
        s20_get_string(TID_OFF, ui->lang)
    };
    const char *key_opts[4] = {
        s20_get_string(TID_OFF, ui->lang),
        "1", "2", "3"
    };

    const char **opts = call_opts;
    int active = ui->call_alert;
    const char *crumb = "5-3";
    if (ui->alert_mode == S20_ALERT_MSG) {
        opts = msg_opts;
        active = ui->msg_alert;
        crumb = "5-5";
    } else if (ui->alert_mode == S20_ALERT_KEYPAD) {
        opts = key_opts;
        active = ui->keypad_level;
        crumb = "5-6";
    }
    int cnt = alert_option_count(ui->alert_mode);
    s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, crumb,
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
    for (int r = 0; r < 3; r++) {
        int idx = ui->alert_scroll + r;
        if (idx >= cnt) break;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", (idx == active) ? "*" : " ", opts[idx]);
        render_list_row(ui, lcd, r, buf, idx == ui->alert_cursor);
    }
    s20_lcd_draw_scrollbar(lcd, 79, 7, 28, cnt, ui->alert_cursor);
    draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
}

static void render_composer(const s20_ui_t *ui, s20_lcd_t *lcd) {
    static const char note_ch[8] = { '-', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "5-4 %d%s*%u",
             (int)ui->comp_dur_div, ui->comp_sharp ? "#" : "", (unsigned)ui->comp_oct);
    s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, hdr,
                              S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);

    char seq[128];
    size_t pos = 0;
    seq[0] = '\0';
    int start = (ui->comp_len > 10) ? (ui->comp_len - 10) : 0;
    for (int i = start; i < ui->comp_len && pos + 8 < sizeof(seq); i++) {
        const s20_comp_note_t *n = &ui->comp_notes[i];
        int k = snprintf(seq + pos, sizeof(seq) - pos, "%s%d%s%c%u",
                         i > start ? " " : "",
                         (int)n->dur_div, n->sharp ? "#" : "",
                         note_ch[n->pitch & 7], (unsigned)n->octave);
        if (k > 0) pos += (size_t)k;
    }
    s20_lcd_draw_rect_outline(lcd, 1, 9, 82, 24, S20_DRAW_SET);
    s20_lcd_draw_text_aligned(lcd, 3, 12, 78, 18, S20_FONT_SMALL_PLAIN,
                              seq[0] ? seq : "1-7 * # 0", S20_ALIGN_LEFT, S20_DRAW_SET, 0, false);

    draw_softkey(lcd, s20_audio_is_playing(&ui->audio) ? "Stop" : s20_get_string(TID_PLAY, ui->lang));
}

void s20_ui_render(s20_ui_t *ui) {
    s20_lcd_t *lcd = &ui->lcd;
    s20_lcd_clear(lcd, false);

    switch (ui->screen) {
    case S20_SCREEN_SPLASH:
        s20_lcd_draw_rect_outline(lcd, 2, 2, 80, 44, S20_DRAW_SET);
        s20_lcd_draw_text_aligned(lcd, 4, 10, 76, 14, S20_FONT_LARGE_BOLD,
                                  "NOKIA", S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        s20_lcd_draw_text_aligned(lcd, 4, 26, 76, 10, S20_FONT_SMALL_PLAIN,
                                  "Series 20", S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        break;
    case S20_SCREEN_IDLE:
        render_idle(ui, lcd);
        break;
    case S20_SCREEN_MENU:
        render_menu(ui, lcd);
        break;
    case S20_SCREEN_TONE_LIST:
        render_tone_list(ui, lcd);
        break;
    case S20_SCREEN_TONE_VOLUME:
        render_tone_volume(ui, lcd);
        break;
    case S20_SCREEN_TONE_ALERT:
        render_tone_alert(ui, lcd);
        break;
    case S20_SCREEN_COMPOSER:
        render_composer(ui, lcd);
        break;
    case S20_SCREEN_LANGUAGE: {
        const char *opts[2] = { s20_get_string(51, S20_LANG_EN), s20_get_string(51, S20_LANG_RU) };
        s20_lcd_draw_text_aligned(lcd, 0, 0, 82, 8, S20_FONT_SMALL_BOLD, "6-2-1",
                                  S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        for (int i = 0; i < 2; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", (ui->lang == (s20_lang_t)i) ? "*" : " ", opts[i]);
            render_list_row(ui, lcd, i, buf, ui->lang_cursor == i);
        }
        draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
        break;
    }
    case S20_SCREEN_CALCULATOR: {
        const char *ops = "+-*/";
        char line1[32], line2[32], acc_s[20];
        if (ui->calc_has_acc && ui->calc_has_op) {
            snprintf(acc_s, sizeof(acc_s), "%.9g", ui->calc_acc);
            snprintf(line1, sizeof(line1), "%s %c", acc_s, ops[ui->calc_op & 3]);
        } else {
            line1[0] = '\0';
        }
        if (ui->calc_error) {
            snprintf(line2, sizeof(line2), "Error");
        } else if (ui->calc_len > 0) {
            snprintf(line2, sizeof(line2), "%s", ui->calc_buf);
        } else if (ui->calc_has_op) {
            snprintf(line2, sizeof(line2), "%c", ops[ui->calc_op & 3]);
        } else {
            snprintf(line2, sizeof(line2), "%.9g", ui->calc_cur);
        }
        s20_lcd_draw_text_aligned(lcd, 2, 2, 80, 9, S20_FONT_SMALL_PLAIN,
                                  line1, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        s20_lcd_draw_text_aligned(lcd, 2, 15, 80, 14, S20_FONT_LARGE_BOLD,
                                  line2, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, s20_get_string(TID_OPTIONS, ui->lang));
        break;
    }
    case S20_SCREEN_CALC_OPTIONS: {
        static const uint16_t opt_tids[9] = {
            TID_CALC_EQUALS, TID_CALC_ADD, TID_CALC_SUB, TID_CALC_MULT,
            TID_CALC_DIVIDE, TID_CLEAR, TID_CALC_RATE, TID_CALC_DOMESTIC, TID_CALC_FOREIGN
        };
        int top = 0;
        if (ui->calc_opt_cursor >= 3) top = ui->calc_opt_cursor - 2;
        for (int i = 0; i < 3 && (top + i) < 9; i++) {
            int idx = top + i;
            render_list_row(ui, lcd, i, s20_get_string(opt_tids[idx], ui->lang),
                            ui->calc_opt_cursor == idx);
        }
        s20_lcd_draw_scrollbar(lcd, 81, 0, 34, 9, ui->calc_opt_cursor);
        draw_softkey(lcd, s20_get_string(TID_SELECT, ui->lang));
        break;
    }
    case S20_SCREEN_CALC_RATE: {
        s20_lcd_draw_text_aligned(lcd, 2, 2, 80, 10, S20_FONT_SMALL_BOLD,
                                  s20_get_string(TID_CALC_RATE_HDR, ui->lang),
                                  S20_ALIGN_LEFT, S20_DRAW_SET, 0, true);
        const char *rb = (ui->calc_len > 0) ? ui->calc_buf : "1";
        s20_lcd_draw_text_aligned(lcd, 2, 15, 80, 14, S20_FONT_LARGE_BOLD,
                                  rb, S20_ALIGN_RIGHT, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
        break;
    }
    case S20_SCREEN_GAMES: {
        s20_games_render(&ui->games, lcd, ui->lang, get_ticks(ui));
        break;
    }
    case S20_SCREEN_STOPWATCH: {
        uint32_t ms = ui->sw_accum_ms;
        if (ui->sw_running) ms += get_ticks(ui) - ui->sw_start_tick;
        uint32_t tenths = (ms / 100) % 10;
        uint32_t total_s = ms / 1000;
        uint32_t s = total_s % 60;
        uint32_t m = (total_s / 60) % 100;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u:%02u.%u", (unsigned)m, (unsigned)s, (unsigned)tenths);
        s20_lcd_draw_text_aligned(lcd, 2, 12, 80, 14, S20_FONT_LARGE_BOLD,
                                  buf, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
        draw_softkey(lcd, ui->sw_running ? "Stop" : "Start");
        break;
    }
    case S20_SCREEN_POPUP: {
        s20_lcd_draw_bitmap_id(lcd, 0, 0, 0x0048, S20_DRAW_SET);
        s20_lcd_draw_rect_outline(lcd, 0, 0, 84, 35, S20_DRAW_SET);
        if (ui->popup_bmp_id) {
            s20_lcd_draw_bitmap_id(lcd, 2, 5, ui->popup_bmp_id, S20_DRAW_SET);
            s20_lcd_draw_text_aligned(lcd, 25, 4, 56, 9, S20_FONT_SMALL_BOLD,
                                      ui->popup_title, S20_ALIGN_LEFT, S20_DRAW_SET, 0, true);
            s20_lcd_draw_text_aligned(lcd, 25, 14, 56, 18, S20_FONT_SMALL_PLAIN,
                                      ui->popup_body, S20_ALIGN_LEFT, S20_DRAW_SET, 0, false);
        } else {
            s20_lcd_draw_text_aligned(lcd, 3, 4, 78, 9, S20_FONT_SMALL_BOLD,
                                      ui->popup_title, S20_ALIGN_CENTER, S20_DRAW_SET, 0, true);
            s20_lcd_draw_text_aligned(lcd, 3, 14, 78, 18, S20_FONT_SMALL_PLAIN,
                                      ui->popup_body, S20_ALIGN_CENTER, S20_DRAW_SET, 0, false);
        }
        draw_softkey(lcd, s20_get_string(TID_OK, ui->lang));
        break;
    }
    }
}
