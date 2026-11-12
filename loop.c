/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* loop.c – the VOX looper: FOUR loop tracks side by side, the way a loop station works (Sven 30.10.2026: "wie eine
   Boss RC-505, halt mit 4 wegen der Tasten aufm MPK"). Each track records after the voice effects and before delay/hall.
   On the LOOP page the pads are the looper (no programs, no punch-in there):
     pad 1-4 (the MPK's upper row) and encoder push 1-4 (the VIBE Box): the track's ONE switch, as a Ditto (1.5) –
              tap: empty → REC · REC → PLAY · PLAY → OVERDUB · OVERDUB → PLAY · stopped → PLAY · tap twice: STOP ·
              hold 1 s: UNDO / REDO (playing) or deleted (stopped) · tap twice and hold: STOP and deleted
     pad 5-8 (the lower row): STOP the track AT THE LOOP'S END (again = at once) · held 1 s: deleted
   The first loop sets the length (LÄNGE FREI: as long as you record · 1/2/4/8 bars: it closes by itself). Every later
   loop starts at the loop's head (REC blinks – WAIT – until it comes) and ends on a multiple of it: all tracks stay in
   step on one clock (G); a track stopped and started again comes back in phase. One track records or overdubs at a
   time (one voice). PLAY / REC / STOP / the pedal / SHIFT+F6 work on the chosen track (the last pad pressed) as before:
   REC and the pedal are the Ditto's one switch, STOP twice = that track deleted, SHIFT + STOP = all stop.
   1.5 (11.11.): every track's switch is a whole Ditto (pad 1-4, encoder push 1-4 – a device's X1-X4, the VIBE Box).
   Memory: 24 s at 48 kHz per track, 16 bit, plus the same again for undo (kept sample by sample while an overdub runs).
   Out (30.10.): each track fades in and out over 5 ms (stop, play, clear: no click), its level glides; it comes out dry
   and sends post fader into the delay (SEND 1) and the hall (SEND 2) – the voice's own delay / hall amounts stay the voice's. */
#include "vox.h"

#define LMAX (24 * 48000)
typedef struct {
  int16_t lb[LMAX * 2], ub[LMAX * 2];
  int len, n, rec_len, pend;                  /* n: frames recorded so far · pend: the recording ends there (a multiple of the master) */
  int od_start, od_count;
  int pend_dub, tp_n, tp_start;                         /* 1.5 (Ditto): an overdub begun by a press – its first seconds kept apart (TP) until the release */
  uint64_t off;                               /* where on G the loop's head lies */
  float amp, gain;                            /* the fade (0..1) and the gliding level */
  int zap;                                    /* cleared while it still sounded: the audio fades it out, then it is gone */
} trk_t;
static trk_t T[NLT];
static float sr;
static int maxlen;
static uint64_t G, m_off;                     /* the clock, the master loop's head on it */
static int ml;                                /* the master length (frames), 0 = no loop yet */
static int rec_t = -1, dub_t = -1, arm;       /* the track recording / overdubbing · arm: it waits for the head */
static double stop_ms[NLT];
#define TMAX (3 * 48000)                      /* the press's overdub, kept apart: 3 s at 48 kHz (1.5 s at 96 kHz) – the hold is 1.5 s */
static int16_t TP[TMAX * 2];                  /* (one overdub at a time: one buffer) */
static void end_dub(int i);

static inline int16_t s16(float x) { float v = x * 16384; return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
static inline float f32(int16_t v) { return v * (1.0f / 16384); }
static lt_state *S(int i) { return &V.lt[i]; }
static int st(int i) { return V.lt[i].state; }
static int running(int i) { int s = st(i); return s == LP_REC || s == LP_PLAY || s == LP_DUB; }
static int any_running(void) { for (int i = 0; i < NLT; i++) if (running(i)) return 1; return 0; }
static int any_loop(void) { for (int i = 0; i < NLT; i++) if (T[i].len && !T[i].zap) return 1; return 0; }
static void mirror(void) {                    /* the chosen track's state where the header and the lights look */
  lt_state *s = S(V.loop_sel); V.loop_state = s->state; V.loop_layers = s->layers; V.loop_undo = s->undo; V.loop_pos = s->pos; V.loop_len = s->len;
}
static int unit(void) {                       /* what a loop's length snaps to: the master, else the bars at the tempo */
  if (ml) return ml;
  int bars = (int)V.loop_bars; return bars ? (int)(60.0f / V.bpm * 4 * (1 << (bars - 1)) * sr) : 0;
}

/* The heavy work over a whole track (up to 24 s = 1.15 M frames) never in one block (VOX 1.5, 11.11.: on the VIBE one
 * block took 11-12 ms of 2.67 – it crackled): the overview and the undo's swap are done a slice per block (loop_slices). */
#define OV_SLICE 16384                         /* frames of the overview per block (~0.05 ms on the VIBE) */
#define UN_SLICE 32768                         /* frames of the undo's swap per block – far ahead of the playhead (128) */
static int ov_k[NLT] = { -1, -1, -1, -1 };     /* the overview's progress, -1 = nothing to do */
static int un_i = -1, un_k, un_p0;             /* the undo being swapped: track, progress, where it began (the playhead) */
static void overview(int i) {
  lt_state *s = S(i);
  for (int c = 0; c < LT_COLS; c++) s->over[c] = 0;
  ov_k[i] = T[i].len > 0 ? 0 : -1;
}
static void ov_slice(int i) {
  lt_state *s = S(i); trk_t *t = &T[i];
  if (ov_k[i] < 0 || t->len <= 0) { ov_k[i] = -1; return; }
  int k = ov_k[i], e = k + OV_SLICE; if (e > t->len) e = t->len;
  for (; k < e; k++) {
    float a = vd_maxf(vd_absf(f32(t->lb[2 * k])), vd_absf(f32(t->lb[2 * k + 1])));
    int c = (int)((long long)k * LT_COLS / t->len);
    if (a > s->over[c]) s->over[c] = a;
  }
  ov_k[i] = k >= t->len ? -1 : k;
}
static void un_slice(int all) {               /* swap the overdub's region back, from the playhead on, a slice (all: the rest) */
  if (un_i < 0) return;
  trk_t *t = &T[un_i]; int len = t->len, n = all ? len : UN_SLICE;
  if (len <= 0) { un_i = -1; return; }
  for (int j = 0; j < n && un_k < len; j++, un_k++) {
    int p = un_p0 + un_k; if (p >= len) p -= len;
    int d = p - t->od_start; if (d < 0) d += len;
    if (d >= t->od_count) continue;
    int16_t a = t->lb[2 * p], c = t->lb[2 * p + 1]; t->lb[2 * p] = t->ub[2 * p]; t->lb[2 * p + 1] = t->ub[2 * p + 1]; t->ub[2 * p] = a; t->ub[2 * p + 1] = c;
  }
  if (un_k >= len) { int i = un_i; un_i = -1; overview(i); }
}
static void loop_slices(void) { un_slice(0); for (int i = 0; i < NLT; i++) if (ov_k[i] >= 0) ov_slice(i); }

static void clear(int i) {
  if (un_i == i) un_i = -1;
  ov_k[i] = -1;
  if (rec_t == i) { rec_t = -1; arm = 0; }
  if (dub_t == i) dub_t = -1;
  lt_state *s = S(i); s->state = LP_EMPTY; s->layers = 0; s->undo = 0; s->pos = s->len = 0; s->stop_at_end = 0;
  if (T[i].len && T[i].amp > 0.0001f) T[i].zap = 1;   /* (still sounding: the audio fades it out first) */
  else { T[i].len = 0; T[i].zap = 0; T[i].amp = 0; }
  T[i].n = T[i].pend = 0; T[i].pend_dub = 0; T[i].tp_n = 0;
  for (int c = 0; c < LT_COLS; c++) s->over[c] = 0;
  if (!any_loop()) ml = 0;
}

static void start_rec(int i) {
  if (rec_t >= 0) { toast("A TRACK IS ALREADY RECORDING"); return; }
  if (dub_t >= 0) { int o = dub_t; end_dub(o); S(o)->state = LP_PLAY; }
  trk_t *t = &T[i];
  if (un_i == i) un_i = -1;
  ov_k[i] = -1;
  t->n = 0; t->pend = 0; t->len = 0; t->zap = 0; t->amp = 0;
  int bars = (int)V.loop_bars;
  t->rec_len = ml ? maxlen : bars ? (int)(60.0f / V.bpm * 4 * (1 << (bars - 1)) * sr) : maxlen;   /* (a later loop ends by its multiple) */
  if (t->rec_len > maxlen) { t->rec_len = maxlen; toast("TOO LONG – CUT TO 24 S"); }
  int others = any_running();                 /* (before this one counts) */
  lt_state *s = S(i); s->layers = 0; s->undo = 0; s->state = LP_REC;
  for (int c = 0; c < LT_COLS; c++) s->over[c] = 0;
  rec_t = i;
  if (ml && others) { arm = 1; toast("RECORDING FROM THE LOOP START"); }   /* (the others run: from the next head) */
  else { arm = 0; if (ml) G += (uint64_t)((ml - (int)((G - m_off) % (uint64_t)ml)) % ml); t->off = G; toast("RECORDING"); }
}

/* the recording becomes the loop: 1 = it is one now · 0 = not yet (it goes on to the master's multiple) or too short */
static int close_rec(int i, int from_audio) {
  trk_t *t = &T[i]; int n = t->n;
  if (arm) { clear(i); toast("RECORDING CANCELLED"); return 0; }
  int u = unit();
  if (u && !from_audio) {
    int k = (n + u / 2) / u; if (k < 1) k = 1;
    if (k * u > n) { if (k * u > t->rec_len) k = t->rec_len / u > 0 ? t->rec_len / u : 1; if (k * u > n) { t->pend = k * u; toast("... TO THE LOOP END"); return 0; } }
    n = k * u;
  }
  if (n < (int)(0.25f * sr)) { clear(i); toast("TOO SHORT – AGAIN"); return 0; }
  t->len = n; t->pend = 0; rec_t = -1;
  int f = (int)(0.004f * sr); if (f > n / 4) f = n / 4;   /* 4 ms fades at the seam: no click */
  for (int k = 0; k < f; k++) {
    float g = (float)k / f;
    for (int c = 0; c < 2; c++) { t->lb[2 * k + c] = (int16_t)(t->lb[2 * k + c] * g); t->lb[2 * (n - 1 - k) + c] = (int16_t)(t->lb[2 * (n - 1 - k) + c] * g); }
  }
  S(i)->layers = 1; S(i)->state = LP_PLAY; overview(i);
  if (!ml) { ml = n; m_off = t->off; }
  return 1;
}

static void stop_cancel(int i) { S(i)->stop_at_end = 0; }
static int pos_of(int i) { return T[i].len ? (int)((G - T[i].off) % (uint64_t)T[i].len) : 0; }
static void start_dub(int i) {
  if (rec_t >= 0) { toast("A TRACK IS ALREADY RECORDING"); return; }
  if (dub_t >= 0 && dub_t != i) { int o = dub_t; end_dub(o); S(o)->state = LP_PLAY; }
  if (un_i >= 0) un_slice(1);                 /* (an undo still being swapped: finished first – rare) */
  T[i].od_start = pos_of(i); T[i].od_count = 0; T[i].pend_dub = 0; T[i].tp_n = 0; S(i)->undo = 0; S(i)->state = LP_DUB; dub_t = i;
  /* (the last layer's undo stays possible until this overdub is really one: a pressed one is kept apart, dub_commit) */
}
/* the press's overdub (Ditto): until the release its first samples' old values sit in TP, so a hold (undo) or a second
 * tap (stop) can take it back without a trace and the last layer's undo is still there; the release makes it a layer */
static void dub_commit(int i) {
  trk_t *t = &T[i]; if (!t->pend_dub) return;
  int p = t->tp_start;
  for (int k = 0; k < t->tp_n; k++) { t->ub[2 * p] = TP[2 * k]; t->ub[2 * p + 1] = TP[2 * k + 1]; if (++p >= t->len) p = 0; }
  t->od_start = t->tp_start; t->od_count = t->tp_n; t->pend_dub = 0; S(i)->undo = 0;
}
static void dub_cancel(int i) {                 /* the press's overdub undone, nothing kept: the track plays as before */
  trk_t *t = &T[i]; if (!t->pend_dub) return;
  int p = t->tp_start;
  for (int k = 0; k < t->tp_n; k++) { t->lb[2 * p] = TP[2 * k]; t->lb[2 * p + 1] = TP[2 * k + 1]; if (++p >= t->len) p = 0; }
  t->pend_dub = 0; t->tp_n = 0;                  /* (od_start / od_count: still the last layer's – its undo stays right) */
  if (dub_t == i) dub_t = -1;
  S(i)->state = LP_PLAY; overview(i);
}
static void end_dub(int i) { dub_commit(i); if (dub_t == i) dub_t = -1; if (T[i].od_count > 0) { S(i)->layers++; S(i)->undo = 1; } }
static void play(int i) {                     /* a stopped track plays again – in step; nothing running: from the master's head */
  if (!T[i].len) return;
  stop_cancel(i);
  if (!any_running() && ml) G += (uint64_t)((ml - (int)((G - m_off) % (uint64_t)ml)) % ml);
  S(i)->state = LP_PLAY;
}
/* STOP: a playing track plays to its loop's end and stands there (the audio fades its last 5 ms); now = at once (5 ms) */
static void stop(int i, int quiet, int now) {
  int s = st(i);
  if (s == LP_REC) { if (!close_rec(i, 0) && st(i) == LP_REC) { arm = 0; T[i].pend = 0; if (!close_rec(i, 1)) return; } S(i)->state = LP_STOP; stop_cancel(i); }
  else if (s == LP_DUB || s == LP_PLAY) {
    if (s == LP_DUB) { end_dub(i); S(i)->state = LP_PLAY; }
    if (now || S(i)->stop_at_end) { S(i)->state = LP_STOP; stop_cancel(i); if (!quiet) toast("STOP"); return; }
    S(i)->stop_at_end = 1; if (!quiet) toast("STOP AT THE LOOP END – AGAIN = NOW"); return;
  }
  else return;
  if (!quiet) toast("STOP – 2× STOP / HOLD = CLEAR");
}
static void undo(int i) {
  if (st(i) == LP_DUB) { end_dub(i); S(i)->state = LP_PLAY; }
  if (st(i) == LP_REC || st(i) == LP_EMPTY || !S(i)->undo) { toast("NOTHING TO UNDO"); return; }
  if (un_i >= 0) un_slice(1);                 /* (the one before still swapping – undo, redo quickly: finished first) */
  trk_t *t = &T[i];                           /* swap = undo, swap again = redo – a slice per block, from the playhead on */
  un_i = i; un_k = 0; un_p0 = t->len ? (int)((G - t->off) % (uint64_t)t->len) : 0;
  un_slice(0);                                /* (the first slice at once: what plays next is already the new layer) */
  if (S(i)->undo == 1) { S(i)->undo = 2; S(i)->layers--; toast("LAYER UNDONE (AGAIN = REDO)"); }
  else { S(i)->undo = 1; S(i)->layers++; toast("LAYER BACK"); }
}
static void tap(int i) {                      /* the track's button */
  switch (st(i)) {
    case LP_EMPTY: start_rec(i); break;
    case LP_REC: if (rec_t == i) { if (arm) { clear(i); toast("RECORDING CANCELLED"); } else if (close_rec(i, 0)) toast("LOOP PLAYS – AGAIN = OVERDUB"); } break;
    case LP_PLAY: if (S(i)->stop_at_end) { stop_cancel(i); toast("PLAYS ON"); break; }
      start_dub(i); if (st(i) == LP_DUB) toast("OVERDUB – HOLD = UNDO"); break;
    case LP_DUB: end_dub(i); S(i)->state = LP_PLAY; toast("LOOP PLAYS"); break;
    case LP_STOP: play(i); toast("LOOP PLAYS – IN TIME"); break;
  }
}

/* ---- the buttons: PLAY / REC / STOP / undo (SHIFT+F6) on the chosen track, as before ---------------------------- */
void loop_button(int b) {
  int i = V.loop_sel, s = st(i);
  if (b == VB_STOP) {
    double now = v_ms();
    if (now - stop_ms[i] < 450 && s != LP_EMPTY) { clear(i); toast("LOOP CLEARED"); stop_ms[i] = 0; mirror(); return; }
    stop_ms[i] = now;
    stop(i, 0, 0); mirror(); return;
  }
  if (b == VB_K3) { undo(i); mirror(); return; }
  if (b == VB_PLAY) tap(i);
  else if (b == VB_REC) {
    switch (s) {
      case LP_EMPTY: start_rec(i); break;
      case LP_REC: if (rec_t == i && !arm && close_rec(i, 0)) start_dub(i); break;
      case LP_PLAY: start_dub(i); break;
      case LP_DUB: end_dub(i); S(i)->state = LP_PLAY; break;
      case LP_STOP: play(i); start_dub(i); break;
    }
  }
  mirror();
}
void loop_stop_all(void) {                     /* SHIFT + STOP: every track at its loop's end · again while they wait: at once */
  int waiting = 0; for (int i = 0; i < NLT; i++) waiting |= S(i)->stop_at_end;
  for (int i = 0; i < NLT; i++) stop(i, 1, waiting);
  toast(waiting ? "ALL TRACKS STOP" : "ALL STOP AT THE LOOP END"); mirror();
}
void loop_clear_all(void) { int had = any_loop() || rec_t >= 0; for (int i = 0; i < NLT; i++) clear(i); if (had) toast("ALL LOOPS CLEARED"); mirror(); }

/* ---- the pads on the LOOP page: 0-3 the tracks' buttons, 4-7 their STOP · held 1 s: UNDO / the track deleted ----- */
static int pd_down[2 * NLT], pd_done[2 * NLT]; static double pd_t[2 * NLT];
void loop_select(int i) {
  if (i < 0 || i >= NLT) return;
  V.loop_sel = i; mirror();
  char t[24] = "TRACK "; char n[4]; int k = 5; for (const char *c = vg_itoa(i + 1, n); *c; c++) t[k++] = *c; t[k] = 0;
  const char *g = " SELECTED"; while (*g && k < 23) t[k++] = *g++; t[k] = 0; toast(t);
}
void loop_pad(int p, int down) {
  if (p < 0 || p >= 2 * NLT) { if (down) toast("PAD 1-4 = TRACK · 5-8 = STOP"); return; }
  int i = p % NLT;
  if (p < NLT) { loop_track_switch(i, down); return; }     /* pad 1-4: the track's one switch (Ditto) */
  if (!down) { pd_down[p] = 0; return; }
  pd_down[p] = 1; pd_done[p] = 0; pd_t[p] = v_ms();
  V.loop_sel = i;
  stop(i, 0, 0);                                           /* pad 5-8: STOP at the loop's end (again = now) · held: deleted */
  mirror();
}

/* ---- ONE switch per track, as the TC Electronic Ditto Looper (Sven 24.10. for REC / the pedal; 11.11.: "je Spur 1 Button –
 * der kann alles, genau so"): pad 1-4 and encoder push 1-4 are track 1-4's switch, REC and the pedal the chosen track's
 *   tap: empty → REC · REC → PLAY · PLAY → OVERDUB · OVERDUB → PLAY · stopped → PLAY
 *   tap twice: STOP (at once, as the Ditto) · hold (1 s) while it plays: the last layer undone, hold again: back
 *   hold while stopped: the track deleted · tap twice and hold the second: STOP and deleted
 * What must be on the beat happens at the press; what a hold could mean instead waits for the release. The hold is seen
 * in the audio (loop_tick), so it also works in the plugin with its window closed. Pressing a track's switch also
 * chooses it (REC / PLAY / STOP / the pedal / SAVE WAV act on it). */
#define SW_HOLD_MS 1500.0                       /* (the Ditto: "mindestens eineinhalb Sekunden") */
#define SW_DOUBLE_MS 400.0
static int sw_down[NLT], sw_double[NLT], sw_held[NLT], sw_stopped[NLT], sw_rec = -1;   /* (sw_stopped: it stood when pressed – a hold deletes, as the Ditto) */
static double sw_t_down[NLT], sw_t_prev[NLT];

static void sw_stop(int i) {
  int s = st(i);
  if (s == LP_DUB) { if (T[i].pend_dub) dub_cancel(i); else { end_dub(i); S(i)->state = LP_PLAY; } }   /* (the first tap's overdub: gone) */
  else if (s == LP_REC) { if (!close_rec(i, 0) && st(i) == LP_REC) { arm = 0; T[i].pend = 0; if (!close_rec(i, 1)) return; } }
  if (st(i) == LP_PLAY || st(i) == LP_DUB) { stop(i, 1, 1); toast("STOP – HOLD = CLEAR"); }
  else if (st(i) != LP_EMPTY) { S(i)->state = LP_STOP; stop_cancel(i); toast("STOP – HOLD = CLEAR"); }
}
void loop_track_switch(int i, int down) {     /* everything at the press, as the Ditto; the release only makes a pressed overdub a layer */
  if (i < 0 || i >= NLT) return;
  double now = v_ms();
  if (down) {
    V.loop_sel = i;
    sw_down[i] = 1; sw_held[i] = 0; sw_t_down[i] = now;
    sw_double[i] = now - sw_t_prev[i] < SW_DOUBLE_MS;
    sw_t_prev[i] = sw_double[i] ? -1e9 : now;            /* (a third tap is a new first one) */
    sw_stopped[i] = st(i) == LP_STOP;
    if (sw_double[i]) { if (st(i) != LP_EMPTY) sw_stop(i); mirror(); return; }
    switch (st(i)) {
      case LP_EMPTY: start_rec(i); break;
      case LP_REC: if (rec_t == i && arm) { clear(i); toast("RECORDING CANCELLED"); }
                   else if (rec_t == i && close_rec(i, 0)) toast("LOOP PLAYS – PRESS = OVERDUB"); break;
      case LP_PLAY: if (S(i)->stop_at_end) { stop_cancel(i); toast("PLAYS ON"); break; }
                  { int had = S(i)->undo, os = T[i].od_start, oc = T[i].od_count; start_dub(i);
                    if (st(i) == LP_DUB) { T[i].pend_dub = 1; T[i].tp_n = 0; T[i].tp_start = T[i].od_start; T[i].od_start = os; T[i].od_count = oc; S(i)->undo = had; toast("OVERDUB – PRESS = PLAY"); } } break;   /* (the last layer's undo kept until the release) */
      case LP_DUB: end_dub(i); S(i)->state = LP_PLAY; toast("LOOP PLAYS"); break;
      case LP_STOP: play(i); toast("LOOP PLAYS – IN TIME"); break;
    }
    mirror(); return;
  }
  if (!sw_down[i]) return;
  sw_down[i] = 0;
  if (!sw_held[i] && T[i].pend_dub) dub_commit(i);         /* (released: the overdub is one – the old layer's undo makes room) */
  mirror();
}
void loop_switch(int down) {                                   /* REC and the pedal: the chosen track's switch */
  if (down) sw_rec = V.loop_sel;
  if (sw_rec >= 0) loop_track_switch(sw_rec, down);
  if (!down) sw_rec = -1;
}
void loop_enc(int i, int down) { loop_track_switch(i, down); }   /* encoder push 1-4 (the VIBE Box): the same switch */
void loop_tick(void) {                                         /* (the audio) the held switches and the held STOP pads */
  loop_slices();                                               /* (the overview, an undo: a slice per block) */
  double now = v_ms();
  for (int p = NLT; p < 2 * NLT; p++) if (pd_down[p] && !pd_done[p] && now - pd_t[p] >= SW_HOLD_MS) {
    pd_done[p] = 1; int i = p % NLT;
    if (st(i) != LP_EMPTY) { clear(i); toast("TRACK CLEARED"); }
    mirror();
  }
  for (int i = 0; i < NLT; i++) {
    if (!sw_down[i] || sw_held[i] || now - sw_t_down[i] < SW_HOLD_MS) continue;
    sw_held[i] = 1;
    if (st(i) == LP_DUB && T[i].pend_dub) dub_cancel(i);     /* (the press began an overdub: taken back, then the undo) */
    if (sw_double[i] || sw_stopped[i] || st(i) == LP_STOP) { if (st(i) != LP_EMPTY) { clear(i); toast("TRACK CLEARED"); } }
    else if (st(i) == LP_PLAY) undo(i);
    mirror();
  }
}

/* ---- the audio: the voice into the recording / the overdub, every track out (dry) and into its two sends ---------- */
void loop_process(const float *vl, const float *vr, float *ll, float *lr, float *s1, float *s2, int n) {
  float master = V.loop_level, keep = V.loop_keep, fstep = 1.0f / (0.005f * sr), gglide = 1.0f / (0.02f * sr);
  float pk[NLT] = { 0, 0, 0, 0 };
  int go = any_running(); for (int i = 0; i < NLT; i++) if (T[i].amp > 0) go = 1;   /* (the clock runs on while one fades out) */
  for (int k = 0; k < n; k++) {
    float in_l = vl[k], in_r = vr[k], ol = 0, or_ = 0, d1 = 0, d2 = 0;
    if (rec_t >= 0 && arm && ml && (G - m_off) % (uint64_t)ml == 0) { arm = 0; T[rec_t].off = G; toast("RECORDING"); }
    for (int i = 0; i < NLT; i++) {
      trk_t *t = &T[i]; int s = st(i);
      if (s == LP_REC && !t->zap) {
        if (rec_t != i || arm) continue;
        if (t->n < maxlen) { t->lb[2 * t->n] = s16(in_l); t->lb[2 * t->n + 1] = s16(in_r); t->n++; }
        if ((t->pend && t->n >= t->pend) || t->n >= t->rec_len) {   /* the multiple, the bars or the memory reached: it plays */
          if (close_rec(i, 1)) { if (!t->pend && !ml) toast("MEMORY FULL – LOOP PLAYS"); else toast("LOOP PLAYS"); }
          mirror();
        }
        continue;
      }
      float want = !t->zap && (s == LP_PLAY || s == LP_DUB) ? 1.0f : 0.0f;   /* 5 ms in, 5 ms out: no click */
      int at = t->len ? (int)((G - t->off) % (uint64_t)t->len) : 0;
      if (S(i)->stop_at_end && s == LP_PLAY && t->len) {   /* STOP at the end: fade over its last 5 ms, then it stands */
        if (at >= t->len - (int)(0.005f * sr) - 1) want = 0;
        if (t->amp <= 0 || (at == 0 && t->amp < 1)) { S(i)->state = LP_STOP; stop_cancel(i); t->amp = 0; mirror(); continue; }
      }
      if (t->amp < want) { t->amp += fstep; if (t->amp > want) t->amp = want; }
      else if (t->amp > want) { t->amp -= fstep; if (t->amp < want) t->amp = want; }
      if (t->zap && t->amp <= 0) { t->len = 0; t->zap = 0; if (!any_loop()) ml = 0; continue; }
      if (!t->len || t->amp <= 0) continue;
      int pos = at;
      float a = f32(t->lb[2 * pos]), b = f32(t->lb[2 * pos + 1]);
      if (s == LP_DUB && dub_t == i) {
        if (t->pend_dub) {                          /* (a pressed overdub: the old values into TP – full: it becomes a layer now) */
          if (t->tp_n < TMAX && t->tp_n < t->len) { TP[2 * t->tp_n] = t->lb[2 * pos]; TP[2 * t->tp_n + 1] = t->lb[2 * pos + 1]; t->tp_n++; }
          else { dub_commit(i); goto plain; }
        } else plain: if (t->od_count < t->len) { t->ub[2 * pos] = t->lb[2 * pos]; t->ub[2 * pos + 1] = t->lb[2 * pos + 1]; t->od_count++; }
        float na = a * keep + in_l, nb = b * keep + in_r;
        t->lb[2 * pos] = s16(na); t->lb[2 * pos + 1] = s16(nb);
        int c = (int)((long long)pos * LT_COLS / t->len); float m = vd_maxf(vd_absf(na), vd_absf(nb));
        if (m > V.lt[i].over[c]) V.lt[i].over[c] = m;
      }
      float gt = master * V.lt[i].level; t->gain += (gt - t->gain) * gglide;
      float g = t->gain * t->amp, ya = a * g, yb = b * g;
      ol += ya; or_ += yb;
      float mono = (ya + yb) * 0.5f; d1 += mono * V.lt[i].send1; d2 += mono * V.lt[i].send2;
      float m = vd_maxf(vd_absf(ya), vd_absf(yb)); if (m > pk[i]) pk[i] = m;
    }
    ll[k] = ol; lr[k] = or_; s1[k] = d1; s2[k] = d2;
    if (go) G++;
  }
  for (int i = 0; i < NLT; i++) {
    lt_state *s = S(i); trk_t *t = &T[i]; int st_ = s->state;
    s->peak = pk[i] > s->peak ? pk[i] : s->peak * 0.85f;
    s->pos = (st_ == LP_REC ? t->n : st_ == LP_PLAY || st_ == LP_DUB ? pos_of(i) : 0) / sr;
    s->len = (st_ == LP_REC ? (t->pend ? t->pend : ml ? 0 : (int)V.loop_bars ? t->rec_len : t->n) : t->zap ? 0 : t->len) / sr;
  }
  mirror();
}

void loop_init(float rate) {
  sr = rate; maxlen = (int)(24 * rate); if (maxlen > LMAX) maxlen = LMAX;
  for (int i = 0; i < NLT; i++) { if (V.lt[i].level <= 0) V.lt[i].level = 0.9f; T[i].gain = V.loop_level * V.lt[i].level; clear(i); }
  V.loop_sel = 0; mirror();
}
int loop_master_len(float *sec) { if (sec) *sec = ml / sr; return ml > 0; }

/* ---- the loop as a file (SDK 13, 29.10.2026, Sven: "es sollte schon Samples speichern können"): F5 on the LOOP page
 * writes the chosen track's loop as a WAV – 32-bit float, stereo, the device's rate (nothing clipped: the loop keeps
 * 6 dB over full scale) – into "VIBE FM/vox/" on the host's writing source: the VIBE's card (/root/vibe-media), the
 * browser's Media folder, the plugin's media folder. The loop is copied at the press (it plays and overdubs on); the
 * writing runs from the draw and from VIBE_EV_MEDIA (READY also comes when the host does not draw), never from the audio. */
static int16_t sb[LMAX * 2];
static int sv_len, sv_at, sv_h, sv_hdr, sv_media = -1;
static char sv_name[48], sv_path[VIBE_MEDIA_PATH];

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static void sv_add(const char *s) { int n = 0; while (sv_name[n]) n++; while (*s && n < 47) sv_name[n++] = *s++; sv_name[n] = 0; }

void loop_save(void) {
  int i = V.loop_sel; trk_t *t = &T[i];
  if (V.loop_saving) { toast_e("STILL SAVING ..."); return; }
  if (st(i) == LP_EMPTY || st(i) == LP_REC || t->len <= 0) { toast_e("RECORD A LOOP FIRST"); return; }
  if (sv_media < 0) { char s[1024]; sv_media = vibe_media_sources(s, sizeof s) >= 0; }   /* (the first ask also turns on VIBE_EV_MEDIA) */
  if (!sv_media) { toast_e("NO FILES HERE (SDK 13)"); return; }
  __builtin_memcpy(sb, t->lb, (size_t)t->len * 2 * sizeof t->lb[0]);
  sv_len = t->len; sv_at = 0; sv_h = 0; sv_hdr = 0;
  char n[8]; sv_name[0] = 0; sv_add("VOX Loop "); sv_add(vg_itoa(i + 1, n));
  if ((int)V.loop_bars) { sv_add(" "); sv_add(vg_itoa((int)(V.bpm + 0.5f), n)); sv_add(" BPM"); }   /* (bars: the tempo belongs to it) */
  sv_add(".wav");
  V.loop_saving = 1; toast_e("SAVING THE LOOP");
  loop_save_work();
}

void loop_save_work(void) {                                    /* on until the host says "not yet" (READY carries it on) */
  static uint8_t b[32768];
  for (int guard = 0; V.loop_saving == 1 && guard < 8; guard++) {   /* (8 x 32 KB per picture: 1.5 kept the one flow short) */
    if (!sv_h) {
      int h = vibe_media_create(sv_name, sv_path, sizeof sv_path);
      if (h == VIBE_MEDIA_PENDING) return;
      if (h < 0) { V.loop_saving = 0; toast_e("NOWHERE TO SAVE"); return; }
      sv_h = h; continue;
    }
    int n, fr = 0;
    if (!sv_hdr) {                                             /* RIFF, fmt (3 = IEEE float), data */
      const char *ri = "RIFF", *wa = "WAVEfmt ", *da = "data"; uint32_t bytes = (uint32_t)sv_len * 8;
      for (int i = 0; i < 4; i++) { b[i] = (uint8_t)ri[i]; b[36 + i] = (uint8_t)da[i]; } for (int i = 0; i < 8; i++) b[8 + i] = (uint8_t)wa[i];
      put32(b + 4, 36 + bytes); put32(b + 16, 16); b[20] = 3; b[21] = 0; b[22] = 2; b[23] = 0;
      put32(b + 24, (uint32_t)sr); put32(b + 28, (uint32_t)sr * 8); b[32] = 8; b[33] = 0; b[34] = 32; b[35] = 0; put32(b + 40, bytes);
      n = 44;
    } else if (sv_at < sv_len) {
      fr = sv_len - sv_at; if (fr > (int)sizeof b / 8) fr = (int)sizeof b / 8;
      for (int i = 0; i < 2 * fr; i++) { union { float f; uint32_t u; } v; v.f = f32(sb[2 * sv_at + i]); put32(b + 4 * i, v.u); }
      n = fr * 8;
    } else {                                                   /* all of it: the host names it when it is complete (WRITTEN) */
      vibe_media_close(sv_h, 1); sv_h = 0; V.loop_saving = 2; return;
    }
    int r = vibe_media_append(sv_h, b, n);
    if (r == VIBE_MEDIA_PENDING) return;                       /* (the host still writes: the same again later) */
    if (r < 0) { vibe_media_close(sv_h, 0); sv_h = 0; V.loop_saving = 0; toast_e("SAVE FAILED"); return; }
    if (!sv_hdr) sv_hdr = 1; else sv_at += fr;
  }
}

void loop_save_event(const vibe_event *e) {                    /* VIBE_EV_MEDIA */
  if (e->id == VIBE_MEDIA_EV_READY) loop_save_work();
  else if (e->id == VIBE_MEDIA_EV_WRITTEN && V.loop_saving == 2) {
    V.loop_saving = 0;
    if (!e->down) { toast_e("SAVE FAILED"); return; }
    const char *f = sv_path; for (const char *p = sv_path; *p; p++) if (*p == '/') f = p + 1;
    char t[40] = "SAVED: "; int k = 13; while (*f && k < 39) t[k++] = *f++; t[k] = 0; toast_e(t);
  }
}
