/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* main.c – VOX: events, pages and their encoders, programs on the pads, the audio chain.
   Colour keys K1-K6 = the areas INPUT EQ VOX PITCH FX LOOP (shown on the right of the display); the auto-tune is
   VOX > TUNE.
   F1 MIXER (SHIFT: DRY) · F2 PRESETS · F3 PRG − · F4 PRG + · F5 SAVE (hold + pad = save there, SHIFT: factory sound)
   · F6 GLOBAL (SHIFT: loop UNDO). On LOOP, F5 = the loop as a WAV (SDK 13, "VIBE FM/vox/"). Encoders 1-4 = the four values of the page, A = tempo, B = DRY/WET (always, on every page), DATA = output.
   Pads 1-16 = programs (tap = keep, hold = punch-in). Keys = vocoder chord / scale TASTEN / harmony TASTEN,
   OCT -/+ shifts them, ENTER = HOLD (or: switch the input on). REC and the pedal = the looper's one switch as the Ditto
   Looper (tap: REC / PLAY / OVERDUB · hold: undo, stopped: delete · 2× tap: STOP · 2× tap + hold: STOP and delete);
   SHIFT + REC: delete. PLAY and STOP (2×: delete) as before – all on the chosen loop track. Since 1.4 the looper has
   FOUR tracks (loop.c): on the LOOP page the pads are the tracks (1-4 their buttons, 5-8 STOP), encoders 1-4 their
   levels, SHIFT + STOP = all stop. */
#include "vox.h"

static int help;                                     /* HOME: the controls (vg_controls) */
static const char *const HELP[] = {
  "K1 - K6", "INPUT EQ VOX PITCH FX LOOP \xC2\xB7 again: next sub-page",
  "ENCODER 1 - 4", "the four values of the page", "A \xC2\xB7 B \xC2\xB7 DATA", "tempo \xC2\xB7 dry / wet \xC2\xB7 output",
  "PADS", "programs (tap = keep, hold = punch-in) \xC2\xB7 LOOP: tracks",
  "PAD 1-4 \xC2\xB7 ENC PUSH", "LOOP: one switch per track (Ditto): tap \xC2\xB7 2\xC3\x97 stop \xC2\xB7 hold undo / clear",
  "KEYS \xC2\xB7 OCT", "vocoder chord / harmony / scale \xC2\xB7 octave",
  "REC \xC2\xB7 PEDAL", "looper: tap REC / PLAY / DUB \xC2\xB7 hold: undo",
  "PLAY \xC2\xB7 STOP", "the loop track (STOP 2\xC3\x97: delete)",
  "F1 \xC2\xB7 F2", "mixer (SHIFT: dry) \xC2\xB7 presets", "F3 \xC2\xB7 F4", "program - / +",
  "F5 \xC2\xB7 F6", "save (LOOP: WAV) \xC2\xB7 global (SHIFT: loop undo)",
  "ENTER", "hold the chord \xC2\xB7 or: input on" };
vox_state V;
double v_ms(void) { return V.now / V.sr * 1000.0; }
void toast(const char *t) { int i = 0; for (; t[i] && i < 39; i++) V.toast[i] = t[i]; V.toast[i] = 0; V.toast_until = v_ms() + 1600; }
void toast_e(const char *t) { int i = 0; for (; t[i] && i < 39; i++) V.toast_e[i] = t[i]; V.toast_e[i] = 0; V.toast_e_until = v_ms() + 1600; }

/* ---- two flows (Protokoll 6.2, VOX 1.5 – Sven 11.11.2026: "das muss stabil laufen"). Before, VOX ran in ONE flow on
 * the VIBE: the display and the keys held up the sound (a pass up to 21 ms against 8 ms of buffer – it crackled as
 * soon as one sang). Now: the control flow (vibe_on_event) keeps only FOCUS and the clicks and queues the rest; the audio
 * flow handles every queued event at the start of its block (the looper, the notes, the programs – one writer); what it
 * may not do (store, requests, the WAV, the DAW's parameters) it asks for with a counter and the draw does it. */
VIBE_PARALLEL_AUDIO
volatile uint32_t want_input, want_input_mode, want_glb, want_prog, want_loop_save;
enum { CMD_SELECT = 200, CMD_PRESET, CMD_ROW, CMD_LOOPROW, CMD_SHIFT_USED };   /* (queued from the control flow) */
#define EQN 256
static vibe_event evq[EQN];
static volatile uint32_t evq_w, evq_r, evq_lost;
static void push(const vibe_event *e) {          /* the control flow: the only writer of evq_w */
  uint32_t w = evq_w; if (w - evq_r >= EQN) { evq_lost++; return; }
  evq[w & (EQN - 1)] = *e; VIBE_RELEASE(); evq_w = w + 1;
}
static void pushc(int type, int id, int down) { vibe_event c = { 0 }; c.type = (uint8_t)type; c.id = (uint8_t)id; c.down = (uint8_t)down; push(&c); }

