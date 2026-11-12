/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* dsp.c – the building blocks of VOX.
   Input stage: gain, noise gate (hold + hysteresis), EQ (low cut 24 dB/oct, low shelf, bell, high shelf), the
   compressor (26.10.: one knob, four kinds – VOICE = a fast peak stage and a smooth leveling stage with a
   program-dependent release, FET, OPTO, CLEAN; automatic make-up), the de-esser (split band, relative detection: the
   sibilants against the whole voice, so loud and quiet are treated alike; no lookahead – no latency).
   Pitch: YIN on the voice, downsampled by 2, one estimate every 256 samples (~10.7 ms) – feeds the display, the
          auto-tune and the pitch engine (voice.c).
   Vocoder: a filter bank (8-24 bands, two band-passes in a row per band on voice and carrier), envelope followers,
            a high-pass path for sibilants and an automatic level so it is as loud as the dry voice.
   Carrier: 8-voice synth on the keys (saw / pulse / super saw) or IN 2.
   FX: tempo-synced ping-pong delay, Freeverb-style hall (stereo in), master limiter + soft clip. */
#include "vox.h"

static float sr;
static inline float db2lin(float db) { return vd_exp2(db * 0.16609640474f); }       /* 10^(db/20) */
static inline float lin2db(float v) { return 6.0205999f * vd_log2(v + 1e-9f); }

/* ---------------------------------------------------------------- EQ --------------------------------------------- */
/* biquads (RBJ cookbook), mono, on the voice before the compressor: low cut (two high-passes: 24 dB/oct), a low shelf at
 * 150 Hz, a bell (200 Hz .. 8 kHz, Q 0.5 .. 4), a high shelf at 8 kHz. Coefficients only when a value changed. */
typedef struct { float b0, b1, b2, a1, a2; } bq_c;
typedef struct { float z1, z2; } bq_s;
enum { EQ_HP1, EQ_HP2, EQ_LOW, EQ_MID, EQ_HIGH, EQ_N };
static bq_c eq_c[EQ_N]; static bq_s eq_s[EQ_N]; static int eq_on[EQ_N];
static float eq_last[6] = { -1, -1, -1, -1, -1, -1 };
static inline float bq_tick(const bq_c *c, bq_s *s, float x) {
  float y = c->b0 * x + s->z1; s->z1 = c->b1 * x - c->a1 * y + s->z2; s->z2 = c->b2 * x - c->a2 * y; return y;
}
static void bq_set(bq_c *c, int type, float hz, float q, float db) {     /* type 0 high-pass, 1 low shelf, 2 bell, 3 high shelf, 4 low-pass */
  float w = hz / sr, cw = vd_sin(w + 0.25f), sw = vd_sin(w), A = vd_exp2(db * 0.0830482f), al = sw / (2 * q);
  float b0, b1, b2, a0, a1, a2, sq = 2 * vd_sqrt(A) * al;
  switch (type) {
    case 0: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case 1: b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq; break;
    case 2: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
    case 4: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;   /* low-pass (the de-esser) */
    default: b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
             a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq; break;
  }
  c->b0 = b0 / a0; c->b1 = b1 / a0; c->b2 = b2 / a0; c->a1 = a1 / a0; c->a2 = a2 / a0;
}
float eq_mid_hz(float pos) { return 200.0f * vd_exp2(vd_clamp(pos, 0, 1) * 5.321928f); }   /* 200 · 40^pos */
void eq_update(void) {
  const vox_sound *s = &V.s;
  float now[6] = { s->eq_cut, s->eq_low, s->eq_mid, s->eq_mid_f, s->eq_mid_q, s->eq_high };
  int same = 1; for (int i = 0; i < 6; i++) if (now[i] != eq_last[i]) same = 0;
  if (same || sr <= 0) return;
  for (int i = 0; i < 6; i++) eq_last[i] = now[i];
  int on[EQ_N] = { s->eq_cut >= 20, s->eq_cut >= 20, vd_absf(s->eq_low) > 0.05f, vd_absf(s->eq_mid) > 0.05f, vd_absf(s->eq_high) > 0.05f };
  if (on[EQ_HP1]) { bq_set(&eq_c[EQ_HP1], 0, s->eq_cut, 0.5412f, 0); bq_set(&eq_c[EQ_HP2], 0, s->eq_cut, 1.3066f, 0); }   /* (Butterworth, 4th order) */
  if (on[EQ_LOW]) bq_set(&eq_c[EQ_LOW], 1, 150, 0.7071f, s->eq_low);
  if (on[EQ_MID]) bq_set(&eq_c[EQ_MID], 2, eq_mid_hz(s->eq_mid_f), s->eq_mid_q > 0.3f ? s->eq_mid_q : 1, s->eq_mid);
  if (on[EQ_HIGH]) bq_set(&eq_c[EQ_HIGH], 3, 8000, 0.7071f, s->eq_high);
  for (int k = 0; k < EQ_N; k++) { if (on[k] && !eq_on[k]) eq_s[k].z1 = eq_s[k].z2 = 0; eq_on[k] = on[k]; }
}
static inline float eq_tick(float x) {
  for (int k = 0; k < EQ_N; k++) if (eq_on[k]) x = bq_tick(&eq_c[k], &eq_s[k], x);
  return x;
}
float eq_response_db(float hz) {                 /* (the display: not the audio) */
  float w = hz / sr, c1 = vd_sin(w + 0.25f), s1 = vd_sin(w), c2 = vd_sin(2 * w + 0.25f), s2 = vd_sin(2 * w), db = 0;
  for (int k = 0; k < EQ_N; k++) if (eq_on[k]) {
    const bq_c *c = &eq_c[k];
    float nr = c->b0 + c->b1 * c1 + c->b2 * c2, ni = -(c->b1 * s1 + c->b2 * s2);
    float dr = 1 + c->a1 * c1 + c->a2 * c2, di = -(c->a1 * s1 + c->a2 * s2);
    db += 3.0103f * vd_log2((nr * nr + ni * ni) / (dr * dr + di * di + 1e-20f) + 1e-20f);
  }
  return db;
}

