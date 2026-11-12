/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* prog.c – the 16 programs (one per pad) and the global settings.
   A program is a whole vox_sound (TUNE strength/speed, VOX, PITCH, FX, dry/wet). Factory sounds fill the pads until
   the player saves their own over them (SAVE). Files (SDK 3 storage): "programs" = all 16, "global" = input, key and
   scale, looper, tempo, output, pad mode, last program. A file of another size (older VOX) is ignored. */
#include "vox.h"

const char *const PRESET_N[N_PRESETS] = {
  "NATURAL", "RADIO", "PHONE", "MEGAPHONE", "T-PAIN", "ROBOT", "DAFT PUNK", "KRAFTWERK",
  "CHOIR", "THIRD", "MONSTER", "CHIPMUNK", "ALIEN", "CATHEDRAL", "ECHO WALL", "LO-FI" };
const uint32_t PRESET_C[N_PRESETS] = {
  0x6EE7A8, 0xF5C93F, 0xF5C93F, 0xFF8A3D, 0x3FD0C9, 0x3FD0C9, 0xB38CFF, 0xB38CFF,
  0x5EA8FF, 0x5EA8FF, 0xFF5A4D, 0xFF5A4D, 0xFF5A4D, 0xE6EAF2, 0xE6EAF2, 0xF5C93F };

#define MAGIC 0x564F5831u                                       /* "VOX1" */
typedef struct { uint32_t magic, size, user; vox_sound s[N_PRESETS]; int8_t src[N_PRESETS]; } prg_file;   /* src: whose name */
typedef struct { uint32_t magic, size; float in_mode, gain_db, gate_db, comp, tune_key, tune_scale,
                 loop_bars, loop_keep, loop_level, bpm, out_db, pad_mode; int32_t last;
                 float comp_style, deess, deess_f;
                 float lt_level[NLT];
                 float lt_send1[NLT], lt_send2[NLT];
                 float rec_fx; } glb_file;   /* (rec_fx since 1.6) */   /* (comp_style … since 26.10., lt_level and the sends since 30.10.: older files are read without them) */
#define GLB_OLD4 ((uint32_t)(sizeof(glb_file) - sizeof(float)))          /* (before 1.6: no rec_fx) */
#define GLB_OLD3 ((uint32_t)(GLB_OLD4 - 2 * NLT * sizeof(float)))
#define GLB_OLD2 ((uint32_t)(GLB_OLD3 - NLT * sizeof(float)))
#define GLB_OLD ((uint32_t)(GLB_OLD2 - 3 * sizeof(float)))
static prg_file P;

static void neutral(vox_sound *s) {
  *s = (vox_sound){ .vox_mode = VOX_OFF, .bands = 16, .carrier = CAR_SUPER, .mix = 1,
                    .harm = HM_THIRD, .harm_width = 0.6f, .harm_mix = 0.7f,
                    .tune_speed = 0.35f, .tune_amt = 0,
                    .pitch_st = 0, .formant_st = 0, .doubler = 0, .colour = CO_OFF,
                    .delay = 0, .delay_div = 4, .feedback = 0.3f, .hall = 0, .room = 0.5f, .wet = 1,
                    .eq_cut = 0, .eq_low = 0, .eq_mid = 0, .eq_mid_f = 0.5f, .eq_mid_q = 1, .eq_high = 0,
                    .dly_type = DT_PINGPONG, .rev_type = RT_HALL, .rev_pre = 0, .mod_type = MT_CHORUS, .mod_rate = 0.3f, .mod_depth = 0.5f, .mod_mix = 0 };   /* (effects at 0 by default – Sven 24.10.) */
}

static void factory(int i, vox_sound *s) {
  neutral(s);
  switch (i) {
    case 0: break;
    case 1: s->colour = CO_RADIO; s->hall = 0.1f; s->delay = 0; break;
    case 2: s->colour = CO_PHONE; s->hall = 0; s->delay = 0; break;
    case 3: s->colour = CO_MEGA; s->delay = 0.25f; s->delay_div = 2; s->hall = 0.3f; break;
    case 4: s->vox_mode = VOX_TUNE; s->tune_amt = 1; s->tune_speed = 0; s->doubler = 0.3f; s->delay = 0.2f; s->delay_div = 2; s->hall = 0.3f; break;
    case 5: s->vox_mode = VOX_TUNE; s->tune_amt = 1; s->tune_speed = 0; s->colour = CO_ROBOT; s->hall = 0.15f; s->delay = 0; break;
    case 6: s->vox_mode = VOX_VOCODER; s->carrier = CAR_SUPER; s->delay = 0.2f; s->hall = 0.3f; break;
    case 7: s->vox_mode = VOX_VOCODER; s->carrier = CAR_PULSE; s->bands = 12; s->hall = 0.2f; s->delay = 0; break;
    case 8: s->vox_mode = VOX_HARMONY; s->harm = HM_CHOIR; s->harm_width = 0.9f; s->tune_amt = 0.5f; s->tune_speed = 0.3f; s->doubler = 0.4f; s->hall = 0.5f; s->room = 0.7f; break;
    case 9: s->vox_mode = VOX_HARMONY; s->harm = HM_THIRD; s->harm_width = 0.5f; s->hall = 0.3f; break;
    case 10: s->pitch_st = -7; s->formant_st = -5; s->hall = 0.3f; s->room = 0.7f; s->delay = 0; break;
    case 11: s->pitch_st = 9; s->formant_st = 6; s->delay = 0; break;
    case 12: s->pitch_st = -3; s->formant_st = 6; s->delay = 0.35f; s->delay_div = 1; s->feedback = 0.5f; s->doubler = 0.5f; break;
    case 13: s->hall = 0.9f; s->room = 1; s->doubler = 0.4f; s->delay = 0.1f; break;
    case 14: s->delay = 0.6f; s->delay_div = 3; s->feedback = 0.75f; s->hall = 0.4f; s->dly_type = DT_TAPE; break;   /* ECHO WALL: tape since 1.6 */
    case 15: s->colour = CO_LOFI; s->hall = 0.15f; s->delay = 0; break;
  }
}