/* ---------------------------------------------------------------- parameters ------------------------------------- */
static const char *const MODE_N[] = { "LINE", "MIC", "HI-Z", "USB" };   /* USB (12.11.): the computer at the VIBE's USB cable, L+R */
static const char *const VOX_N[] = { "OFF", "TUNE", "VOCODER", "HARMONY" };
static const char *const CAR_N[] = { "SAW", "PULSE", "SUPER", "IN 2" };
static const char *const DIV_N[] = { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2" };
static const char *const KEY_N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const SCALE_N[] = { "CHROM.", "MAJOR", "MINOR", "PENTA", "BLUES", "KEYS" };
static const char *const HARM_N[] = { "THIRD", "FIFTH", "TRIAD", "OCT DOWN", "CHOIR", "KEYS" };
static const char *const COL_N[] = { "OFF", "RADIO", "PHONE", "MEGAPHONE", "ROBOT", "LO-FI" };
static const char *const OFFON_N[] = { "OFF", "ON" };
const char *const DLY_N[] = { "DIGITAL", "ANALOG", "TAPE", "PING-PONG" };   /* (1.6: also ui.c) */
const char *const REV_N[] = { "HALL", "ROOM", "PLATE", "SPRING" };
const char *const MOD_N[] = { "CHORUS", "FLANGER", "PHASER" };
static const char *const BARS_N[] = { "FREE", "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
static const char *const PADM_N[] = { "PUNCH-IN", "TOGGLE" };
static const char *const CSTYLE_N[] = { "VOICE", "FET", "OPTO", "CLEAN" };
enum { F_DB, F_PCT, F_NAME, F_GATE, F_INT, F_ST, F_SPEED, F_AMT, F_WET, F_PRG, F_CUT, F_LOGHZ, F_Q, F_KHZ, F_PRE, F_RATE };   /* (F_PRE: 0..100 ms · F_RATE: 0.05..6 Hz, 1.6) */
typedef struct { const char *label; float *v; float min, max, step; int fmt; const char *const *names; } pdef;
static float prg_sel;                      /* PRESETS page: the program under encoder 1 */

static int page_defs(int page, pdef d[4]) {
  static const pdef none = { 0 };
  for (int i = 0; i < 4; i++) d[i] = none;
  switch (page) {
    case P_INPUT:
      if (V.shift || V.sub_in) {                   /* (SETUP, or SHIFT: what is set once – the input, the compressor's kind, the S) */
        d[0] = (pdef){ "MODE", &V.in_mode, 0, 3, 1, F_NAME, MODE_N };
        d[1] = (pdef){ "COMP TYPE", &V.comp_style, 0, CS_COUNT - 1, 1, F_NAME, CSTYLE_N };
        d[2] = (pdef){ "S-FREQ", &V.deess_f, 4000, 10000, 250, F_KHZ, 0 };
      } else {
        d[0] = (pdef){ "GAIN", &V.gain_db, -12, 40, 1, F_DB, 0 };
        d[1] = (pdef){ "GATE", &V.gate_db, -80, -20, 1, F_GATE, 0 };
        d[2] = (pdef){ "COMP", &V.comp, 0, 1, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "DE-ESS", &V.deess, 0, 1, 0.02f, F_AMT, 0 };
      }
      return 1;
    case P_EQ:                                     /* (SHIFT: the bell's frequency and Q) */
      d[0] = (pdef){ "LOW CUT", &V.s.eq_cut, 0, 400, 10, F_CUT, 0 };
      d[1] = (pdef){ "LOW", &V.s.eq_low, -12, 12, 0.5f, F_DB, 0 };
      if (V.shift || V.sub_eq) {
        d[2] = (pdef){ "MID FREQ", &V.s.eq_mid_f, 0, 1, 0.01f, F_LOGHZ, 0 };
        d[3] = (pdef){ "MID Q", &V.s.eq_mid_q, 0.5f, 4, 0.1f, F_Q, 0 };
      } else {
        d[2] = (pdef){ "MID", &V.s.eq_mid, -12, 12, 0.5f, F_DB, 0 };
        d[3] = (pdef){ "HIGH", &V.s.eq_high, -12, 12, 0.5f, F_DB, 0 };
      }
      return 1;
    case P_VOX:
      d[0] = (pdef){ "MODE", &V.s.vox_mode, 0, VOX_COUNT - 1, 1, F_NAME, VOX_N };
      if ((int)V.s.vox_mode == VOX_TUNE) {         /* the auto-tune: STÄRKE is the master (100 % = laid flat at once) */
        d[1] = (pdef){ "KEY", &V.tune_key, 0, 11, 1, F_NAME, KEY_N };
        d[2] = (pdef){ "SCALE", &V.tune_scale, 0, SC_COUNT - 1, 1, F_NAME, SCALE_N };
        d[3] = (pdef){ "AMOUNT", &V.s.tune_amt, 0, 1, 0.05f, F_AMT, 0 };
      } else if ((int)V.s.vox_mode == VOX_VOCODER) {
        d[1] = (pdef){ "BANDS", &V.s.bands, 8, 24, 4, F_INT, 0 };
        d[2] = (pdef){ "CARRIER", &V.s.carrier, 0, CAR_COUNT - 1, 1, F_NAME, CAR_N };
        d[3] = (pdef){ "MIX", &V.s.mix, 0, 1, 0.02f, F_PCT, 0 };
      } else if ((int)V.s.vox_mode == VOX_HARMONY) {
        d[1] = (pdef){ "VOICES", &V.s.harm, 0, HM_COUNT - 1, 1, F_NAME, HARM_N };
        d[2] = (pdef){ "WIDTH", &V.s.harm_width, 0, 1, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "LEVEL", &V.s.harm_mix, 0, 1, 0.02f, F_PCT, 0 };
      }
      return 1;
    case P_PITCH:
      d[0] = (pdef){ "PITCH", &V.s.pitch_st, -12, 12, 1, F_ST, 0 };
      d[1] = (pdef){ "FORMANT", &V.s.formant_st, -8, 8, 1, F_ST, 0 };
      d[2] = (pdef){ "DOUBLER", &V.s.doubler, 0, 1, 0.02f, F_PCT, 0 };
      d[3] = (pdef){ "COLOR", &V.s.colour, 0, CO_COUNT - 1, 1, F_NAME, COL_N };
      return 1;
    case P_FX:                                     /* 1.6 (Sven 11.11.): three sub-pages – K5 again = the next */
      if (V.sub_fx == 1) {
        d[0] = (pdef){ "REVERB", &V.s.hall, 0, 1, 0.02f, F_PCT, 0 };
        d[1] = (pdef){ "SIZE", &V.s.room, 0, 1, 0.02f, F_PCT, 0 };
        d[2] = (pdef){ "PRE-DELAY", &V.s.rev_pre, 0, 1, 0.02f, F_PRE, 0 };
        d[3] = (pdef){ "TYPE", &V.s.rev_type, 0, RT_COUNT - 1, 1, F_NAME, REV_N };
      } else if (V.sub_fx == 2) {
        d[0] = (pdef){ "AMOUNT", &V.s.mod_mix, 0, 1, 0.02f, F_PCT, 0 };
        d[1] = (pdef){ "RATE", &V.s.mod_rate, 0, 1, 0.02f, F_RATE, 0 };
        d[2] = (pdef){ "DEPTH", &V.s.mod_depth, 0, 1, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "TYPE", &V.s.mod_type, 0, MT_COUNT - 1, 1, F_NAME, MOD_N };
      } else {
        d[0] = (pdef){ "DELAY", &V.s.delay, 0, 1, 0.02f, F_PCT, 0 };
        d[1] = (pdef){ "TIME", &V.s.delay_div, 0, 6, 1, F_NAME, DIV_N };
        d[2] = (pdef){ "FEEDBACK", &V.s.feedback, 0, 0.96f, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "TYPE", &V.s.dly_type, 0, DT_COUNT - 1, 1, F_NAME, DLY_N };
      }
      return 1;
    case P_LOOP:                                   /* K6 again = the row: PEGEL · SEND 1 DELAY · SEND 2 HALL · LOOP (length, overdub, the loops' level) – SHIFT changes nothing here */
      if (V.loop_row == 3) {
        d[0] = (pdef){ "LENGTH", &V.loop_bars, 0, 4, 1, F_NAME, BARS_N };
        d[1] = (pdef){ "OVERDUB", &V.loop_keep, 0.5f, 1, 0.02f, F_PCT, 0 };
        d[2] = (pdef){ "LOOPS", &V.loop_level, 0, 1, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "REC FX", &V.rec_fx, 0, 1, 1, F_NAME, OFFON_N };   /* (1.6: the effects into the recording) */
      } else {                                     /* the row: PEGEL · SEND 1 DELAY · SEND 2 HALL (K6 again = the next) */
        static const char *const TN[3][NLT] = { { "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4" },
                                                { "DELAY 1", "DELAY 2", "DELAY 3", "DELAY 4" },
                                                { "REVERB 1", "REVERB 2", "REVERB 3", "REVERB 4" } };
        int row = V.loop_row < 0 || V.loop_row > 3 ? 0 : V.loop_row;
        for (int i = 0; i < NLT; i++) d[i] = (pdef){ TN[row][i], row == 0 ? &V.lt[i].level : row == 1 ? &V.lt[i].send1 : &V.lt[i].send2, 0, 1, 0.02f, F_PCT, 0 };
      }
      return 1;
    case P_MIX:
      d[0] = (pdef){ "DRY/WET", &V.s.wet, 0, 1, 0.02f, F_WET, 0 };
      d[1] = (pdef){ "DELAY", &V.s.delay, 0, 1, 0.02f, F_PCT, 0 };
      d[2] = (pdef){ "REVERB", &V.s.hall, 0, 1, 0.02f, F_PCT, 0 };
      d[3] = (pdef){ "LOOP", &V.loop_level, 0, 1, 0.02f, F_PCT, 0 };
      return 1;
    case P_PRESETS:
      d[0] = (pdef){ "PROGRAM", &prg_sel, 0, N_PRESETS - 1, 1, F_PRG, 0 };
      return 1;
    case P_GLOBAL:
      d[0] = (pdef){ "TEMPO", &V.bpm, 40, 240, 1, F_INT, 0 };
      d[1] = (pdef){ "OUTPUT", &V.out_db, -40, 6, 1, F_DB, 0 };
      d[2] = (pdef){ "PADS", &V.pad_mode, 0, 1, 1, F_NAME, PADM_N };
      return 1;
  }
  return 0;
}

static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }
static void fmt(const pdef *d, char *out) {
  char b[16]; float v = *d->v; out[0] = 0;
  switch (d->fmt) {
    case F_NAME: cat(out, d->names[(int)(v + 0.5f)]); break;
    case F_INT: cat(out, vg_itoa((int)(v + 0.5f), b)); break;
    case F_PRG: cat(out, vg_itoa((int)(v + 0.5f) + 1, b)); break;
    case F_WET: { int p = (int)(v * 100 + 0.5f); if (p == 0) cat(out, "DRY"); else if (p == 100) cat(out, "WET"); else { cat(out, vg_itoa(p, b)); cat(out, " %"); } break; }
    case F_AMT: if (v < 0.001f) { cat(out, "OFF"); break; } /* fall through */
    case F_PCT: cat(out, vg_itoa((int)(v * 100 + 0.5f), b)); cat(out, " %"); break;
    case F_SPEED: { int ms = (int)(400 * v * v + 0.5f); if (ms == 0) cat(out, "HARD"); else { cat(out, vg_itoa(ms, b)); cat(out, " ms"); } break; }
    case F_CUT: if (v < 20) { cat(out, "OFF"); break; } cat(out, vg_itoa((int)(v + 0.5f), b)); cat(out, " Hz"); break;
    case F_LOGHZ: { int hz = (int)(eq_mid_hz(v) + 0.5f);
      if (hz < 1000) { cat(out, vg_itoa(hz, b)); cat(out, " Hz"); }
      else { cat(out, vg_itoa(hz / 1000, b)); cat(out, "."); cat(out, vg_itoa(hz / 100 % 10, b)); cat(out, " kHz"); } break; }
    case F_KHZ: { int hz = (int)(v + 0.5f); cat(out, vg_itoa(hz / 1000, b)); cat(out, "."); cat(out, vg_itoa(hz / 100 % 10, b)); cat(out, " kHz"); break; }
    case F_PRE: cat(out, vg_itoa((int)(v * 100 + 0.5f), b)); cat(out, " ms"); break;   /* (1.6: 0..100 ms) */
    case F_RATE: { int c = (int)(mod_hz(v) * 100 + 0.5f); cat(out, vg_itoa(c / 100, b)); cat(out, "."); if (c % 100 < 10) cat(out, "0"); cat(out, vg_itoa(c % 100, b)); cat(out, " Hz"); break; }
    case F_Q: { int q = (int)(v * 10 + 0.5f); cat(out, vg_itoa(q / 10, b)); cat(out, "."); cat(out, vg_itoa(q % 10, b)); break; }
    case F_ST: { int i = (int)(v < 0 ? v - 0.5f : v + 0.5f); if (i > 0) cat(out, "+"); cat(out, vg_itoa(i, b)); cat(out, " ST"); break; }
    case F_GATE: if (v <= -80) { cat(out, "OFF"); break; } /* fall through */
    case F_DB: { int i = (int)(v < 0 ? v - 0.5f : v + 0.5f); if (i > 0) cat(out, "+"); cat(out, vg_itoa(i, b)); cat(out, " dB"); break; }
  }
}

void params_get(v_param p[4]) {
  pdef d[4]; page_defs(V.page, d);
  for (int i = 0; i < 4; i++) {
    p[i].label = d[i].label; p[i].bipolar = 0; p[i].text[0] = 0; p[i].value = 0;
    if (!d[i].label) continue;
    fmt(&d[i], p[i].text);
    p[i].value = (*d[i].v - d[i].min) / (d[i].max - d[i].min);
    p[i].bipolar = d[i].fmt == F_ST;
  }
}

/* ---------------------------------------------------------------- programs (pads) -------------------------------- */
static int auto_chord;                     /* a vocoder program put a chord on HOLD by itself */
static void chord_release(void) { if (auto_chord) { auto_chord = 0; V.hold = 0; carrier_release_all(); } }
static void chord_auto(void) {             /* a vocoder needs notes: if none are held, a chord goes on HOLD */
  if ((int)V.s.vox_mode != VOX_VOCODER) return;
  int have[MAX_VOICES]; if (carrier_notes(have, MAX_VOICES)) return;      /* the player's chord wins */
  static const int CM7[] = { 48, 55, 58, 63 }, OCT[] = { 36, 48 };
  const int *c = (int)V.s.carrier == CAR_PULSE ? OCT : CM7; int n = (int)V.s.carrier == CAR_PULSE ? 2 : 4;
  V.hold = 1; auto_chord = 1;
  for (int i = 0; i < n; i++) carrier_note(c[i], 1);
  for (int i = 0; i < n; i++) carrier_note(c[i], 0);
}
static void prog_load(int i) { chord_release(); V.s = *prog_get(i); V.preset = i; prg_sel = (float)i; chord_auto(); }

static void prog_save(int i) {
  prog_store(i, &V.s, V.preset); V.preset = i; prg_sel = (float)i; global_save();
  char b[40] = "SAVED ON PAD "; char t[8]; cat(b, vg_itoa(i + 1, t)); toast(b);
}

static vox_sound snap; static int snap_preset, pad_held = -1; static double pad_t;
static void pad(int i, int down) {
  if (i < 0 || i >= N_PRESETS) return;            /* (16 pads, 16 programs: another id never reads past them) */
  if (V.page == P_LOOP) {                         /* LOOP: the pads are the looper – no programs, no punch-in there */
    int tap = V.shift_tap > 0 && v_ms() - V.shift_tap < 1500;   /* (SHIFT tapped just before: as if held – VIBE DJ's way) */
    if (down && (V.shift || tap)) { V.shift_tap = 0; loop_select(i % NLT); return; }   /* SHIFT + pad 1-8: that track chosen, nothing plays */
    loop_pad(i, down); return;
  }
  if (down) {
    if (V.save_held) { V.save_used = 1; prog_save(i); return; }             /* SAVE held + pad = save there */
    snap = V.s; snap_preset = V.preset; pad_held = i; pad_t = v_ms();
    prog_load(i);
    toast(prog_name(i));
  } else if (i == pad_held) {
    pad_held = -1;
    if ((int)V.pad_mode == 0 && v_ms() - pad_t > 450) {                     /* held: only while held (punch-in) */
      chord_release();
      V.s = snap; V.preset = snap_preset; if (snap_preset >= 0) prg_sel = (float)snap_preset;
      chord_auto();
    }
  }
}

/* ---------------------------------------------------------------- encoders --------------------------------------- */
static float acc[4];                       /* stepped values move one step per detent, fractional turns add up */
static void param_turn(int i, float detents) {
  pdef d[4]; if (!page_defs(V.page, d) || !d[i].label) return;
  float v = *d[i].v;
  if (d[i].step >= 1) {
    acc[i] += detents;
    while (acc[i] >= 1) { v += d[i].step; acc[i] -= 1; }
    while (acc[i] <= -1) { v -= d[i].step; acc[i] += 1; }
  } else v += detents * d[i].step * (V.shift ? 0.25f : 1);
  *d[i].v = vd_clamp(v, d[i].min, d[i].max);
  if (d[i].v == &V.in_mode) want_input_mode++;                                /* (the draw tells the host) */
  if (d[i].v == &prg_sel && (int)prg_sel != V.preset) prog_load((int)prg_sel);
}

/* ---------------------------------------------------------------- events ----------------------------------------- */
static void set_page(int p) {
  if (V.page == P_GLOBAL && p != P_GLOBAL) global_save();                  /* leaving GLOBAL keeps its settings */
  V.page = p; acc[0] = acc[1] = acc[2] = acc[3] = 0;
  if (p == P_INPUT && !V.input_on) want_input++;
  if (p == P_PRESETS && V.preset >= 0) prg_sel = (float)V.preset;
}

/* ---------------------------------------------------------------- the DAW (SDK 8) ------------------------------- */
/* VOX's settings as DAW parameters, both ways: the DAW sets them (VIBE_EV_PARAM), and what the panel, a preset or MIDI
 * changes goes back to the DAW (daw_sync, once a block – only what differs). Names as a DAW shows them. */
static const char *const KIND_C[4] = { "Voice", "FET", "Opto", "Clean" };
static const vibe_param VP[] = {
  { "gain", "Gain", -12, 40, 6, "dB", 0, 0 },          { "gate", "Gate", -80, -20, -55, "dB", 0, 0 },
  { "comp", "Compressor", 0, 1, 0.4f, "", 0, 0 },       { "comp_kind", "Compressor Kind", 0, 3, 0, "", 4, KIND_C },
  { "deess", "De-Esser", 0, 1, 0.3f, "", 0, 0 },        { "deess_freq", "De-Esser Freq", 4000, 10000, 6500, "Hz", 0, 0 },
  { "tune", "Tune Amount", 0, 1, 0, "", 0, 0 },         { "tune_speed", "Tune Speed", 0, 1, 0.35f, "", 0, 0 },
  { "pitch", "Pitch", -12, 12, 0, "st", 25, 0 },        { "formant", "Formant", -8, 8, 0, "st", 17, 0 },
  { "doubler", "Doubler", 0, 1, 0, "", 0, 0 },          { "delay", "Delay", 0, 1, 0, "", 0, 0 },
  { "feedback", "Feedback", 0, 0.96f, 0.3f, "", 0, 0 }, { "hall", "Reverb", 0, 1, 0, "", 0, 0 },
  { "room", "Room", 0, 1, 0.5f, "", 0, 0 },             { "wet", "Dry / Wet", 0, 1, 1, "", 0, 0 },
  { "delay_type", "Delay Type", 0, 3, 3, "", 4, DLY_N }, { "reverb_type", "Reverb Type", 0, 3, 0, "", 4, REV_N },   /* (1.6: appended – the ids before stay) */
  { "predelay", "Pre-Delay", 0, 1, 0, "", 0, 0 },       { "mod_type", "Mod Type", 0, 2, 0, "", 3, MOD_N },
  { "mod_rate", "Mod Rate", 0, 1, 0.3f, "", 0, 0 },     { "mod_depth", "Mod Depth", 0, 1, 0.5f, "", 0, 0 },
  { "mod_amount", "Mod Amount", 0, 1, 0, "", 0, 0 },
};
#define NVP ((int)(sizeof VP / sizeof VP[0]))
static const char *const VG[] = { "INPUT", "INPUT", "INPUT", "INPUT", "INPUT", "INPUT", "TUNE", "TUNE",   /* (the Editor's groups) */
                                  "PITCH", "PITCH", "PITCH", "FX", "FX", "FX", "FX", "MIX", "FX", "FX", "FX", "FX", "FX", "FX", "FX" };
static float *vp_val(int i) {
  float *const P[NVP] = { &V.gain_db, &V.gate_db, &V.comp, &V.comp_style, &V.deess, &V.deess_f, &V.s.tune_amt, &V.s.tune_speed,
                          &V.s.pitch_st, &V.s.formant_st, &V.s.doubler, &V.s.delay, &V.s.feedback, &V.s.hall, &V.s.room, &V.s.wet,
                          &V.s.dly_type, &V.s.rev_type, &V.s.rev_pre, &V.s.mod_type, &V.s.mod_rate, &V.s.mod_depth, &V.s.mod_mix };
  return i >= 0 && i < NVP ? P[i] : 0;
}
static void daw_sync(void) {
  for (int i = 0; i < NVP; i++) { float v = *vp_val(i); if (vibe_param_get(i) != v) vibe_param_set(i, v); }
}

void vibe_on_init(float sample_rate) {
  vibe_extras_own(1);    /* (1.5, SDK 19 extras: X1-X4 – the encoder pushes – are ours: LOOP = the tracks (loop_enc), elsewhere pads 1-4 as before) */
  vf_mode(VF_ON_HOLD);   /* (SDK 10 FOCUS: DATA stays the program; ENTER held 0.6 s = FOCUS) */
  vf_shift_walk(1);      /* (SHIFT held + DATA: the frame walks every control of the page, one by one – as VIBE DJ's DECKS) */
  vf_clicks(1);          /* (SDK 18: the plugin's window and the browser – a click on a FOCUS control presses it, a fader takes the value) */
  V.sr = sample_rate;
  V.in_mode = VIBE_IN_MIC; V.gain_db = 6; V.gate_db = -55; V.comp = 0.4f;
  V.comp_style = CS_VOICE; V.deess = 0.3f; V.deess_f = 6500;              /* (a voice: both stages, a little de-essing) */
  V.tune_key = 0; V.tune_scale = SC_MAJOR;           /* (C major: the tune is heard; CHROM. on the TUNE page) */
  V.loop_bars = 0; V.loop_keep = 0.9f; V.loop_level = 0.9f; V.rec_fx = 1;
  V.bpm = 120; V.out_db = 0; V.pad_mode = 0;
  prog_init();                                                              /* programs + global settings */
  vibe_params(VP, NVP); vibe_param_groups(VG); daw_sync();                                         /* (SDK 8: the DAW's parameters, at the settings now) */
  dsp_init(sample_rate); voice_init(sample_rate); loop_init(sample_rate);
  vibe_input_mode(0, V.in_mode < 2.5f ? (int)V.in_mode : 0);
  static const char *const K[6] = { "INPUT", "EQ", "VOX", "PITCH", "FX", "LOOP" };
  static const char *const F[6] = { "MIXER", "PRESETS", "PRG -", "PRG +", "SAVE", "GLOBAL" };
  for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, K[i]); vibe_label(VB_F1 + i, F[i]); }
  if (V.preset >= 0) { prg_sel = (float)V.preset; chord_auto(); }
  vibe_request(VIBE_REQ_AUDIO_INPUT);
  vibe_log("VOX 1.6 started");
}