/* ---------------------------------------------------------------- input stage ------------------------------------ */
static inline float tc(float ms) { return 1 - vd_exp2(-1.442695f / (ms * 0.001f * sr)); }   /* a one-pole's step for ms */
/* soft-knee gain computer: how many dB over the threshold become reduction */
static inline float gain_reduction(float over, float ratio, float knee) {
  float s = 1 - 1 / ratio;
  if (over <= -knee * 0.5f) return 0;
  if (over < knee * 0.5f) { float k = over + knee * 0.5f; return s * k * k / (2 * knee); }
  return s * over;
}
/* the compressor: amount V.comp 0..1 on one knob. Stage 1 (peaks, FET-like) listens to the peak level, stage 2 (the
 * level, opto-like) to ~30 ms RMS after stage 1; its release is program-dependent – fast while far above its target,
 * slow near it (no pumping on a held note). The make-up: fixed by the settings. */
static float k1_pk, k1_gr, k2_ms, k2_gr, mk_avg;
static float compressor(float x) {
  float c = V.comp, t = 0;
  if (c < 0.001f) { k1_gr = k2_gr = 0; V.gr_db = 0; return x; }
  int st = (int)(V.comp_style + 0.5f);
  int one = st == CS_VOICE || st == CS_FET, two = st != CS_FET;
  if (one) {
    float thr = st == CS_FET ? -6 - 24 * c : -4 - 16 * c, ratio = st == CS_FET ? 6 : 4, knee = st == CS_FET ? 3 : 6;
    float a = vd_absf(x);
    k1_pk = a > k1_pk ? a : k1_pk + (a - k1_pk) * tc(5);
    float gr = gain_reduction(lin2db(k1_pk) - thr, ratio, knee);
    k1_gr += (gr - k1_gr) * (gr > k1_gr ? tc(st == CS_FET ? 0.8f : 2) : tc(st == CS_FET ? 50 : 60));
    x *= db2lin(-k1_gr); t += k1_gr;
  } else k1_gr = 0;
  if (two) {
    float thr = st == CS_VOICE ? -12 - 18 * c : -10 - 24 * c;
    float ratio = st == CS_VOICE ? 2.5f : st == CS_OPTO ? 3 : 1 + 4 * c, knee = st == CS_OPTO ? 12 : st == CS_CLEAN ? 6 : 10;
    k2_ms += (x * x - k2_ms) * tc(30);
    float gr = gain_reduction(3.0103f * vd_log2(k2_ms + 1e-12f) - thr, ratio, knee);
    float att = tc(st == CS_CLEAN ? 5 : st == CS_OPTO ? 10 : 15);
    float rel = st == CS_CLEAN ? tc(120) : (k2_gr - gr > 3 ? tc(80) : tc(st == CS_OPTO ? 1200 : 900));
    k2_gr += (gr - k2_gr) * (gr > k2_gr ? att : rel);
    x *= db2lin(-k2_gr); t += k2_gr;
  } else k2_gr = 0;
  /* the make-up: fixed by the settings (as on a hardware compressor) – a make-up that followed the reduction would lift
   * exactly the loud parts again and undo the leveling (measured 26.10.) */
  float mk = 0;
  if (one) mk += st == CS_FET ? 0.55f * (6 + 24 * c) * (1 - 1 / 6.0f) : 0.2f * (4 + 16 * c) * 0.75f;
  if (two) mk += 0.45f * (st == CS_VOICE ? 12 + 18 * c : 10 + 24 * c) * (1 - 1 / (st == CS_VOICE ? 2.5f : st == CS_OPTO ? 3 : 1 + 4 * c));
  mk_avg = mk;
  V.gr_db = t;
  return x * db2lin(mk);
}
/* the de-esser: the voice split at S-FREQ (a 2-pole low-pass; the high part = the rest, so both add up to the voice);
 * the high part's level against the whole voice's – over a threshold (the more DE-ESS, the lower) the high part goes
 * down, at most 3..12 dB. Attack 0.5 ms, release 40 ms. */
