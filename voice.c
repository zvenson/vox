/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* voice.c – what makes the voice another voice.
   Pitch engine: TD-PSOLA. The tracker (dsp.c) gives the period P; every P samples an analysis mark is set on the input.
                 Each output voice takes the grain (Hann, 2P long) around the mark nearest to "now − latency" and lays it
                 down every P/ratio samples → new pitch, same formants. Reading the grain faster/slower (formant factor)
                 moves the formants: monster, chipmunk. Up to 4 voices (lead + 3 harmonies), ~11 ms + one period latency (~17 ms).
   Auto-tune:    per pitch estimate (~10.7 ms) the nearest allowed note (key + scale, or the held keys), the correction
                 glides there with SPEED (0 ms = hard steps, the T-Pain/robot sound), STRENGTH scales it.
   Harmonizer:   diatonic intervals in the key (third, fifth, triad, choir), an octave below, or the held keys.
   Colour:       radio, phone, megaphone (band-pass + drive), robot (ring modulator + metal comb), lo-fi (crush).
   Doubler:      two slowly wobbling short delays, hard left/right – sounds like two takes. */
#include "vox.h"

static float sr;
static inline float st2r(float st) { return vd_exp2(st * (1.0f / 12)); }

/* ---------------------------------------------------------------- scales and targets ----------------------------- */
static const uint16_t SCALE_BITS[SC_COUNT] = {                 /* bit n = n semitones above the key */
  0xFFF,                                                       /* chromatic */
  (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* major */
  (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* natural minor */
  (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* major pentatonic */
  (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* blues */
  0                                                            /* the held keys */
};
static int pc(int n) { return ((n % 12) + 12) % 12; }
static int rotate(int bits, int key) { int m = 0; for (int i = 0; i < 12; i++) if (bits & (1 << i)) m |= 1 << pc(i + key); return m; }

static int allowed_mask(int scale) {                           /* over absolute pitch classes */
  int key = (int)V.tune_key;
  if (scale == SC_KEYS) {
    int notes[MAX_VOICES], k = carrier_notes(notes, MAX_VOICES), m = 0;
    for (int i = 0; i < k; i++) m |= 1 << pc(notes[i]);
    return m ? m : 0xFFF;
  }
  return rotate(SCALE_BITS[scale], key);
}
static int nearest(float midi, int mask, int keep) {
  /* hysteresis: the note held a little past half-way – but with no glide (SPEED 0, the hard "Cher" tune) it jumps right at
   * half-way: the steps are the sound */
  float hy = tune_amount() > 0.99f || V.s.tune_speed < 0.05f ? 0.52f : 0.65f;
  if (keep > 0 && (mask & (1 << pc(keep))) && vd_absf(midi - keep) < hy) return keep;
  int best = (int)(midi + 0.5f); float bd = 99;
  for (int n = (int)midi - 7; n <= (int)midi + 7; n++) if (mask & (1 << pc(n))) { float d = vd_absf(n - midi); if (d < bd) { bd = d; best = n; } }
  return best;
}
static int step(int note, int k, int mask) {                   /* k scale steps up (k > 0) or down */
  while (k > 0) { do note++; while (!(mask & (1 << pc(note)))); k--; }
  while (k < 0) { do note--; while (!(mask & (1 << pc(note)))); k++; }
  return note;
}

/* ---------------------------------------------------------------- the pitch engine (PSOLA) ----------------------- */
#define PR 4096
#define PRM (PR - 1)
#define MK 64
static float pin[PR], olaL[PR], olaR[PR], hann[1025];
static long long w;                                            /* absolute sample count */
static long long mk_pos[MK]; static float mk_per[MK]; static uint8_t mk_voiced[MK]; static int mk_n;
static double mk_last;
typedef struct { double next; float st, ratio, gain, target, pan; } gvoice;
static gvoice gv[1 + MAX_HARM];
static float lat, use, use_k, agc = 1, agc_in, agc_out, agc_k;

int pitch_engine_on(void) {
  return tune_amount() > 0.001f || vd_absf(V.s.pitch_st) > 0.01f || vd_absf(V.s.formant_st) > 0.01f || (int)V.s.vox_mode == VOX_HARMONY;
}

/* once per pitch estimate: where the lead and the harmonies should sing */
void tune_hop(float midi) {
  static float corr; static int target;
  float hop = 2.0f * yin_step() / sr;             /* (dsp.c: an estimate every yin_step() samples at sr/2) */
  int mask = allowed_mask((int)V.tune_scale);
  if (midi > 0) {
    int t = nearest(midi, mask, target); target = t;
    /* STÄRKE is the master (Sven 24.10.): 100 % = the voice laid flat on the note at once – no glide, no vibrato left,
     * completely unnatural; less = less of the way and a softer glide (SPEED scaled by what is left of 100 %) */
    float amt = tune_amount();
    float want = (t - midi) * amt;
    float tau = 0.4f * V.s.tune_speed * V.s.tune_speed * (1 - amt);   /* 0 .. 400 ms, 0 at 100 % */
    float k = tau < 0.002f ? 1 : 1 - vd_exp2(-hop / tau * 1.442695f);
    corr += (want - corr) * k;
  } else { corr = 0; target = 0; }
  gv[0].st = corr + V.s.pitch_st; gv[0].target = 1; gv[0].pan = 0;
  V.pitch_out = midi > 0 ? midi + gv[0].st : 0;
  V.pitch_out_hist[V.pitch_pos] = V.pitch_out;
  V.tune_target = midi > 0 && tune_amount() > 0.001f ? target : 0;

  /* harmonies */
  int hn[MAX_HARM], n = 0;
  if ((int)V.s.vox_mode == VOX_HARMONY && midi > 0) {
    int hmask = (int)V.tune_scale == SC_CHROM || (int)V.tune_scale == SC_KEYS ? rotate(SCALE_BITS[SC_MAJOR], (int)V.tune_key) : mask;
    int base = tune_amount() > 0.001f ? target : nearest(midi, hmask, 0);
    if (!(hmask & (1 << pc(base)))) base = nearest(base, hmask, 0);
    switch ((int)V.s.harm) {
      case HM_THIRD: hn[n++] = step(base, 2, hmask); break;
      case HM_FIFTH: hn[n++] = step(base, 4, hmask); break;
      case HM_TRIAD: hn[n++] = step(base, 2, hmask); hn[n++] = step(base, 4, hmask); break;
      case HM_OCT_DOWN: hn[n++] = base - 12; break;
      case HM_CHOIR: hn[n++] = step(base, 2, hmask); hn[n++] = step(base, 4, hmask); hn[n++] = base - 12; break;
      case HM_KEYS: { int notes[MAX_VOICES], k = carrier_notes(notes, MAX_VOICES); for (int i = 0; i < k && n < MAX_HARM; i++) hn[n++] = notes[i]; break; }
    }
  }
  static const float PAN[3][3] = { { -0.5f }, { -1, 1 }, { -1, 1, 0 } };
  for (int j = 0; j < MAX_HARM; j++) {
    gvoice *g = &gv[1 + j];
    if (j < n) {
      float st = hn[j] - midi + V.s.pitch_st;
      g->st = g->target > 0 ? g->st + (st - g->st) * 0.35f : st;  /* ~25 ms glide between notes */
      g->target = V.s.harm_mix; g->pan = PAN[n - 1][j] * V.s.harm_width;
    } else g->target = 0;
  }
  for (int j = 0; j < n; j++) V.harm_notes[j] = hn[j];
  V.harm_n = n;
  for (int v = 0; v <= MAX_HARM; v++) gv[v].ratio = st2r(gv[v].st);
}

static void grain(gvoice *g, float f) {
  double target = g->next - lat;
  int best = -1; double bd = 1e18;
  for (int i = 0; i < MK && i < mk_n; i++) {                   /* newest first */
    int idx = (mk_n - 1 - i) & (MK - 1);
    long long p = mk_pos[idx]; float per = mk_per[idx];
    if (p + per + 1 >= w) continue;                            /* the grain must be complete in the input */
    if (p - per < w - PR + 4) break;                           /* already overwritten */
    double d = p > target ? p - target : target - p;
    if (d < bd) { bd = d; best = idx; } else if (p < target) break;
  }
  if (best < 0) { g->next += 64; return; }
  float P = mk_per[best], ratio = mk_voiced[best] ? g->ratio : 1;
  if (!mk_voiced[best] && g != &gv[0]) g->target = 0;        /* harmonies rest on consonants */
  g->gain += (g->target - g->gain) * 0.25f;
  float Ps = P / ratio;
  int G = (int)(2 * P / f); if (G > PR - 512) G = PR - 512; if (G < 16) G = 16;
  float gain = g->gain * vd_clamp(f / ratio, 0, 2);
  float pl = vd_minf(1, 1 - g->pan) * gain, pr = vd_minf(1, 1 + g->pan) * gain;
  long long base = (long long)g->next;
  double in0 = (double)mk_pos[best] - P;
  float hs = 1024.0f / G;
  for (int k = 0; k < G; k++) {
    double ip = in0 + k * f; long long i0 = (long long)ip; float fr = (float)(ip - i0);
    float a = pin[i0 & PRM], s = a + (pin[(i0 + 1) & PRM] - a) * fr;
    float hp = k * hs; int hi = (int)hp; float wv = hann[hi] + (hann[hi + 1] - hann[hi]) * (hp - hi);
    s *= wv;
    int o = (int)((base + k) & PRM);
    olaL[o] += s * pl; olaR[o] += s * pr;
  }
  g->next += Ps;
}

void pitch_engine(float x, float *l, float *r) {
  pin[w & PRM] = x;
  /* analysis marks, one per period (unvoiced: every 5.3 ms) */
  int voiced = V.pitch_now > 0;
  float P = voiced ? vd_clamp(sr / vd_mtof(V.pitch_now), sr / 1200, sr / 65) : sr * 0.0053f;
  if (mk_n == 0 || w - mk_last > 3 * P) mk_last = (double)w - P;
  if (w >= mk_last + P) {
    mk_last += P;
    int idx = mk_n & (MK - 1); mk_pos[idx] = (long long)(mk_last + 0.5); mk_per[idx] = P; mk_voiced[idx] = (uint8_t)voiced; mk_n++;
  }
  int on = pitch_engine_on() && !V.dry;
  use += ((float)on - use) * use_k;
  float oL = 0, oR = 0;
  if (use > 0.0005f) {
    float f = st2r(V.s.formant_st);
    for (int v = 0; v <= MAX_HARM; v++) {
      gvoice *g = &gv[v];
      if (v > 0 && g->target <= 0 && g->gain < 0.002f) { g->gain = 0; g->next = (double)w; continue; }
      if (g->next < w - 2048) g->next = (double)w;           /* coming back after a pause */
      while (w >= g->next) grain(g, f);
    }
    int o = (int)(w & PRM);
    oL = olaL[o]; oR = olaR[o]; olaL[o] = olaR[o] = 0;
    /* automatic level: shifted voices lose energy (the harmonics move away from the formants) – bring it back,
       slowly and only while someone sings. 0.8 .. 4 (= -2 .. +12 dB). */
    float m = (oL + oR) * 0.5f;
    agc_in += (x * x - agc_in) * agc_k; agc_out += (m * m - agc_out) * agc_k;
    if (agc_in > 1e-5f && agc_out > 1e-9f) { float want = vd_clamp(vd_sqrt(agc_in / agc_out), 0.8f, 4); agc += (want - agc) * agc_k * 4; }
    oL *= agc; oR *= agc;
  } else for (int v = 0; v <= MAX_HARM; v++) gv[v].next = (double)w;
  *l = x + (oL - x) * use; *r = x + (oR - x) * use;
  w++;
}

/* ---------------------------------------------------------------- colour ----------------------------------------- */
typedef struct { vd_svf hp, lp, bp; float held, comb[512]; int cnt, ci; } colour_ch;
static colour_ch cc[2];
static vd_svf_c c_hp, c_lp, c_bp;
static float ring_ph;
static int c_last = -1;

static void colour_setup(int c) {
  switch (c) {
    case CO_RADIO: vd_svf_set(&c_hp, 320, 0.7f, sr); vd_svf_set(&c_lp, 3400, 0.9f, sr); break;
    case CO_PHONE: vd_svf_set(&c_hp, 650, 1.0f, sr); vd_svf_set(&c_lp, 2700, 1.3f, sr); break;
    case CO_MEGA: vd_svf_set(&c_hp, 600, 0.7f, sr); vd_svf_set(&c_bp, 1700, 1.6f, sr); vd_svf_set(&c_lp, 4200, 0.7f, sr); break;
    case CO_ROBOT: vd_svf_set(&c_hp, 120, 0.7f, sr); break;
    case CO_LOFI: vd_svf_set(&c_lp, 3600, 0.7f, sr); break;
  }
  c_last = c;
}

void colour(float *l, float *r) {
  int c = (int)V.s.colour; if (c == CO_OFF) return;
  if (c != c_last) colour_setup(c);
  float ring = 0;
  if (c == CO_ROBOT) { ring_ph += 64 / sr; if (ring_ph >= 1) ring_ph -= 1; ring = vd_sin(ring_ph); }
  float *io[2] = { l, r };
  for (int ch = 0; ch < 2; ch++) {
    colour_ch *s = &cc[ch]; float x = *io[ch], y = x;
    switch (c) {
      case CO_RADIO: y = vd_svf_tick(&s->lp, &c_lp, vd_svf_tick(&s->hp, &c_hp, x).hp).lp; y = vd_tanh(y * 2.4f) * 0.95f; break;
      case CO_PHONE: y = vd_svf_tick(&s->lp, &c_lp, vd_svf_tick(&s->hp, &c_hp, x).hp).lp; y = vd_tanh(y * 6) * 0.95f; break;
      case CO_MEGA: y = vd_svf_tick(&s->bp, &c_bp, vd_svf_tick(&s->hp, &c_hp, x).hp).bp * 2;
                    y = vd_svf_tick(&s->lp, &c_lp, vd_tanh(y * 16) * 0.42f).lp; break;
      case CO_ROBOT: {                                          /* ring modulator + 4 ms metal comb */
        y = vd_svf_tick(&s->hp, &c_hp, x).hp * (0.3f + 0.7f * ring) * 1.3f;
        int d = (int)(0.004f * sr); if (d > 511) d = 511;
        float fbk = s->comb[(s->ci - d) & 511];
        s->comb[s->ci] = y + fbk * 0.55f; s->ci = (s->ci + 1) & 511;
        y = (y + fbk) * 0.95f; break; }
      case CO_LOFI: {                                           /* ~8 kHz, 5 bit */
        if (--s->cnt <= 0) { s->cnt = (int)(sr / 8000); float q = vd_tanh(x * 1.5f) * 16; s->held = (float)(int)(q + (q < 0 ? -0.5f : 0.5f)) / 16; }
        y = vd_svf_tick(&s->lp, &c_lp, s->held).lp * 0.8f; break; }
    }
    *io[ch] = y;
  }
}

/* ---------------------------------------------------------------- doubler ---------------------------------------- */
#define DB 2048
static float dbuf[DB], dph1, dph2;
static int dbw;

void doubler(float *l, float *r) {
  float amt = V.s.doubler;
  float m = (*l + *r) * 0.5f;
  dbuf[dbw] = m;
  if (amt > 0.001f) {
    dph1 += 0.31f / sr; if (dph1 >= 1) dph1 -= 1;
    dph2 += 0.23f / sr; if (dph2 >= 1) dph2 -= 1;
    float d1 = sr * (0.012f + 0.0016f * vd_sin(dph1)), d2 = sr * (0.019f + 0.0021f * vd_sin(dph2));
    float t[2], d[2] = { d1, d2 };
    for (int k = 0; k < 2; k++) {
      float rp = dbw - d[k]; while (rp < 0) rp += DB;
      int i0 = (int)rp; float fr = rp - i0;
      t[k] = dbuf[i0] + (dbuf[(i0 + 1) & (DB - 1)] - dbuf[i0]) * fr;
    }
    *l += t[0] * amt * 0.8f; *r += t[1] * amt * 0.8f;
  }
  dbw = (dbw + 1) & (DB - 1);
}

void voice_init(float rate) {
  sr = rate;
  lat = 0.011f * sr;
  use_k = 1 - vd_coef(0.01f, sr);
  agc_k = 1 - vd_coef(0.3f, sr);
  for (int i = 0; i <= 1024; i++) hann[i] = 0.5f - 0.5f * vd_sin(i / 1024.0f + 0.25f);
  for (int v = 0; v <= MAX_HARM; v++) gv[v].ratio = 1;
  gv[0].gain = gv[0].target = 1;
}
