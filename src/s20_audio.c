#include "s20_audio.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define REPS_TICK_MS 11u

static float biquad_process(s20_biquad_t *q, float x) {
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static void biquad_hpf(s20_biquad_t *q, float fs, float fc, float Q) {
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cs = cosf(w0);
    float sn = sinf(w0);
    float alpha = sn / (2.0f * Q);
    float a0 = 1.0f + alpha;
    q->b0 = ((1.0f + cs) * 0.5f) / a0;
    q->b1 = (-(1.0f + cs)) / a0;
    q->b2 = ((1.0f + cs) * 0.5f) / a0;
    q->a1 = (-2.0f * cs) / a0;
    q->a2 = (1.0f - alpha) / a0;
    q->z1 = q->z2 = 0.0f;
}

static void biquad_lpf(s20_biquad_t *q, float fs, float fc, float Q) {
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cs = cosf(w0);
    float sn = sinf(w0);
    float alpha = sn / (2.0f * Q);
    float a0 = 1.0f + alpha;
    q->b0 = ((1.0f - cs) * 0.5f) / a0;
    q->b1 = (1.0f - cs) / a0;
    q->b2 = ((1.0f - cs) * 0.5f) / a0;
    q->a1 = (-2.0f * cs) / a0;
    q->a2 = (1.0f - alpha) / a0;
    q->z1 = q->z2 = 0.0f;
}

static void biquad_peak(s20_biquad_t *q, float fs, float fc, float Q, float gain_db) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cs = cosf(w0);
    float sn = sinf(w0);
    float alpha = sn / (2.0f * Q);
    float a0 = 1.0f + alpha / A;
    q->b0 = (1.0f + alpha * A) / a0;
    q->b1 = (-2.0f * cs) / a0;
    q->b2 = (1.0f - alpha * A) / a0;
    q->a1 = (-2.0f * cs) / a0;
    q->a2 = (1.0f - alpha / A) / a0;
    q->z1 = q->z2 = 0.0f;
}

static void setup_speaker_eq(s20_audio_t *aud) {
    float fs = (float)((aud->sample_rate > 0) ? aud->sample_rate : 44100);
    /* Miniature Nokia 3310 piezo buzzer inside plastic shell:
     * - Steep roll-off below ~550 Hz (no bass on tiny piezo disc)
     * - Enclosure mid-resonance boost around ~2.2 kHz (+4.5 dB)
     * - 4th-order roll-off above ~3.5 kHz to tame harsh digital harmonics */
    biquad_hpf(&aud->eq_hpf, fs, 550.0f, 0.707f);
    biquad_peak(&aud->eq_peak, fs, 2200.0f, 1.4f, 4.5f);
    biquad_lpf(&aud->eq_lpf1, fs, 3500.0f, 0.707f);
    biquad_lpf(&aud->eq_lpf2, fs, 3500.0f, 0.707f);
}

static float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static void set_note(s20_audio_t *aud, uint16_t freq_hz, uint32_t dur_ms) {
    aud->cur_freq = freq_hz;
    aud->samples_left = (uint32_t)(((uint64_t)dur_ms * (uint32_t)aud->sample_rate) / 1000u);
    aud->note_total_samples = aud->samples_left;
    if (freq_hz > 0 && aud->sample_rate > 0) {
        aud->phase_inc = (float)freq_hz / (float)aud->sample_rate;
        if (aud->phase_inc > 0.45f) aud->phase_inc = 0.45f;
    } else {
        aud->phase_inc = 0.0f;
    }
}