static bq_c ds_c; static bq_s ds_s; static float ds_f = -1, ds_hi, ds_all, ds_gr;
static float deesser(float x) {
  float amt = V.deess;
  if (amt < 0.001f) { ds_gr = 0; V.ds_db = 0; return x; }
  if (V.deess_f != ds_f) { ds_f = V.deess_f; bq_set(&ds_c, 4, ds_f, 0.7071f, 0); }
  float lo = bq_tick(&ds_c, &ds_s, x), hi = x - lo;
  float ah = vd_absf(hi), ax = vd_absf(x);
  ds_hi = ah > ds_hi ? ah : ds_hi + (ah - ds_hi) * tc(30);
  ds_all = ax > ds_all ? ax : ds_all + (ax - ds_all) * tc(30);
  float gr = 0;
  if (ds_all > 3e-4f) {                                                        /* (not on silence: -70 dB) */
    float rel = lin2db(ds_hi) - lin2db(ds_all), thr = -4 - 14 * amt, range = 3 + 9 * amt;
    gr = vd_clamp((rel - thr) * 1.5f, 0, range);
  }
  ds_gr += (gr - ds_gr) * (gr > ds_gr ? tc(0.5f) : tc(40));
  V.ds_db = ds_gr;
  return lo + hi * db2lin(-ds_gr);
}
static float g_env, g_gain, meter_rel;
static int g_hold;

float input_stage(float raw) {
  if (vd_absf(raw) > 0.99f) V.clip_until = (float)v_ms() + 1000;
  static float gain_s = 1;                                                     /* GAIN glides (~10 ms): no zipper */
  gain_s += (db2lin(V.gain_db) - gain_s) * 0.002f;
  float x = raw * gain_s;
  float a = vd_absf(x);
  V.in_peak = a > V.in_peak ? a : V.in_peak * meter_rel;
  /* gate: opens above the threshold, closes 4.4 dB below it after 60 ms – no chatter on word ends */
  g_env = a > g_env ? a : g_env * 0.9995f;
  if (V.gate_db <= -80) V.gate_open = 1;
  else {
    float thr = db2lin(V.gate_db);
    if (g_env > thr) { V.gate_open = 1; g_hold = (int)(0.06f * sr); }
    else if (g_env < thr * 0.6f) { if (g_hold > 0) g_hold--; else V.gate_open = 0; }
  }
  g_gain += ((float)V.gate_open - g_gain) * (V.gate_open ? 0.02f : 0.0004f);   /* ~1 ms open, ~50 ms close */
  x *= g_gain;
  x = eq_tick(x);                                                              /* the EQ, before the compressor */
  x = compressor(x);
  return deesser(x);
}

/* ---------------------------------------------------------------- pitch (YIN) ------------------------------------ */
#define YN 1024                       /* ring at sr/2 */
#define YW 384                        /* integration window */
#define YTMAX 360                     /* longest period: 24 kHz / 360 = 67 Hz */
#define YTMIN 20                      /* shortest: 1.2 kHz */
static float yring[YN], ylin[YW + YTMAX], yd[YTMAX + 1];
static int ypos, ycount, ydec;
static vd_svf ylp; static vd_svf_c ylpc;