static void key(int note, int down) {
  int used = (int)V.s.vox_mode == VOX_VOCODER || (int)V.tune_scale == SC_KEYS || ((int)V.s.vox_mode == VOX_HARMONY && (int)V.s.harm == HM_KEYS);
  if (down && !used) toast("KEYS: VOCODER, SCALE/VOICES KEYS");
  if (down && auto_chord) { auto_chord = 0; carrier_release_all(); V.hold = 0; }   /* playing replaces the program's chord */
  carrier_note(note, down);
}

static void prg_go(int i) {
  prog_load(i); char b[40] = ""; char t[8]; cat(b, vg_itoa(i + 1, t)); cat(b, " "); cat(b, prog_name(i)); toast(b);
}
static void prg_step(int d) { prg_go(V.preset < 0 ? (d > 0 ? 0 : N_PRESETS - 1) : (V.preset + d + N_PRESETS) % N_PRESETS); }

/* the sub-pages (01.11., Sven: chips as in ZVAMPLER, so one sees there are more): the same colour key again = the next,
   a chip clicked (plugin, browser) = that one. INPUT: LEVELS · SETUP (the SHIFT layer), EQ: BANDS · MID (the SHIFT
   layer), VOX: its mode (encoder 1 too), LOOP: the encoders' row */