static void write_programs(void) { VIBE_RELEASE(); want_prog++; }   /* (the audio flow: the draw writes them, prog_write) */
void prog_write(void) {
  P.magic = MAGIC; P.size = sizeof P;
  if (vibe_store_write("programs", &P, sizeof P)) toast_e("SAVE FAILED");
}

const vox_sound *prog_get(int i) { return &P.s[i < 0 || i >= N_PRESETS ? 0 : i]; }
const char *prog_name(int i) { int s = P.src[i]; return s >= 0 && s < N_PRESETS ? PRESET_N[s] : "OWN"; }
uint32_t prog_colour(int i) { int s = P.src[i]; return s >= 0 && s < N_PRESETS ? PRESET_C[s] : 0xE6EAF2; }
void prog_store(int i, const vox_sound *s, int src) { P.s[i] = *s; P.src[i] = (int8_t)(src >= 0 ? P.src[src] : -1); P.user |= 1u << i; V.prog_user = (int)P.user; write_programs(); }
void prog_reset(int i) { factory(i, &P.s[i]); P.src[i] = (int8_t)i; P.user &= ~(1u << i); V.prog_user = (int)P.user; write_programs(); }

void global_save(void) { VIBE_RELEASE(); want_glb++; }   /* (the audio flow: the draw writes it, global_write) */
void global_write(void) {
  glb_file g = { MAGIC, sizeof g, V.in_mode, V.gain_db, V.gate_db, V.comp, V.tune_key, V.tune_scale,
                 V.loop_bars, V.loop_keep, V.loop_level, V.bpm, V.out_db, V.pad_mode, V.preset,
                 V.comp_style, V.deess, V.deess_f, { V.lt[0].level, V.lt[1].level, V.lt[2].level, V.lt[3].level },
                 { V.lt[0].send1, V.lt[1].send1, V.lt[2].send1, V.lt[3].send1 }, { V.lt[0].send2, V.lt[1].send2, V.lt[2].send2, V.lt[3].send2 }, V.rec_fx };
  vibe_store_write("global", &g, sizeof g);
}

void prog_init(void) {
  int pn = vibe_store_read("programs", &P, sizeof P);
  const int OLD = 12 + N_PRESETS * VOX_SOUND_OLD + N_PRESETS;   /* (a file from before 1.6: the sounds without the FX sub-pages' values) */
  if (pn == OLD && P.magic == MAGIC && P.size == (uint32_t)OLD) {   /* read as it is, then spread out – the new values as VOX sounded before */
    static uint8_t raw[sizeof(prg_file)]; __builtin_memcpy(raw, &P, (size_t)OLD);
    vox_sound d; neutral(&d);
    for (int i = 0; i < N_PRESETS; i++) { vox_sound s = d; __builtin_memcpy(&s, raw + 12 + i * VOX_SOUND_OLD, (size_t)VOX_SOUND_OLD); P.s[i] = s; }
    __builtin_memcpy(P.src, raw + 12 + N_PRESETS * VOX_SOUND_OLD, N_PRESETS);
    P.size = sizeof P; pn = sizeof P;
  }
  if (pn != (int)sizeof P || P.magic != MAGIC || P.size != sizeof P) {
    for (int i = 0; i < N_PRESETS; i++) { factory(i, &P.s[i]); P.src[i] = (int8_t)i; }
    P.user = 0;
  }
  V.prog_user = (int)P.user;
  neutral(&V.s);                                 /* (the start: no effect – FX at 0, Sven 24.10.) */
  V.preset = -1;
  glb_file g = { 0 };
  int n = vibe_store_read("global", &g, sizeof g);
  if (g.magic == MAGIC && ((n == (int)sizeof g && g.size == sizeof g) || (n == (int)GLB_OLD4 && g.size == GLB_OLD4) || (n == (int)GLB_OLD3 && g.size == GLB_OLD3) || (n == (int)GLB_OLD2 && g.size == GLB_OLD2) || (n == (int)GLB_OLD && g.size == GLB_OLD))) {
    if (n >= (int)GLB_OLD2) { V.comp_style = g.comp_style; V.deess = g.deess; V.deess_f = g.deess_f; }
    if (n >= (int)GLB_OLD3) for (int i = 0; i < NLT; i++) V.lt[i].level = vd_clamp(g.lt_level[i], 0, 1);
    if (n == (int)sizeof g) V.rec_fx = g.rec_fx > 0.5f ? 1.0f : 0.0f;
    if (n >= (int)GLB_OLD4) for (int i = 0; i < NLT; i++) { V.lt[i].send1 = vd_clamp(g.lt_send1[i], 0, 1); V.lt[i].send2 = vd_clamp(g.lt_send2[i], 0, 1); }
    V.in_mode = g.in_mode; V.gain_db = g.gain_db; V.gate_db = g.gate_db; V.comp = g.comp;
    V.tune_key = g.tune_key; V.tune_scale = g.tune_scale;
    V.loop_bars = g.loop_bars; V.loop_keep = g.loop_keep; V.loop_level = g.loop_level;
    V.bpm = g.bpm; V.out_db = g.out_db; V.pad_mode = g.pad_mode;
    if (g.last >= 0 && g.last < N_PRESETS) { V.s = P.s[g.last]; V.preset = g.last; }
  }
  V.s.delay = 0; V.s.hall = 0;                   /* (also with the last program back: it starts dry, the effects up by hand) */
}