static void yin(void) {
  int len = YW + YTMAX; float e = 0;
  for (int i = 0; i < len; i++) { ylin[i] = yring[(ypos - len + i) & (YN - 1)]; }
  for (int i = len - YW; i < len; i++) e += ylin[i] * ylin[i];
  float midi = 0;
  if (e / YW > 1e-5f) {                                                        /* quieter than -50 dB: no pitch */
    float cum = 0; int best = -1;
    yd[0] = 1;
    for (int t = 1; t <= YTMAX; t++) {
      float d = 0; const float *a = ylin, *b = ylin + t;
      for (int j = 0; j < YW; j++) { float q = a[j] - b[j]; d += q * q; }
      cum += d; yd[t] = cum > 0 ? d * t / cum : 1;
    }
    for (int t = YTMIN; t < YTMAX; t++) if (yd[t] < 0.15f) { while (t + 1 < YTMAX && yd[t + 1] < yd[t]) t++; best = t; break; }
    if (best < 0) { float m = 0.3f; for (int t = YTMIN; t < YTMAX; t++) if (yd[t] < m) { m = yd[t]; best = t; } }
    if (best > 0) {
      float a = yd[best - 1], b = yd[best], c = yd[best + 1], den = a - 2 * b + c;
      float tau = best + (den > 1e-9f ? 0.5f * (a - c) / den : 0);
      float f = sr * 0.5f / tau;
      midi = 69 + 12 * vd_log2(f / 440.0f);
    }
  }
  V.pitch_now = midi;
  V.pitch_hist[V.pitch_pos] = midi;
  tune_hop(midi);                                                              /* writes V.pitch_out_hist */
  V.pitch_pos = (V.pitch_pos + 1) % PITCH_HIST;
}

/* how often the pitch is measured (samples at sr/2): a strong tune twice as often (5.3 ms) – the voice laid flat between
 * two estimates too; else every 10.7 ms */
int yin_step(void) { return tune_amount() > 0.5f ? 128 : 256; }

void pitch_feed(float x) {
  vd_svf_out o = vd_svf_tick(&ylp, &ylpc, x);
  if ((ydec ^= 1)) return;
  yring[ypos] = o.lp; ypos = (ypos + 1) & (YN - 1);
  if (++ycount >= yin_step()) { ycount = 0; yin(); }
}

/* ---------------------------------------------------------------- carrier ---------------------------------------- */
typedef struct { int note, down; float freq, env, target, ph[3]; uint32_t age; } voice_t;
static voice_t vc[MAX_VOICES];
static uint32_t age; static int keys_down;
static vd_noise noise;

void carrier_note(int note, int on) {
  if (on) {
    if (V.hold && keys_down == 0) for (int i = 0; i < MAX_VOICES; i++) vc[i].target = 0;   /* a new chord replaces the held one */
    keys_down++;
    int v = -1; uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_VOICES; i++) if (vc[i].note == note && vc[i].target > 0) v = i;
    if (v < 0) for (int i = 0; i < MAX_VOICES; i++) if (vc[i].env < 0.001f && vc[i].target == 0) { v = i; break; }
    if (v < 0) for (int i = 0; i < MAX_VOICES; i++) if (vc[i].age < oldest) { oldest = vc[i].age; v = i; }
    voice_t *p = &vc[v];
    if (p->env < 0.001f) p->ph[0] = p->ph[1] = p->ph[2] = 0;
    p->note = note; p->down = 1; p->freq = vd_mtof((float)note); p->target = 1; p->age = ++age;
  } else {
    if (keys_down > 0) keys_down--;
    for (int i = 0; i < MAX_VOICES; i++) if (vc[i].note == note && vc[i].down) { vc[i].down = 0; if (!V.hold) vc[i].target = 0; }
  }
}
void carrier_release_all(void) { for (int i = 0; i < MAX_VOICES; i++) if (!vc[i].down) vc[i].target = 0; }
int carrier_notes(int *out, int max) {
  int n = 0;
  for (int i = 0; i < MAX_VOICES && n < max; i++) if (vc[i].target > 0) out[n++] = vc[i].note;
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && out[j] < out[j - 1]; j--) { int t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
  return n;
}

static float carrier_tick(float ext) {
  int type = (int)V.s.carrier;
  if (type == CAR_IN2) return ext * 2;
  float s = 0, total = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    voice_t *p = &vc[i];
    p->env += (p->target - p->env) * (p->target > p->env ? 0.004f : 0.0004f);  /* ~5 ms in, ~50 ms out */
    if (p->env < 0.0001f && p->target == 0) continue;
    float dt = p->freq / sr, o;
    if (type == CAR_SUPER) {
      static const float DET[3] = { 1.0f, 1.0069f, 0.9931f };                  /* ±12 cents */
      o = 0;
      for (int k = 0; k < 3; k++) { float d = dt * DET[k]; p->ph[k] += d; if (p->ph[k] >= 1) p->ph[k] -= 1; o += vd_saw(p->ph[k], d); }
      o *= 0.5f;
    } else {
      p->ph[0] += dt; if (p->ph[0] >= 1) p->ph[0] -= 1;
      o = type == CAR_PULSE ? vd_square(p->ph[0], dt) : vd_saw(p->ph[0], dt);
    }
    s += o * p->env; total += p->env;
  }
  return (s + vd_noise_next(&noise) * 0.08f * vd_minf(total, 1)) * 0.35f;     /* a little noise: consonants stay */
}