static const char *const SUB_IN[] = { "LEVELS", "SETUP" }, *const SUB_EQ[] = { "BANDS", "MID" }, *const SUB_LOOP[] = { "VOL", "DLY", "REV", "LENGTH" },
                         *const SUB_FX[] = { "DELAY", "REVERB", "MOD" };
int sub_count(int page) { return page == P_INPUT || page == P_EQ ? 2 : page == P_VOX ? VOX_COUNT : page == P_LOOP ? 4 : page == P_FX ? 3 : 0; }
int sub_now(int page) { return page == P_INPUT ? V.sub_in : page == P_EQ ? V.sub_eq : page == P_VOX ? (int)V.s.vox_mode : page == P_LOOP ? (V.loop_row < 0 || V.loop_row > 3 ? 0 : V.loop_row) : page == P_FX ? V.sub_fx : 0; }
const char *const *sub_names(int page) { return page == P_INPUT ? SUB_IN : page == P_EQ ? SUB_EQ : page == P_VOX ? VOX_N : page == P_FX ? SUB_FX : SUB_LOOP; }
static void loop_row_set(int row);
static void sub_set(int page, int k) {
  if (page == P_LOOP) { loop_row_set(k); return; }
  if (page == P_INPUT) V.sub_in = k; else if (page == P_EQ) V.sub_eq = k; else if (page == P_VOX) V.s.vox_mode = (float)k; else if (page == P_FX) V.sub_fx = k; else return;
  acc[0] = acc[1] = acc[2] = acc[3] = 0;
}
static void loop_row_set(int row) {                                         /* LOOP: what the encoders set – VOL → DLY → REV → LENGTH · OVERDUB · LOOPS */
  static const char *const RN[4] = { "ENCODERS = LEVEL", "ENCODERS = SEND 1 DELAY", "ENCODERS = SEND 2 REVERB", "ENCODERS = LENGTH · OVERDUB · LOOPS · REC FX" };
  V.loop_row = row; acc[0] = acc[1] = acc[2] = acc[3] = 0; toast(RN[row]);
}
static void button(int b, int down) {
  if (b >= VB_X1) {                                                         /* an encoder push (X1-X4): LOOP = that track, elsewhere pad 1-4 as a host would */
    if (b < VB_X1 + 4) { if (V.page == P_LOOP) loop_enc(b - VB_X1, down); else pad(b - VB_X1, down); }
    return;
  }
  if (b == VB_F5) {                                                         /* SAVE: on release, unless a pad took it */
    if (down) { V.save_held = 1; V.save_used = 0; return; }
    V.save_held = 0;
    if (V.save_used) return;
    if (V.shift) { if (V.preset >= 0) { prog_reset(V.preset); prog_load(V.preset); toast("FACTORY SOUND BACK"); } else toast("CHOOSE A PAD FIRST"); return; }
    if (V.page == P_LOOP) { want_loop_save++; return; }                    /* (the WAV: the draw starts it – never in the audio) */                         /* (LOOP: the loop as a WAV) */
    if (V.preset >= 0) prog_save(V.preset); else { global_save(); toast("HOLD SAVE + PAD = SAVE THERE"); }
    return;
  }
  if (b == VB_REC) { if (down && V.shift) loop_clear_all(); else loop_switch(down); return; }   /* the looper's switch (Ditto); SHIFT: delete */
  if (!down) return;
  if (b >= VB_K1 && b <= VB_K6) {
    int p = P_INPUT + b - VB_K1;
    if (p == V.page && !V.shift && sub_count(p)) { sub_set(p, (sub_now(p) + 1) % sub_count(p)); return; }   /* the key again: the next sub-page (chip) */
    set_page(p); return;
  }
  switch (b) {
    case VB_F1:
      if (V.shift) { V.dry ^= 1; toast(V.dry ? "DRY – ONLY THE VOICE" : "EFFECTS ON"); }
      else set_page(P_MIX);
      break;
    case VB_F2: set_page(P_PRESETS); break;
    case VB_F3: case VB_LEFT: prg_step(-1); break;
    case VB_F4: case VB_RIGHT: prg_step(1); break;
    case VB_F6: if (V.shift) loop_button(VB_K3); else set_page(P_GLOBAL); break;
    case VB_OCT_DOWN: if (V.octave > -2) V.octave--; break;
    case VB_OCT_UP: if (V.octave < 2) V.octave++; break;
    case VB_ENTER: case VB_DATA_PUSH:
      if (!V.input_on) { want_input++; toast("TURNING THE INPUT ON"); break; }
      V.hold ^= 1; auto_chord = 0; if (!V.hold) carrier_release_all(); toast(V.hold ? "HOLD ON – THE CHORD STAYS" : "HOLD OFF"); break;
    case VB_HOME: help = !help; break;                 /* HOME = what every control does (Protokoll 3.2, 3.11.2026) */
    case VB_RETURN: help = 0; break;
    case VB_PLAY: loop_button(b); break;
    case VB_STOP: if (V.shift) loop_stop_all(); else loop_button(b); break;
  }
}

