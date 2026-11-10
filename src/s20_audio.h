#ifndef S20_AUDIO_H
#define S20_AUDIO_H

#include "s20_assets.h"
#include <stdbool.h>
#include <stdint.h>

#define S20_AUDIO_MAX_LOOP_DEPTH 4
#define S20_AUDIO_MAX_CUSTOM     64

typedef enum {
    S20_AUD_IDLE = 0,
    S20_AUD_REPS,
    S20_AUD_CUSTOM,
    S20_AUD_BEEP
} s20_audio_mode_t;

typedef struct {
    uint16_t start_pc;
    uint8_t remaining;
} s20_loop_frame_t;

typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float z1, z2;
} s20_biquad_t;

typedef struct {
    int sample_rate;
    s20_audio_mode_t mode;
    uint8_t volume;      /* 0..5 */
    bool ascending;      /* ramp volume on repeat */
    uint8_t asc_level;

    /* REPS bytecode playback state */
    const uint8_t *reps_data;
    uint16_t reps_len;
    uint16_t pc;
    bool loop;
    s20_loop_frame_t loops[S20_AUDIO_MAX_LOOP_DEPTH];
    int loop_sp;

    /* Custom sequence (Composer / alerts) */
    uint16_t custom_freqs[S20_AUDIO_MAX_CUSTOM];
    uint16_t custom_durs_ms[S20_AUDIO_MAX_CUSTOM];
    int custom_count;
    int custom_idx;

    /* Current tone generator state */
    uint16_t cur_freq;
    uint32_t samples_left;
    uint32_t note_total_samples;
    float phase;      /* 0.0 .. 1.0 */
    float phase_inc;  /* freq / sample_rate */
    uint8_t beep_vol;

    /* Phone piezo speaker EQ chain */
    s20_biquad_t eq_hpf;   /* ~550 Hz high-pass */
    s20_biquad_t eq_peak;  /* ~2.2 kHz housing resonance */
    s20_biquad_t eq_lpf1;  /* ~3.6 kHz low-pass stage 1 */
    s20_biquad_t eq_lpf2;  /* ~3.6 kHz low-pass stage 2 */
} s20_audio_t;

void s20_audio_init(s20_audio_t *aud, int sample_rate);
void s20_audio_set_sample_rate(s20_audio_t *aud, int sample_rate);
void s20_audio_set_volume(s20_audio_t *aud, uint8_t vol, bool ascending);
void s20_audio_play_tone(s20_audio_t *aud, int tone_idx, bool loop);
void s20_audio_play_custom(s20_audio_t *aud, const uint16_t *freqs,
                           const uint16_t *durs_ms, int count, bool loop);
void s20_audio_beep(s20_audio_t *aud, uint16_t freq_hz, uint16_t dur_ms, uint8_t vol);
void s20_audio_stop(s20_audio_t *aud);
bool s20_audio_is_playing(const s20_audio_t *aud);

/* Mixes mono int16 PCM samples into `out`. Thread-safe if caller holds audio lock. */
void s20_audio_mix(s20_audio_t *aud, int16_t *out, int num_samples);

#endif /* S20_AUDIO_H */