static bool advance_reps(s20_audio_t *aud) {
    if (!aud->reps_data || aud->reps_len == 0) return false;

    for (int safety = 0; safety < 256; safety++) {
        if (aud->pc >= aud->reps_len) {
            if (aud->loop) {
                aud->pc = (aud->reps_len >= 2 && aud->reps_data[0] == 0x00 && aud->reps_data[1] == 0x02) ? 2 : 0;
                aud->loop_sp = 0;
                if (aud->ascending && aud->asc_level < 5) aud->asc_level++;
                continue;
            }
            return false;
        }

        uint8_t op = aud->reps_data[aud->pc++];
        if (op == 0x00 && aud->pc == 1 && aud->pc < aud->reps_len && aud->reps_data[1] == 0x02) {
            aud->pc++;
            continue;
        }
        if (op == 0x07) {
            if (aud->loop) {
                aud->pc = (aud->reps_len >= 2 && aud->reps_data[0] == 0x00 && aud->reps_data[1] == 0x02) ? 2 : 0;
                aud->loop_sp = 0;
                if (aud->ascending && aud->asc_level < 5) aud->asc_level++;
                continue;
            }
            return false;
        }
        if (op == 0xFC || op == 0x0A) {
            if (aud->pc < aud->reps_len) aud->pc++;
            continue;
        }
        if (op == 0x09) {
            continue;
        }
        if (op == 0x05) {
            uint8_t cnt = (aud->pc < aud->reps_len) ? aud->reps_data[aud->pc++] : 1;
            if (aud->loop_sp < S20_AUDIO_MAX_LOOP_DEPTH) {
                aud->loops[aud->loop_sp].start_pc = aud->pc;
                aud->loops[aud->loop_sp].remaining = (cnt > 0) ? cnt : 1;
                aud->loop_sp++;
            }
            continue;
        }
        if (op == 0x06) {
            if (aud->loop_sp > 0) {
                s20_loop_frame_t *lf = &aud->loops[aud->loop_sp - 1];
                if (lf->remaining > 1) {
                    lf->remaining--;
                    aud->pc = lf->start_pc;
                } else {
                    aud->loop_sp--;
                }
            }
            continue;
        }
        if (op >= 0x40) {
            uint8_t dur = (aud->pc < aud->reps_len) ? aud->reps_data[aud->pc++] : 10;
            uint16_t freq = 0;
            if (op > 0x40) {
                uint8_t idx = (uint8_t)(op - 0x40);
                if (idx < S20_NUM_TONE_FREQS) {
                    freq = s20_tone_freqs[idx];
                }
            }
            uint32_t dur_ms = ((uint32_t)dur + 1u) * REPS_TICK_MS;
            set_note(aud, freq, dur_ms);
            return true;
        }
    }
    return false;
}

static bool advance_custom(s20_audio_t *aud) {
    if (aud->custom_idx >= aud->custom_count) {
        if (aud->loop && aud->custom_count > 0) {
            aud->custom_idx = 0;
            if (aud->ascending && aud->asc_level < 5) aud->asc_level++;
        } else {
            return false;
        }
    }
    uint16_t f = aud->custom_freqs[aud->custom_idx];
    uint16_t d = aud->custom_durs_ms[aud->custom_idx];
    aud->custom_idx++;
    set_note(aud, f, d);
    return true;
}

void s20_audio_init(s20_audio_t *aud, int sample_rate) {
    memset(aud, 0, sizeof(*aud));
    aud->sample_rate = (sample_rate > 0) ? sample_rate : 44100;
    aud->volume = 3;
    aud->asc_level = 3;
    setup_speaker_eq(aud);
}

void s20_audio_set_sample_rate(s20_audio_t *aud, int sample_rate) {
    if (sample_rate > 0 && sample_rate != aud->sample_rate) {
        aud->sample_rate = sample_rate;
        setup_speaker_eq(aud);
    }
}

void s20_audio_set_volume(s20_audio_t *aud, uint8_t vol, bool ascending) {
    if (vol > 5) vol = 5;
    aud->volume = vol;
    aud->ascending = ascending;
    aud->asc_level = ascending ? 1 : vol;
}

void s20_audio_play_tone(s20_audio_t *aud, int tone_idx, bool loop) {
    if (tone_idx < 0 || tone_idx >= S20_NUM_TONES) return;
    aud->mode = S20_AUD_REPS;
    aud->reps_data = s20_tones[tone_idx].data;
    aud->reps_len = s20_tones[tone_idx].len;
    aud->pc = 0;
    aud->loop = loop;
    aud->loop_sp = 0;
    aud->asc_level = aud->ascending ? 1 : aud->volume;
    aud->samples_left = 0;
    aud->phase = 0.0f;
    if (!advance_reps(aud)) {
        aud->mode = S20_AUD_IDLE;
    }
}

void s20_audio_play_custom(s20_audio_t *aud, const uint16_t *freqs,
                           const uint16_t *durs_ms, int count, bool loop) {
    if (!freqs || !durs_ms || count <= 0) return;
    if (count > S20_AUDIO_MAX_CUSTOM) count = S20_AUDIO_MAX_CUSTOM;
    memcpy(aud->custom_freqs, freqs, (size_t)count * sizeof(uint16_t));
    memcpy(aud->custom_durs_ms, durs_ms, (size_t)count * sizeof(uint16_t));
    aud->custom_count = count;
    aud->custom_idx = 0;
    aud->loop = loop;
    aud->asc_level = aud->ascending ? 1 : aud->volume;
    aud->mode = S20_AUD_CUSTOM;
    aud->samples_left = 0;
    aud->phase = 0.0f;
    if (!advance_custom(aud)) {
        aud->mode = S20_AUD_IDLE;
    }
}