static int shift_used;                     /* something happened while SHIFT was down: no tap */
/* SDK 18: a click into the display (the plugin's window, the browser – never on the VIBE) where FOCUS has no control
   of its own. LOOP: a strip's STOP field = pad 5-8 (held while the mouse is down), its waveform = choose that track.
   (The header's ◀ ▶, the strips' buttons, the PRESETS' cells and the faders are FOCUS controls: vf_event takes them.) */
static int touch(const vibe_event *e) {
  static int stop_held = -1;
  int x = VIBE_TOUCH_X(e), y = VIBE_TOUCH_Y(e);
  if (e->id == VIBE_TOUCH_UP) { if (stop_held < 0) return 0; pushc(VIBE_EV_PAD, NLT + stop_held, 0); stop_held = -1; return 1; }
  if (e->id != VIBE_TOUCH_DOWN || V.page != P_LOOP || x < 12) return 0;
  int i = (x - 12) / 116; if (i >= NLT) return 0;
  if (y >= 92 + LOOP_OY && y < 108 + LOOP_OY) { stop_held = i; pushc(VIBE_EV_PAD, NLT + i, 1); return 1; }
  if (y >= 112 + LOOP_OY && y < 138 + LOOP_OY) { pushc(CMD_SELECT, i, 1); return 1; }
  return 0;
}

