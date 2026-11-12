/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* vox.h – VOX, the voice processor for VIBE (concept: KONZEPT.md).
   Signal path:  IN 1 → gain → gate → compressor → PITCH ENGINE (auto-tune, pitch, formant, harmonizer – PSOLA)
                 → VOCODER → COLOUR (radio, phone, megaphone, robot, lo-fi) → DOUBLER → LOOPER → delay → hall → OUT
   Files: main.c = events, pages, parameters, presets, the audio chain · dsp.c = input, pitch tracker, vocoder, fx ·
          voice.c = pitch engine, auto-tune, harmonizer, colour, doubler · loop.c = looper · ui.c = the display */
#ifndef VOX_H
#define VOX_H
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"

enum { P_INPUT, P_EQ, P_VOX, P_PITCH, P_FX, P_LOOP,              /* the areas on the colour keys K1-K6 */
       P_MIX, P_PRESETS, P_GLOBAL, P_COUNT };                      /* pages on F1, F2, F6 */
enum { VOX_OFF, VOX_TUNE, VOX_VOCODER, VOX_HARMONY, VOX_COUNT };   /* (TUNE: the auto-tune – in VOX since 24.10.) */
enum { CAR_SAW, CAR_PULSE, CAR_SUPER, CAR_IN2, CAR_COUNT };
enum { SC_CHROM, SC_MAJOR, SC_MINOR, SC_PENTA, SC_BLUES, SC_KEYS, SC_COUNT };
enum { HM_THIRD, HM_FIFTH, HM_TRIAD, HM_OCT_DOWN, HM_CHOIR, HM_KEYS, HM_COUNT };
enum { CO_OFF, CO_RADIO, CO_PHONE, CO_MEGA, CO_ROBOT, CO_LOFI, CO_COUNT };
enum { LP_EMPTY, LP_REC, LP_PLAY, LP_DUB, LP_STOP };

#define MAX_BANDS 24
#define MAX_VOICES 8
#define MAX_HARM 3                     /* harmony voices besides the lead */
#define PITCH_HIST 256                 /* one point per pitch hop (~10.7 ms) → ~2.7 s of curve */
#define LOOP_COLS 396                  /* (the old one-loop overview; the tracks use LT_COLS) */
#define NLT 4                          /* the looper's tracks (30.10.: four, side by side – the MPK's two rows of four pads) */
#define LT_COLS 100                    /* a track's overview in its strip */
#define N_PRESETS 16
#define FO_PRESET 200   /* (SDK 10 FOCUS: the PRESETS page's cells, 200 + program) */
#define FO_FADER 300    /* (FOCUS: the LOOP page's faders, 300 + track * 3 + VOL / DLY / REV) */
#define LOOP_OY 20      /* (LOOP: the strips start this far below the chip row – ui.c and the clicks in main.c) */
int  sub_count(int page);   /* main.c: a page's sub-pages (chips), 0 = none · which is open · names */
int  sub_now(int page);
const char *const *sub_names(int page);
#define FO_ROW 400      /* (FOCUS: the chip row – a page's sub-pages, 400 + chip: INPUT, EQ, VOX, LOOP) */
enum { CS_VOICE, CS_FET, CS_OPTO, CS_CLEAN, CS_COUNT };
enum { DT_DIGITAL, DT_ANALOG, DT_TAPE, DT_PINGPONG, DT_COUNT };   /* 1.6: the delay's kinds (FX › DELAY) – PING-PONG = what VOX had before */
enum { RT_HALL, RT_ROOM, RT_PLATE, RT_SPRING, RT_COUNT };         /* 1.6: the reverb's kinds (FX › REVERB) – HALL = before */
enum { MT_CHORUS, MT_FLANGER, MT_PHASER, MT_COUNT };              /* 1.6: FX › MOD (AMOUNT 0 = off) */   /* the compressor's kinds (KOMP-ART) */

/* what a preset sets – kept together so a pad can save and restore it in one go */
typedef struct {
  float vox_mode, bands, carrier, mix;                 /* VOX: vocoder */
  float harm, harm_width, harm_mix;                    /* VOX: harmonizer */
  float tune_speed, tune_amt;                          /* VOX > TUNE (key and scale stay: they belong to the song) */
  float pitch_st, formant_st, doubler, colour;         /* PITCH */
  float delay, delay_div, feedback, hall, room;        /* FX */
  float wet;                                           /* MIXER: 0 = only the voice, 1 = only the effects */
  float eq_cut, eq_low, eq_mid, eq_mid_f, eq_mid_q, eq_high;   /* EQ: low cut Hz (0 off), dB, dB, freq 0..1, Q, dB */
  float dly_type, rev_type, rev_pre, mod_type, mod_rate, mod_depth, mod_mix;   /* 1.6 (11.11.): FX sub-pages – appended: older programs are read and get these as before */
} vox_sound;
#define VOX_SOUND_OLD ((int)(sizeof(vox_sound) - 7 * sizeof(float)))   /* (a program before 1.6) */