/* ---------------------------------------------------------------- vocoder ---------------------------------------- */
typedef struct { vd_svf m1, m2, c1, c2; float env; } band_t;
static band_t bd[MAX_BANDS];
static vd_svf_c bc[MAX_BANDS];
static int bn;
static float env_att, env_rel, voc_gain = 8, dry_ms, voc_ms;
static vd_svf sib; static vd_svf_c sibc;

static void bands_setup(int n) {
  const float lo = 110, oct = 5.992f;                                          /* 110 Hz .. 7 kHz */
  float bw = oct / n, r = vd_exp2(bw), q = vd_sqrt(r) / (r - 1) * 0.85f;
  for (int i = 0; i < n; i++) vd_svf_set(&bc[i], lo * vd_exp2(oct * (i + 0.5f) / n), q, sr);
  for (int i = bn; i < n; i++) bd[i].env = 0;
  bn = n; V.band_n = n;
}

float vocoder(float mod, float ext) {
  int n = (int)V.s.bands; if (n != bn) bands_setup(n);
  float car = carrier_tick(ext), out = 0;
  for (int i = 0; i < bn; i++) {
    band_t *b = &bd[i]; const vd_svf_c *c = &bc[i]; float k = c->k;
    float m = vd_svf_tick(&b->m1, c, mod).bp * k; m = vd_svf_tick(&b->m2, c, m).bp * k;
    float x = vd_svf_tick(&b->c1, c, car).bp * k; x = vd_svf_tick(&b->c2, c, x).bp * k;
    float r = vd_absf(m);
    b->env += (r - b->env) * (r > b->env ? env_att : env_rel);
    out += x * b->env;
  }
  /* sibilants (s, t, f) come straight from the voice: they make the words understandable */
  float s = vd_svf_tick(&sib, &sibc, mod).hp;
  /* automatic level: the vocoder as loud as the voice (follows slowly, only while someone sings) */
  dry_ms += (mod * mod - dry_ms) * 0.00005f; voc_ms += (out * out - voc_ms) * 0.00005f;
  if (dry_ms > 1e-5f && voc_ms > 1e-9f) {
    float want = vd_clamp(vd_sqrt(dry_ms / voc_ms), 1, 150);
    voc_gain += (want - voc_gain) * 0.0002f;
  }
  return out * voc_gain + s * 0.6f;
}
void vocoder_meters(void) { for (int i = 0; i < bn; i++) V.band_env[i] = bd[i].env; }

/* ---------------------------------------------------------------- delay + hall + master -------------------------- */
#define DMAX 96000                                                             /* 2 s at 48 kHz */
#define NC 4
#define PMAX 9600                                                              /* the pre-delay: 100 ms at 96 kHz */
#define NSP 6                                                                  /* spring: the chirp */
static const int CL[NC] = { 1557, 1617, 1491, 1422 }, CR[NC] = { 1580, 1640, 1514, 1445 };
static const int AL[2] = { 556, 441 }, AR[2] = { 579, 464 };
static const int DFL[2] = { 142, 379 }, DFR[2] = { 151, 367 };
static const int SPL[NSP] = { 37, 41, 43, 47, 53, 59 }, SPR[NSP] = { 39, 42, 45, 49, 55, 61 };
static const float DIVS[] = { 0.25f, 1.0f / 3, 0.5f, 0.75f, 1, 1.5f, 2 };       /* 1/16 1/8T 1/8 1/8. 1/4 1/4. 1/2 */

/* 1.6 (Sven 11.11.: "sollte es nicht die Effekte so aufnehmen wie sie sind"): TWO delay + reverb paths – the voice's
 * (its sound goes into the looper as one hears it, with delay, reverb and MOD) and the loops' (their SEND 1 / SEND 2,
 * never into a recording). Same settings, own state. Their sum is what one path gave before. */
typedef struct {
  float dl[DMAX], dr[DMAX], dtime, tone; int dw;
  float cbL[NC][1700], cbR[NC][1700], cfL[NC], cfR[NC], abL[2][600], abR[2][600]; int ciL[NC], ciR[NC], aiL[2], aiR[2];
  float pre[PMAX]; int pw;
  float dfL[2][400], dfR[2][400]; int diL[2], diR[2];
  float spL[NSP][64], spR[NSP][64]; int siL[NSP], siR[NSP];
  float sp_hp, sp_lp, sp_oL, sp_oR, dlp[2], dhp[2], wow_ph, flut_ph, ds_s, hs_s;
} fxp;
static fxp FV, FL;                                                            /* the voice's · the loops' */
static float lim = 1;