void vibe_on_event(const vibe_event *e) {         /* the control flow: FOCUS, the clicks – everything else queued */
  if (e->type == VIBE_EV_MEDIA) { loop_save_event(e); return; }   /* (SDK 13: the loop being written) */
  if (e->type == VIBE_EV_TOUCH && touch(e)) return;              /* (SDK 18: the display clicked – the looper's fields) */
  if (e->type == VIBE_EV_BUTTON && e->id == VB_SHIFT) { push(e); return; }
  { int id = -1, r = vf_event(e, &id);                      /* SDK 10 FOCUS first: stand-ins, and the PRESETS' cells */
    if (r == VF_NO) { push(e); return; }
    pushc(CMD_SHIFT_USED, 0, 0);
    if (r == VF_PRESS && id >= FO_PRESET && id < FO_PRESET + N_PRESETS) pushc(CMD_PRESET, id - FO_PRESET, 1);   /* (LOOP: the strips' buttons) */
    if (r == VF_PRESS && id >= FO_ROW && id < FO_ROW + 8) pushc(CMD_ROW, id - FO_ROW, 1);   /* (a chip clicked = that sub-page) */
    if (e->type == VIBE_EV_TOUCH && r == VF_MOVED && vf_focus() >= FO_FADER && vf_focus() < FO_FADER + NLT * 3)   /* a fader clicked: its row for the encoders */
      pushc(CMD_LOOPROW, (vf_focus() - FO_FADER) % 3, 1);
  }
}