typedef struct {
  float sr; double now;                /* sample clock */
  int page, shift, octave, input_on, dry;
  int sub_in, sub_eq, sub_fx;           /* (sub_fx, 1.6: FX › DELAY · REVERB · MOD) */
  double shift_t, shift_tap;           /* SHIFT went down · SHIFT tapped alone (the next pad within 1.5 s has it – an MPK's pads are silent while its SHIFT is held, as in VIBE DJ) */
  vox_sound s;                         /* the sound (presets) */
  /* INPUT */
  float in_mode, gain_db, gate_db, comp;
  float comp_style, deess, deess_f;    /* (26.10.) the compressor's kind (CS_*), the de-esser: amount 0..1, frequency Hz */
  /* TUNE */
  float tune_key, tune_scale;
  /* LOOP */
  float loop_bars, loop_keep, loop_level;
  /* global */
  float bpm, out_db, pad_mode;         /* pad_mode 0: held = punch-in, 1: always switch */
  int save_held, save_used, prog_user; /* prog_user: bit i = pad i holds the player's own sound */
  int hold, in2_live;                  /* in2_live: IN 2 carries its own signal (not a copy of a mono IN 1) */
  int preset;                          /* last preset chosen on the pads, -1 = none */
  /* meters (written by the audio thread, read by the display) */
  float in_peak, out_peak, gr_db, clip_until; int gate_open;
  float ds_db;                         /* the de-esser's reduction right now */
  float band_env[MAX_BANDS]; int band_n;
  float pitch_hist[PITCH_HIST], pitch_out_hist[PITCH_HIST]; int pitch_pos;   /* MIDI note, 0 = unvoiced */
  float pitch_now, pitch_out; int tune_target;
  int harm_notes[MAX_HARM]; int harm_n;                /* the harmony notes right now, for the display */
  float scope[512]; int scope_pos;
  /* looper: four tracks, and the chosen one mirrored into loop_state … (the header, the lights, the old buttons) */
  struct lt_state { int state, layers, undo, stop_at_end; float pos, len, level, send1, send2, peak; float over[LT_COLS]; } lt[NLT];   /* send1 = DELAY, send2 = HALL (post fader) */
  int loop_sel;                                        /* the track the buttons, the pedal and ALS WAV work on */
  float rec_fx;                                        /* 1.6: 1 = the looper records the voice WITH delay, reverb, MOD (as one hears it) · 0 = before them */
  int loop_row;                                        /* what encoders 1-4 set on LOOP: 0 PEGEL, 1 SEND 1 DELAY, 2 SEND 2 HALL, 3 LÄNGE · OVERDUB · LOOPS (K6 again = the next) */
  int loop_state, loop_layers, loop_undo;              /* loop_undo: 0 none, 1 undo possible, 2 redo possible */
  int loop_saving;                                     /* the loop as a WAV: 1 writing, 2 waiting for the host's WRITTEN */
  float loop_pos, loop_len;                            /* seconds */
  float loop_over[LOOP_COLS];
  char toast[40]; double toast_until;
  char toast_e[40]; double toast_e_until;               /* (1.5, two flows: the control flow's own toast – one writer each) */
} vox_state;
typedef struct lt_state lt_state;
extern vox_state V;
/* the auto-tune's strength as it acts: in VOX > TUNE and under HARMONY (its base note), else off */
static inline float tune_amount(void) { int m = (int)V.s.vox_mode; return m == VOX_TUNE || m == VOX_HARMONY ? V.s.tune_amt : 0; }

double v_ms(void);
void toast(const char *t);                             /* from the audio flow (every handler runs there since 1.5) */
void toast_e(const char *t);                           /* from the control flow (saving a WAV, the files' answers) */
/* 1.5: two flows (Protokoll 6.2) – what the audio flow may not do itself, it asks for; the draw does it (vox_jobs) */
extern volatile uint32_t want_input, want_input_mode, want_glb, want_prog, want_loop_save;
extern const char *const DLY_N[], *const REV_N[], *const MOD_N[];   /* (1.6: the FX kinds' names – main.c) */
static inline float mod_hz(float v) { return 0.05f * vd_pow(120.0f, v); }   /* MOD RATE 0..1 → 0.05..6 Hz */
void  global_write(void);                              /* (the draw) the globals to the store */
void  prog_write(void);                                /* (the draw) the programs to the store */