static inline float comb(float *buf, int len, int *idx, float *filt, float in, float fb, float damp) {
  float o = buf[*idx]; *filt = o * (1 - damp) + *filt * damp; buf[*idx] = in + *filt * fb; if (++*idx >= len) *idx = 0; return o;
}
static inline float allpass(float *buf, int len, int *idx, float in) {
  float b = buf[*idx], o = -in + b; buf[*idx] = in + b * 0.5f; if (++*idx >= len) *idx = 0; return o;
}
static inline float allpass_g(float *buf, int len, int *idx, float in, float g) {   /* (1.6) a diffuser with its own gain */
  float b = buf[*idx], o = b - g * in; buf[*idx] = in + g * o; if (++*idx >= len) *idx = 0; return o;
}

/* ---- 1.6 (Sven 11.11.): FX › DELAY · REVERB · MOD. The kinds: DELAY digital (clean) · analog (each repeat darker and
 * a little driven, as a bucket brigade) · tape (wow and flutter, saturation, darker) · ping-pong (VOX before 1.6, left /
 * right, the right one darker). REVERB hall (before 1.6) · room (short, dark) · plate (bright, dense – two diffusers
 * first) · spring (a chirping chain of short allpasses, thin and boingy). PRE-DELAY 0..100 ms before any of them. MOD
 * on the voice before delay and reverb: chorus (two swaying taps), flanger (a short tap with feedback), phaser (four
 * moving allpasses with feedback); AMOUNT 0 = off and nothing computed. */
#define MMAX 4096                                                              /* MOD: up to 40 ms at 96 kHz */
static float mlb[MMAX], mrb[MMAX]; static int mw;
static float mph, ph_x[2][4], ph_y[2][4], ph_fb[2];

static inline float rd_lin(const float *b, int len, float pos) {               /* a fractional tap, pos frames behind */
  int i0 = (int)pos; float fr = pos - i0; int a = i0 % len, c = (i0 + 1) % len; return b[a] + (b[c] - b[a]) * fr;
}
static inline float pole(float hz) { float c = 6.2831853f * hz / sr; return c >= 1 ? 1 : c / (1 + c); }   /* one-pole coefficient */

static void mod_process(float *xl, float *xr, float m, float rate, float depth, int type) {
  mlb[mw] = *xl; mrb[mw] = *xr;
  mph += rate / sr; if (mph >= 1) mph -= 1;
  float sL = vd_sin(mph), sR = vd_sin(mph + 0.25f < 1 ? mph + 0.25f : mph - 0.75f), yl, yr;
  if (type == MT_PHASER) {
    for (int c = 0; c < 2; c++) {
      float sw = c ? sR : sL, fc = 250.0f * vd_exp2(depth * 3.2f * (0.5f + 0.5f * sw)), w = 3.14159265f * fc / sr, a = (1 - w) / (1 + w);
      float x = (c ? *xr : *xl) + ph_fb[c] * 0.55f;
      for (int k = 0; k < 4; k++) { float y = -a * x + ph_x[c][k] + a * ph_y[c][k]; ph_x[c][k] = x; ph_y[c][k] = y; x = y; }
      ph_fb[c] = x; if (c) yr = x; else yl = x;
    }
    *xl += (yl - *xl) * 0.5f * m; *xr += (yr - *xr) * 0.5f * m;
  } else {
    float base = type == MT_FLANGER ? 0.0004f : 0.012f, span = type == MT_FLANGER ? 0.004f : 0.008f;
    float pL = (base + span * depth * (0.5f + 0.5f * sL)) * sr, pR = (base + span * depth * (0.5f + 0.5f * sR)) * sr;
    yl = rd_lin(mlb, MMAX, (float)(mw + MMAX) - pL); yr = rd_lin(mrb, MMAX, (float)(mw + MMAX) - pR);
    if (type == MT_FLANGER) { mlb[mw] += yl * 0.6f; mrb[mw] += yr * 0.6f; *xl += (yl - *xl) * 0.5f * m; *xr += (yr - *xr) * 0.5f * m; }
    else { *xl = *xl * (1 - 0.3f * m) + yl * 0.7f * m; *xr = *xr * (1 - 0.3f * m) + yr * 0.7f * m; }
  }
  if (++mw >= MMAX) mw = 0;
}