static void handle(const vibe_event *e) {          /* the audio flow: one event out of the queue */
  if (e->type == VIBE_EV_BUTTON && e->id == VB_SHIFT) {   /* SHIFT held: SHIFT + … · tapped alone on LOOP: the next pad chooses a track (an MPK) */
    V.shift = e->down;
    if (e->down) { V.shift_t = v_ms(); shift_used = 0; }
    else if (!shift_used && e->value >= 0 && v_ms() - V.shift_t < 450 && V.page == P_LOOP) { V.shift_tap = v_ms(); toast("SHIFT – NOW A PAD = SELECT TRACK"); }
    return;
  }
  if (V.shift && !(e->type == VIBE_EV_PAD && !e->down)) shift_used = 1;
  switch (e->type) {
    case CMD_SHIFT_USED: break;
    case CMD_SELECT: loop_select(e->id); break;
    case CMD_PRESET: if (V.page == P_LOOP) { loop_pad(e->id, 1); loop_pad(e->id, 0); } else prg_go(e->id); break;
    case CMD_ROW: sub_set(V.page, e->id); break;
    case CMD_LOOPROW: V.loop_row = e->id; break;
    case VIBE_EV_BUTTON: button(e->id, e->down); break;
    case VIBE_EV_PAD: pad(e->id, e->down); break;
    case VIBE_EV_PEDAL: if (e->id == VIBE_PEDAL_SWITCH) loop_switch(e->down); break;
    case VIBE_EV_ENCODER:
      if (e->id <= VE_4) param_turn(e->id, e->value);
      else if (e->id == VE_A) V.bpm = vd_clamp(V.bpm + e->value * (V.shift ? 0.1f : 1), 40, 240);
      else if (e->id == VE_B) {                   /* DRY/WET: always at hand (Sven 24.10.), the hall stays on FX */
        V.s.wet = vd_clamp(V.s.wet + e->value * 0.02f, 0, 1);
        char t[24] = "DRY/WET ", n[8]; int pc = (int)(V.s.wet * 100 + 0.5f);
        cat(t, pc == 0 ? "DRY" : pc == 100 ? "WET" : vg_itoa(pc, n)); if (pc > 0 && pc < 100) cat(t, " %"); toast(t);
      }
      else if (e->id == VE_DATA) {                /* DATA = the program, as OberVave's sound (the output: GLOBAL, encoder 2) */
        static float acc; acc += e->value;
        while (acc >= 1) { acc -= 1; prg_step(1); }
        while (acc <= -1) { acc += 1; prg_step(-1); }
      }
      break;
    case VIBE_EV_KEY: key(48 + e->id + 12 * V.octave, e->down); break;
    case VIBE_EV_MIDI: {
      int st = e->midi[0] & 0xF0, ch = e->midi[0] & 15, d1 = e->midi[1], d2 = e->midi[2];
      if (ch == 9) { if (st == 0x90 && d2 && d1 >= 36 && d1 < 52) pad(d1 - 36, 1); else if ((st == 0x80 || st == 0x90) && d1 >= 36 && d1 < 52) pad(d1 - 36, 0); break; }
      if (st == 0x90 && d2) key(d1, 1);
      else if (st == 0x80 || st == 0x90) key(d1, 0);
      break; }
    case VIBE_EV_PARAM: { float *v = vp_val(e->id); if (v) *v = vibe_param_get(e->id); break; }   /* (SDK 8: the DAW) */
    case VIBE_EV_CLOCK: {                                                      /* CLK IN: one pulse per 16th */
      static float last; float dt = e->value;
      if (dt > 0.03f && dt < 0.4f) { float bpm = 60.0f / (dt * 4); V.bpm = last > 0 ? V.bpm * 0.6f + bpm * 0.4f : bpm; last = dt; }
      else last = 0;
      break; }
  }
}