/* dsp.c */
void  dsp_init(float sr);
float input_stage(float x);                            /* gain, gate, compressor, meters */
void  pitch_feed(float x);                             /* YIN tracker, fills V.pitch_hist / V.pitch_now */
void  carrier_note(int note, int on);
void  carrier_release_all(void);
int   carrier_notes(int *out, int max);                /* the sounding (or held) notes, for the display */
float vocoder(float mod, float ext);                   /* one sample: voice in, vocoded out */
void  vocoder_meters(void);                            /* band levels → V.band_env (once per block) */
void  fx_voice(const float *vl, const float *vr, float *wl, float *wr, int n);   /* 1.6: the voice + MOD, delay, reverb (what REC FX records) */
void  fx_out(const float *wl, const float *wr, const float *ll, const float *lr, const float *s1, const float *s2, float *l, float *r, int n);   /* + the loops, their sends' own path, master */

/* voice.c */
void  voice_init(float sr);
void  tune_hop(float midi);                            /* once per pitch estimate: auto-tune + harmony targets */
int   pitch_engine_on(void);                           /* anything to shift? */
void  pitch_engine(float x, float *l, float *r);       /* one sample: lead + harmony voices (latency ~15 ms) */
void  colour(float *l, float *r);                      /* radio, phone, megaphone, robot, lo-fi */
void  doubler(float *l, float *r);

/* prog.c – the 16 programs on the pads and the global settings, kept with vibe_store_* */
void  prog_init(void);                                 /* load (or factory), then the global settings */
const vox_sound *prog_get(int i);
void  prog_store(int i, const vox_sound *s, int src);  /* the sound onto pad i (src: the program it came from, -1 own) */
const char *prog_name(int i);                          /* the name a pad shows */
uint32_t prog_colour(int i);
void  prog_reset(int i);                               /* pad i back to the factory sound */
void  global_save(void);

/* loop.c */
void  loop_init(float sr);
void  loop_button(int b);                              /* VB_PLAY, VB_REC, VB_STOP, VB_K3 (undo) */
void  loop_switch(int down);                          /* PLAY / the pedal: one switch, as the Ditto Looper */
void  loop_tick(void);                                /* (the audio) a held switch */
void  eq_update(void);                                /* dsp.c: the EQ's filters from the sound (once a block) */
float eq_response_db(float hz);                       /* dsp.c: the EQ's curve (the display) */
float eq_mid_hz(float pos);                           /* dsp.c: MID FREQ 0..1 → 200 Hz .. 8 kHz */
int   yin_step(void);                                 /* dsp.c: samples (at sr/2) between two pitch estimates */
void  loop_clear_all(void);                           /* SHIFT + REC: every loop deleted */
void  loop_stop_all(void);                            /* SHIFT + STOP */
void  loop_pad(int p, int down);                      /* the LOOP page's pads: 0-3 the tracks, 4-7 their STOP */
void  loop_track_switch(int i, int down);              /* 1.5: track i's one switch (Ditto) – pad 1-4, encoder push 1-4, REC / pedal */
void  loop_enc(int i, int down);                      /* 1.5: encoder push 1-4 on LOOP = that track's Ditto switch (as pad 1-4) */
void  loop_select(int i);                             /* SHIFT + pad: the track chosen, nothing else happens */
int   loop_master_len(float *sec);                    /* the first loop's length (the others are multiples) */
void  loop_process(const float *vl, const float *vr, float *ll, float *lr, float *s1, float *s2, int n);   /* records the voice, the loops out (and their sends) */
void  loop_save(void);                                 /* F5 on LOOP: the loop as a WAV into "VIBE FM/vox/" (SDK 13) */
void  loop_save_work(void);                            /* (draw / READY) the writing goes on */
void  loop_save_event(const vibe_event *e);            /* VIBE_EV_MEDIA: READY, WRITTEN */

/* ui.c */
void ui_draw(void);
void ui_lights(void);
extern const char *const PRESET_N[N_PRESETS];
extern const uint32_t PRESET_C[N_PRESETS];

/* main.c: the four encoders of the current page */
typedef struct { const char *label; char text[16]; float value; int bipolar; } v_param;
void params_get(v_param p[4]);

#endif