/* the block's settings, once */
typedef struct { float want, fb, ds, hs, room, damp, lp_a, hp_a, sp_hp_a, sp_lp_a; int dt, rt, pd; } fxset;
static fxset fx_settings(void) {
  fxset c;
  c.want = vd_clamp(60.0f / V.bpm * DIVS[(int)V.s.delay_div] * sr, 64, DMAX - 4);
  c.fb = V.s.feedback * 0.92f; c.ds = V.dry ? 0 : V.s.delay; c.hs = V.dry ? 0 : V.s.hall;
  c.dt = (int)(V.s.dly_type + 0.5f); c.rt = (int)(V.s.rev_type + 0.5f);
  float size = V.s.room;
  switch (c.rt) {
    case RT_ROOM:   c.room = 0.45f + size * 0.30f; c.damp = 0.45f; break;
    case RT_PLATE:  c.room = 0.80f + size * 0.18f; c.damp = 0.08f; break;
    case RT_SPRING: c.room = 0.76f + size * 0.16f; c.damp = 0.30f; break;
    default:        c.room = 0.72f + size * 0.26f; c.damp = 0.2f + (1 - size) * 0.35f; break;   /* HALL – as before 1.6 */
  }
  c.pd = (int)(vd_clamp(V.s.rev_pre, 0, 1) * 0.1f * sr); if (c.pd > PMAX - 1) c.pd = PMAX - 1;
  c.lp_a = pole(c.dt == DT_TAPE ? 4200 : c.dt == DT_ANALOG ? 2600 : 16000); c.hp_a = pole(c.dt == DT_TAPE ? 90 : 140);
  c.sp_hp_a = pole(280); c.sp_lp_a = pole(3800);
  return c;
}
/* one sample through one path: x into the delay (at DELAY) and the reverb (at REVERB), s1 / s2 straight in (the sends);
 * out: the delay's taps (0.9) + the reverb, stereo – without x itself */
static inline void fx_tick(fxp *f, const fxset *c, float x, float s1, float s2, float *oL_, float *oR_) {
  f->ds_s += (c->ds - f->ds_s) * 0.002f; f->hs_s += (c->hs - f->hs_s) * 0.002f;
  if (f->dtime == 0) f->dtime = c->want;
  f->dtime += (c->want - f->dtime) * 0.0005f;
  float t = f->dtime; int dt = c->dt, rt = c->rt;
  if (dt == DT_TAPE) {                                                       /* wow 0.6 Hz, flutter 7 Hz */
    f->wow_ph += 0.6f / sr; if (f->wow_ph >= 1) f->wow_ph -= 1; f->flut_ph += 7.0f / sr; if (f->flut_ph >= 1) f->flut_ph -= 1;
    t += (0.0012f * vd_sin(f->wow_ph) + 0.00015f * vd_sin(f->flut_ph)) * sr;
  } else if (dt == DT_ANALOG) { f->wow_ph += 0.35f / sr; if (f->wow_ph >= 1) f->wow_ph -= 1; t += 0.0002f * vd_sin(f->wow_ph) * sr; }
  float rp = f->dw - t; while (rp < 0) rp += DMAX;
  int i0 = (int)rp; float fr = rp - i0; if (i0 >= DMAX) i0 -= DMAX; int i1 = i0 + 1 >= DMAX ? 0 : i0 + 1;
  float tapL = f->dl[i0] + (f->dl[i1] - f->dl[i0]) * fr, tapR = f->dr[i0] + (f->dr[i1] - f->dr[i0]) * fr;
  float in_d = x * f->ds_s + s1;
  if (dt == DT_PINGPONG) {                                                   /* (VOX before 1.6, sample for sample) */
    f->tone += (tapL - f->tone) * 0.35f;                                     /* the right tap is darker */
    f->dl[f->dw] = in_d + tapR * c->fb; f->dr[f->dw] = f->tone;
  } else {
    float fl = tapL, fr2 = tapR;
    if (dt != DT_DIGITAL) {                                                  /* analog, tape: each repeat darker, the lows thinner, driven */
      f->dlp[0] += (fl - f->dlp[0]) * c->lp_a; f->dlp[1] += (fr2 - f->dlp[1]) * c->lp_a;
      f->dhp[0] += (f->dlp[0] - f->dhp[0]) * c->hp_a; f->dhp[1] += (f->dlp[1] - f->dhp[1]) * c->hp_a;
      float k = dt == DT_TAPE ? 1.4f : 1.15f;
      fl = vd_tanh((f->dlp[0] - f->dhp[0]) * k) / k; fr2 = vd_tanh((f->dlp[1] - f->dhp[1]) * k) / k;
    }
    f->dl[f->dw] = in_d + fl * c->fb; f->dr[f->dw] = in_d * 0.98f + fr2 * c->fb;
  }
  if (++f->dw >= DMAX) f->dw = 0;
  float in = ((x + (tapL + tapR) * 0.5f) * f->hs_s + s2) * 0.6f, oL = 0, oR = 0;
  if (c->pd) { f->pre[f->pw] = in; int q = f->pw - c->pd; if (q < 0) q += PMAX; in = f->pre[q]; if (++f->pw >= PMAX) f->pw = 0; }
  float inL = in, inR = in;
  if (rt == RT_PLATE) for (int a = 0; a < 2; a++) { inL = allpass_g(f->dfL[a], DFL[a], &f->diL[a], inL, 0.7f); inR = allpass_g(f->dfR[a], DFR[a], &f->diR[a], inR, 0.7f); }
  else if (rt == RT_SPRING) {
    f->sp_hp += (inL - f->sp_hp) * c->sp_hp_a; float h = inL - f->sp_hp; f->sp_lp += (h - f->sp_lp) * c->sp_lp_a; inL = inR = f->sp_lp;
    for (int a = 0; a < NSP; a++) { inL = allpass_g(f->spL[a], SPL[a], &f->siL[a], inL, 0.62f); inR = allpass_g(f->spR[a], SPR[a], &f->siR[a], inR, 0.62f); }
  }
  for (int k = 0; k < NC; k++) { oL += comb(f->cbL[k], CL[k], &f->ciL[k], &f->cfL[k], inL, c->room, c->damp); oR += comb(f->cbR[k], CR[k], &f->ciR[k], &f->cfR[k], inR, c->room, c->damp); }
  for (int a = 0; a < 2; a++) { oL = allpass(f->abL[a], AL[a], &f->aiL[a], oL); oR = allpass(f->abR[a], AR[a], &f->aiR[a], oR); }
  if (rt == RT_SPRING) { f->sp_oL += (oL - f->sp_oL) * c->sp_lp_a; f->sp_oR += (oR - f->sp_oR) * c->sp_lp_a; oL = f->sp_oL * 1.2f; oR = f->sp_oR * 1.2f; }
  else if (rt == RT_ROOM) { oL *= 1.3f; oR *= 1.3f; }
  *oL_ = tapL * 0.9f + oL; *oR_ = tapR * 0.9f + oR;
}