void s20_audio_beep(s20_audio_t *aud, uint16_t freq_hz, uint16_t dur_ms, uint8_t vol) {
    if (vol == 0 || freq_hz == 0 || dur_ms == 0) return;
    if (vol > 5) vol = 5;
    aud->mode = S20_AUD_BEEP;
    aud->beep_vol = vol;
    aud->phase = 0.0f;
    set_note(aud, freq_hz, dur_ms);
}

void s20_audio_stop(s20_audio_t *aud) {
    aud->mode = S20_AUD_IDLE;
    aud->cur_freq = 0;
    aud->samples_left = 0;
}

bool s20_audio_is_playing(const s20_audio_t *aud) {
    return aud->mode != S20_AUD_IDLE;
}

void s20_audio_mix(s20_audio_t *aud, int16_t *out, int num_samples) {
    static const float vol_gain[6] = { 0.0f, 0.12f, 0.24f, 0.42f, 0.68f, 0.95f };
    uint32_t att_samples = (uint32_t)(aud->sample_rate / 650);  /* ~1.5 ms attack */
    uint32_t rel_samples = (uint32_t)(aud->sample_rate / 220);  /* ~4.5 ms release */
    uint32_t gap_samples = (uint32_t)(aud->sample_rate / 250);  /* ~4.0 ms articulation gap */

    for (int i = 0; i < num_samples; i++) {
        while (aud->mode != S20_AUD_IDLE && aud->samples_left == 0) {
            bool ok = false;
            if (aud->mode == S20_AUD_REPS) ok = advance_reps(aud);
            else if (aud->mode == S20_AUD_CUSTOM) ok = advance_custom(aud);
            if (!ok) {
                aud->mode = S20_AUD_IDLE;
                aud->cur_freq = 0;
                break;
            }
        }

        float raw = 0.0f;
        if (aud->mode != S20_AUD_IDLE) {
            uint8_t v = (aud->mode == S20_AUD_BEEP) ? aud->beep_vol
                      : (aud->ascending ? aud->asc_level : aud->volume);
            if (v > 5) v = 5;
            float gain = vol_gain[v];

            if (aud->cur_freq > 0 && gain > 0.0f) {
                uint32_t elapsed = aud->note_total_samples - aud->samples_left;
                bool use_gap = (aud->mode != S20_AUD_BEEP) && (aud->note_total_samples > gap_samples * 3u);
                uint32_t active_len = use_gap ? (aud->note_total_samples - gap_samples) : aud->note_total_samples;

                if (elapsed < active_len) {
                    /* PolyBLEP anti-aliased square wave */
                    float dt = aud->phase_inc;
                    float sq = (aud->phase < 0.5f) ? 1.0f : -1.0f;
                    sq += poly_blep(aud->phase, dt);
                    float p2 = aud->phase + 0.5f;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    sq -= poly_blep(p2, dt);

                    /* Smooth attack & release envelope */
                    float env = 1.0f;
                    if (att_samples > 0 && elapsed < att_samples) {
                        env = (float)elapsed / (float)att_samples;
                    } else if (rel_samples > 0 && active_len - elapsed < rel_samples) {
                        env = (float)(active_len - elapsed) / (float)rel_samples;
                    }

                    raw = sq * gain * env;
                    aud->phase += dt;
                    if (aud->phase >= 1.0f) aud->phase -= 1.0f;
                }
            }

            if (aud->samples_left > 0) aud->samples_left--;
        }

        /* Run through Nokia 3310 piezo speaker EQ even during silence so filter tails decay naturally */
        float y = biquad_process(&aud->eq_hpf, raw);
        y = biquad_process(&aud->eq_peak, y);
        y = biquad_process(&aud->eq_lpf1, y);
        y = biquad_process(&aud->eq_lpf2, y);

        /* Soft limiter (piezo mechanical saturation) */
        y = y / (1.0f + 0.35f * fabsf(y));
        int32_t s = (int32_t)(y * 16000.0f);
        if (s > 32767) s = 32767;
        else if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
}