/* ---------------------------------------------------------------- audio ------------------------------------------ */
static int usb_input(void) {                      /* the host's "USB L" (its "USB R" next to it), -1 = this host has none */
  int n = vibe_input_count();
  for (int i = 2; i + 1 < n; i++) { const char *a = vibe_input_name(i); if (a[0] == 'U' && a[1] == 'S' && a[2] == 'B' && a[3] == ' ' && a[4] == 'L') return i; }
  return -1;
}
void vibe_on_audio(const float *in, float *l, float *r, int n) {
  static float vl[VIBE_MAX_BLOCK], vr[VIBE_MAX_BLOCK], ll[VIBE_MAX_BLOCK], lr[VIBE_MAX_BLOCK], s1[VIBE_MAX_BLOCK], s2[VIBE_MAX_BLOCK], wet_s = 1;
  { uint32_t w = evq_w; VIBE_ACQUIRE();            /* the queued events, in order (1.5: two flows) */
    for (uint32_t r = evq_r; r != w; r++) { vibe_event ev = evq[r & (EQN - 1)]; handle(&ev); VIBE_RELEASE(); evq_r = r + 1; } }
  loop_tick();                                    /* (the looper's switch held: undo / delete) */
  eq_update();                                    /* (the EQ's filters, when a value changed) */
  const float *in2 = vibe_input(1);
  if (V.in_mode > 2.5f) {                         /* MODE USB (12.11.): the host's "USB L" / "USB R" (SDK 26), as one voice */
    static float ub[VIBE_MAX_BLOCK]; int u = usb_input();
    const float *ul = u >= 0 ? vibe_input(u) : 0, *ur = u >= 0 ? vibe_input(u + 1) : 0;
    if (ul && ur) { for (int i = 0; i < n; i++) ub[i] = 0.5f * (ul[i] + ur[i]); in = ub; } else in = 0;
  }
  if (in && in2) {                         /* a mono mic arrives on both channels: that is no IN 2 */
    int same = 1; for (int i = 0; i < n && same; i++) same = in[i] == in2[i];
    if (same) in2 = 0;
  }
  V.in2_live = in2 != 0;
  int voc = !V.dry && (int)V.s.vox_mode == VOX_VOCODER;
  float mix = V.s.mix, wet_t = V.dry ? 1 : V.s.wet;
  if (in) V.input_on = 1;
  for (int i = 0; i < n; i++) {
    float x = input_stage(in ? in[i] : 0), L, R;
    pitch_feed(x);
    pitch_engine(x, &L, &R);               /* auto-tune, pitch, formant, harmonies – or the plain voice */
    if (voc) { float y = vocoder((L + R) * 0.5f, in2 ? in2[i] : 0); L = L * (1 - mix) + y * mix; R = R * (1 - mix) + y * mix; }
    if (!V.dry) { colour(&L, &R); doubler(&L, &R); }
    wet_s += (wet_t - wet_s) * 0.002f;     /* DRY/WET: the plain voice against everything above */
    vl[i] = x + (L - x) * wet_s; vr[i] = x + (R - x) * wet_s;
  }
  static float wl[VIBE_MAX_BLOCK], wr[VIBE_MAX_BLOCK];
  fx_voice(vl, vr, wl, wr, n);             /* the voice with MOD, delay and reverb – as one hears it */
  if (V.rec_fx > 0.5f) loop_process(wl, wr, ll, lr, s1, s2, n); /* REC FX (1.6, Sven 11.11.): the looper records what one hears */
  else loop_process(vl, vr, ll, lr, s1, s2, n);                  /* (off: before delay, reverb, MOD – as before 1.6) */
  fx_out(wl, wr, ll, lr, s1, s2, l, r, n);  /* + the loops, their sends through their own delay + reverb, the master */
  if (voc) vocoder_meters();
  for (int i = 0; i < n; i += 2) { V.scope[V.scope_pos] = l[i]; V.scope_pos = (V.scope_pos + 1) & 511; }
  V.now += n;
}

static void vox_jobs(void) {                       /* the draw (control flow): what the audio flow asked for */
  static uint32_t d_in, d_mode, d_glb, d_prog, d_wav;
  uint32_t a = want_input, b = want_input_mode, c = want_glb, d = want_prog, w = want_loop_save; VIBE_ACQUIRE();
  if (a != d_in) { d_in = a; vibe_request(VIBE_REQ_AUDIO_INPUT); }
  if (b != d_mode) { d_mode = b; vibe_input_mode(0, V.in_mode < 2.5f ? (int)V.in_mode : 0); }
  if (c != d_glb) { d_glb = c; global_write(); }
  if (d != d_prog) { d_prog = d; prog_write(); }
  if (w != d_wav) { d_wav = w; loop_save(); }
  daw_sync();                                       /* (SDK 8: what changed, to the DAW – never from the audio) */
}
void vibe_on_draw(uint16_t *fb) { (void)fb; vox_jobs(); loop_save_work(); ui_draw(); if (help) vg_controls("VOX", HELP, sizeof HELP / sizeof HELP[0] / 2); ui_lights(); }