static fxset fxc;                                                             /* (this block's – fx_voice sets it, fx_out uses it) */
/* the voice with its MOD, delay and reverb – what the looper records (REC FX on) and what one hears of the voice */
void fx_voice(const float *vl, const float *vr, float *wl, float *wr, int n) {
  fxc = fx_settings();
  float mm = V.dry ? 0 : V.s.mod_mix, mrate = mod_hz(V.s.mod_rate), mdepth = V.s.mod_depth; int mt = (int)(V.s.mod_type + 0.5f);
  static float mm_s;
  for (int i = 0; i < n; i++) {
    mm_s += (mm - mm_s) * 0.002f;
    float xl = vl[i], xr = vr[i], oL, oR;
    if (mm_s > 0.0005f || mm > 0) mod_process(&xl, &xr, mm_s, mrate, mdepth, mt);
    fx_tick(&FV, &fxc, (xl + xr) * 0.5f, 0, 0, &oL, &oR);
    wl[i] = xl + oL; wr[i] = xr + oR;
  }
}
/* the out: the voice (wet), the loops, their sends through the loops' own delay + reverb, then the master */
void fx_out(const float *wl, const float *wr, const float *ll, const float *lr, const float *s1, const float *s2, float *l, float *r, int n) {
  float out_t = db2lin(V.out_db);
  static float out = 1;
  for (int i = 0; i < n; i++) {
    out += (out_t - out) * 0.002f;
    float oL = 0, oR = 0;
    fx_tick(&FL, &fxc, 0, s1[i], s2[i], &oL, &oR);                           /* (always: a path stopped would keep old echoes) */
    float L = (wl[i] + ll[i] + oL) * out, R = (wr[i] + lr[i] + oR) * out;
    /* master: limiter at -3 dB, then a soft clip – feedback or a shout never turns into hard digital distortion */
    float pk = vd_maxf(vd_absf(L), vd_absf(R)), tl = pk > 0.7f ? 0.7f / pk : 1;
    lim = tl < lim ? tl : lim + (1 - lim) * 0.0002f; if (lim > tl) lim = tl;
    l[i] = vd_tanh(L * lim); r[i] = vd_tanh(R * lim);
    float o = vd_maxf(vd_absf(l[i]), vd_absf(r[i]));
    V.out_peak = o > V.out_peak ? o : V.out_peak * meter_rel;
  }
}

void dsp_init(float rate) {
  sr = rate;
  meter_rel = vd_coef(0.3f, sr);                                               /* vd_coef = what stays per sample */
  env_att = 1 - vd_coef(0.003f, sr); env_rel = 1 - vd_coef(0.03f, sr);
  vd_svf_set(&ylpc, 900, 0.7f, sr);
  vd_svf_set(&sibc, 5500, 0.7f, sr);
  bn = 0;
}
