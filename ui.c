/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). VOX, see LICENSE. */ /* ui.c – the VOX display (480 x 320) and the lights.
   Top: area, input mode, level, DRY/HOLD/looper, tempo. Left (x 12..408): what the page is about. Right: the six
   colour keys K1-K6 (the panel has no captions), the open area lit – as in GROOVE and OberVave.
   Row above the soft keys: the four encoder values. Bottom: F1-F6 (with SHIFT: DRY and UNDO). */
#include "vox.h"

#define BG 0x0B1220
#define CELL 0x18223A
#define CELL2 0x24314A
#define MUTED 0x5B6577
#define DIM 0x8A93A6
#define FG 0xE6EAF2
#define SEP 0x1F2A3D
#define SOFTBAR 0x131C2E
#define ACC 0x3FD0C9                  /* VOX teal */
#define GREEN 0x6EE7A8
#define YELLOW 0xF5C93F
#define RED 0xFF5A4D
#define PURPLE 0xB38CFF
#define ORANGE 0xFF8A3D
#define BLUE 0x3FA9F5
#define CW 468                        /* the pages' right edge: full width (28.10.: no colour-key column – the keys carry their names) */
#define MID ((12 + CW) / 2)               /* the pages' middle (full width since 28.10.) */

static const char *const PAGE_N[P_COUNT] = { "INPUT", "EQ", "VOX", "PITCH", "FX", "LOOP", "MIXER", "PRESETS", "GLOBAL" };
static const uint32_t KEY_C[6] = { GREEN, ACC, PURPLE, RED, BLUE, ORANGE };
static const char *const NOTE_N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static float lvl01(float lin) {                                   /* -60..0 dB → 0..1 */
  if (lin < 0.001f) return 0;
  return vd_clamp(1 + 6.0206f * vd_log2(lin) / 60, 0, 1);
}
static char *note_name(int n, char *b) {
  int i = 0; for (const char *s = NOTE_N[((n % 12) + 12) % 12]; *s; s++) b[i++] = *s;
  vg_itoa(n / 12 - 1, b + i); return b;
}
static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }
static int softx(int i) { return 36 + (int)(i * 81.7f); }
static int loop_shift(void) { return V.loop_row == 3; }   /* LOOP: the 4th row (LÄNGE · OVERDUB · LOOPS) shows the value row instead of the faders – SHIFT never flips it */
static uint32_t page_colour(void) { return V.page < 6 ? KEY_C[V.page] : ACC; }

static int hdr_ax, hdr_bx, hdr_nw;    /* the header's ◀ page ▶ (where they are: registered last, see ui_draw) */
static int page_prev(void) { return V.page < 6 ? (V.page + 5) % 6 : P_LOOP; }
static int page_next(void) { return V.page < 6 ? (V.page + 1) % 6 : P_INPUT; }
static void header(void) {
  char b[24];
  int w = vg_text("VOX", 12, 9, VG_MED, ACC, VG_LEFT);
  /* ◀ page ▶: the page in its key's colour (the only key in sight); the triangles and the name take clicks in the
     plugin's window and the browser (SDK 18) – the previous / the next area, as the colour keys do */
  hdr_ax = 24 + w;
  vg_tri(hdr_ax + 7, 11, hdr_ax + 7, 23, hdr_ax, 17, DIM);
  hdr_nw = vg_text(PAGE_N[V.page], hdr_ax + 15, 9, VG_MED, page_colour(), VG_LEFT);
  hdr_bx = hdr_ax + 15 + hdr_nw + 8;
  vg_tri(hdr_bx, 11, hdr_bx, 23, hdr_bx + 7, 17, DIM);
  static const char *const MODE_N[] = { "LINE", "MIC", "HI-Z", "USB" };
  vg_text(V.input_on ? MODE_N[(int)V.in_mode] : "IN OFF", 150, 12, VG_SMALL, V.input_on ? DIM : RED, VG_LEFT);
  vg_rect(200, 15, 70, 6, CELL);                                  /* small input meter */
  float l = lvl01(V.in_peak);
  vg_rect(200, 15, (int)(70 * l), 6, v_ms() < V.clip_until ? RED : l > 0.9f ? YELLOW : GREEN);
  int x = 280;
  if (V.dry) { vg_round(x, 9, 34, 17, 3, YELLOW); vg_text("DRY", x + 17, 12, VG_BOLD, 0x111214, VG_CENTER); x += 40; }
  if (V.hold) { vg_round(x, 9, 40, 17, 3, PURPLE); vg_text("HOLD", x + 20, 12, VG_BOLD, 0x111214, VG_CENTER); x += 46; }
  static const char *const LS[] = { "", "REC", "LOOP", "DUB", "STOP" };
  static const uint32_t LC[] = { 0, RED, GREEN, ORANGE, CELL2 };
  if (V.loop_state != LP_EMPTY && x < 320) { char ls[8] = ""; cat(ls, LS[V.loop_state]); cat(ls, " "); cat(ls, vg_itoa(V.loop_sel + 1, b)); vg_round(x, 9, 48, 17, 3, LC[V.loop_state]); vg_text(ls, x + 24, 12, VG_BOLD, V.loop_state == LP_STOP ? DIM : 0x111214, VG_CENTER); }
  { int pc = (int)(V.s.wet * 100 + 0.5f); char w[16] = "WET ", n[8];  /* DRY/WET, always in sight (encoder B) */
    cat(w, vg_itoa(pc, n)); cat(w, "%"); vg_text(w, 395, 12, VG_BOLD, V.dry || pc == 0 ? YELLOW : DIM, VG_RIGHT);
  }
  b[0] = 0; cat(b, vg_itoa((int)(V.bpm + 0.5f), b + 12)); vg_text(b, 440, 12, VG_SMALL, DIM, VG_RIGHT);
  vg_text("BPM", 444, 12, VG_SMALL, MUTED, VG_LEFT);
  vg_rect(10, 34, VIBE_SCREEN_W - 20, 1, SEP);
}

static void softkeys(void) {
  static const char *const F[6] = { "MIXER", "PRESETS", "PRG -", "PRG +", "SAVE", "GLOBAL" };
  int on[6] = { V.page == P_MIX, V.page == P_PRESETS, 0, 0, V.save_held, V.page == P_GLOBAL };
  vg_rect(0, 280, VIBE_SCREEN_W, 40, SOFTBAR);
  for (int i = 0; i < 6; i++) {
    const char *l = V.shift && i == 0 ? "DRY" : V.shift && i == 5 ? "UNDO" : V.shift && i == 4 ? "FACTORY" : i == 4 && V.page == P_LOOP ? "SAVE WAV" : F[i];
    int x = softx(i);
    vg_round(x - 35, 286, 70, 28, 4, on[i] ? ACC : CELL2); vf_key(VB_F1 + i, x - 35, 286, 70, 28);
    vg_text(l, x, 293, VG_BOLD, on[i] ? 0x111214 : FG, VG_CENTER);
  }
}

static void params_row(void) {
  v_param p[4]; params_get(p);
  uint32_t c = page_colour();
  for (int i = 0; i < 4; i++) {
    if (!p[i].label) continue;
    int x = 12 + i * 116; char n[2] = { (char)('1' + i), 0 };
    vf_encoder(VE_1 + i, x - 4, 198, 112, 66);
    vg_text(n, x, 202, VG_SMALL, MUTED, VG_LEFT);
    vg_text(p[i].label, x + 52, 202, VG_BOLD, DIM, VG_CENTER);
    vg_text(p[i].text, x + 52, 220, VG_BIG, 0xFFFFFF, VG_CENTER);
    vg_rect(x, 254, 104, 5, SEP);
    if (p[i].bipolar) { int m = x + 52, e = x + (int)(104 * p[i].value); vg_rect(e < m ? e : m, 254, (e < m ? m - e : e - m) + 1, 5, c); vg_rect(m, 251, 1, 11, DIM); }
    else vg_rect(x, 254, (int)(104 * p[i].value), 5, c);
  }
}

static void toast_draw(void) {                   /* (1.5: two writers, two slots – the newer one shows; a copy, never torn mid-draw) */
  double now = v_ms(), ua = V.toast_until, ue = V.toast_e_until;
  int use_e = V.toast_e[0] && now <= ue && (ue >= ua || !V.toast[0] || now > ua);
  if (!use_e && (!V.toast[0] || now > ua)) return;
  char t[40]; const char *src = use_e ? V.toast_e : V.toast; int i = 0; for (; src[i] && i < 39; i++) t[i] = src[i]; t[i] = 0;
  int w = vg_text_width(t, VG_BOLD) + 32;
  vg_round(240 - w / 2, 236, w, 30, 6, ACC);
  vg_text(t, 240, 243, VG_BOLD, 0x111214, VG_CENTER);
}

/* a horizontal level meter -60..0 dB */
static void meter(int y, const char *label, float lin, int clip, float mark_db) {
  int x = 92, w = 270;
  vg_text(label, 12, y + 2, VG_BOLD, DIM, VG_LEFT);
  vg_rect(x, y, w, 16, CELL);
  float l = lvl01(lin); int seg = (int)(w * l);
  int g = (int)(w * 0.8f), yl = (int)(w * 0.95f);                  /* -12 dB, -3 dB */
  vg_rect(x, y, seg < g ? seg : g, 16, GREEN);
  if (seg > g) vg_rect(x + g, y, (seg < yl ? seg : yl) - g, 16, YELLOW);
  if (seg > yl) vg_rect(x + yl, y, seg - yl, 16, RED);
  if (mark_db > -80) { int mx = x + (int)(w * vd_clamp(1 + mark_db / 60, 0, 1)); vg_rect(mx - 1, y - 3, 3, 22, FG); }
  if (clip) vg_text("CLIP", CW, y + 2, VG_BOLD, RED, VG_RIGHT);
}

/* the sub-pages of a page as chips (ZVAMPLER's second header line – Sven 01.11.: so one sees there are more): the open one
   filled in the page's colour; the colour key again = the next, a chip clicked (plugin, browser) = that one */
static void chips(uint32_t col) {
  int n = sub_count(V.page); if (!n) return;
  const char *const *names = sub_names(V.page); int cur = sub_now(V.page), cx = 12;
  for (int k = 0; k < n; k++) {
    int w = vg_text_width(names[k], VG_BOLD) + 20, on = k == cur;
    vg_round(cx, 38, w, 17, 8, on ? col : CELL); vf_button(FO_ROW + k, cx, 38, w, 17);
    vg_text(names[k], cx + w / 2, 41, VG_BOLD, on ? 0x111214 : DIM, VG_CENTER);
    cx += w + 6;
  }
  char b[16] = "K"; char t[4]; cat(b, vg_itoa(V.page + 1, t)); cat(b, " AGAIN = NEXT");
  vg_text(b, CW, 41, VG_SMALL, MUTED, VG_RIGHT);
}

static void page_input(void) {
  chips(GREEN);
  if (!V.input_on) {
    vg_round(12, 66, CW - 12, 116, 8, CELL);
    vg_text("INPUT OFF", MID, 80, VG_BIG, RED, VG_CENTER);
    vg_text("Press ENTER – the browser asks for the mic.", MID, 120, VG_SMALL, FG, VG_CENTER);
    vg_text("Mic into IN 1. Headphones, or it feeds back.", MID, 142, VG_SMALL, DIM, VG_CENTER);
    return;
  }
  meter(66, "INPUT", V.in_peak, v_ms() < V.clip_until, V.gate_db);
  for (int d = -60; d <= 0; d += 12) {
    char b[8]; int x = 92 + (int)(270 * (1 + d / 60.0f));
    vg_rect(x, 84, 1, 4, MUTED); vg_text(vg_itoa(d, b), x, 90, VG_SMALL, MUTED, VG_CENTER);
  }
  vg_text("GATE", 12, 118, VG_BOLD, DIM, VG_LEFT);
  int open = V.gate_open || V.gate_db <= -80;
  vg_round(92, 114, 70, 20, 4, V.gate_db <= -80 ? CELL2 : open ? GREEN : CELL2);
  vg_text(V.gate_db <= -80 ? "OFF" : open ? "OPEN" : "CLOSED", 127, 118, VG_BOLD, open && V.gate_db > -80 ? 0x111214 : DIM, VG_CENTER);
  vg_text("COMP", 176, 118, VG_BOLD, DIM, VG_LEFT);
  vg_rect(228, 116, 110, 16, CELL);
  int gr = (int)(110 * vd_clamp(V.gr_db / 20, 0, 1));
  vg_rect(338 - gr, 116, gr, 16, ORANGE);
  { char b[16] = "-"; char t[8]; cat(b, vg_ftoa(V.gr_db, 1, t)); cat(b, " dB"); vg_text(b, CW, 118, VG_SMALL, DIM, VG_RIGHT); }
  meter(150, "OUTPUT", V.out_peak, 0, -99);
  /* (26.10.) the de-esser: how far the sibilants go down right now (its kind and S-FREQ: on the SHIFT layer) */
  vg_text("DE-ESS", 12, 182, VG_BOLD, DIM, VG_LEFT);
  vg_rect(92, 180, 246, 16, CELL);
  int ds = (int)(246 * vd_clamp(V.ds_db / 12, 0, 1));
  vg_rect(338 - ds, 180, ds, 16, 0x3FD0C9);
  { char b[16] = "-"; char t[8]; cat(b, vg_ftoa(V.ds_db, 1, t)); cat(b, " dB"); vg_text(b, CW, 182, VG_SMALL, DIM, VG_RIGHT); }
}

/* the sung pitch over the last ~2.7 s; the window follows the voice */
static int scale_mask(void) {
  static const uint16_t B[SC_COUNT] = { 0xFFF, 0xAB5, 0x5AD, 0x295, 0x4E9, 0 };
  int sc = (int)V.tune_scale, key = (int)V.tune_key, m = 0;
  if (sc == SC_KEYS) { int n[MAX_VOICES], k = carrier_notes(n, MAX_VOICES); for (int i = 0; i < k; i++) m |= 1 << (n[i] % 12); return m ? m : 0xFFF; }
  for (int i = 0; i < 12; i++) if (B[sc] & (1 << i)) m |= 1 << ((i + key) % 12);
  return m;
}
static void curve(const float *hist, int x0, int y0, int w, int h, float lo, float ppn, float width, uint32_t col) {
  float px = 0, py = 0; int have = 0;
  for (int k = 0; k < PITCH_HIST; k++) {
    float p = hist[(V.pitch_pos + k) % PITCH_HIST];
    float x = x0 + k * (w / (float)(PITCH_HIST - 1)), y = y0 + h - (p - lo) * ppn;
    if (p <= 0 || y < y0 || y > y0 + h) { have = 0; continue; }
    if (have) vg_line(px, py, x, y, width, col); else vg_rect((int)x - 1, (int)y - 1, 3, 3, col);
    px = x; py = y; have = 1;
  }
}
static void pitch_curve(int x0, int y0, int w, int h) {
  static float center = 57;
  float sum = 0; int cnt = 0;
  for (int k = 0; k < 64; k++) { float p = V.pitch_hist[(V.pitch_pos - 1 - k + PITCH_HIST) % PITCH_HIST]; if (p > 0) { sum += p; cnt++; } }
  if (cnt > 4) center += (sum / cnt - center) * 0.08f;
  float lo = center - 12, ppn = h / 24.0f;
  vg_rect(x0, y0, w, h, 0x0E1628);
  int mask = scale_mask(), tuned = tune_amount() > 0.001f;
  for (int n = (int)lo; n <= (int)lo + 25; n++) {
    float y = y0 + h - (n - lo) * ppn; if (y < y0 || y > y0 + h) continue;
    int c = ((n % 12) + 12) % 12;
    vg_rect(x0, (int)y, w, 1, tuned && (mask & (1 << c)) ? 0x22406A : c == 0 ? CELL2 : 0x131D31);
    if (c == 0 || c == 7) { char b[8]; vg_text(note_name(n, b), x0 - 6, (int)y - 7, VG_SMALL, c == 0 ? DIM : MUTED, VG_RIGHT); }
  }
  int shifted = tune_amount() > 0.001f || vd_absf(V.s.pitch_st) > 0.01f;
  curve(V.pitch_hist, x0, y0, w, h, lo, ppn, shifted ? 1.4f : 2.2f, shifted ? MUTED : ACC);
  if (shifted && !V.dry) curve(V.pitch_out_hist, x0, y0, w, h, lo, ppn, 2.2f, ACC);
}

/* EQ: the curve over 20 Hz .. 20 kHz (log), ±15 dB, the bell's frequency marked */
static void page_eq(void) {
  chips(ACC);
  int x0 = 44, y0 = 62, w = CW - 52, h = 112;
  vg_rect(x0, y0, w, h, 0x0E1628);
  static const int GRID[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
  static const char *const GN[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
  for (int i = 0; i < 8; i++) {
    int x = x0 + (int)(w * vd_log2(GRID[i] / 20.0f) / 9.9658f);
    vg_rect(x, y0, 1, h, 0x131D31); vg_text(GN[i], x, y0 + h + 3, VG_SMALL, MUTED, VG_CENTER);
  }
  for (int d = -12; d <= 12; d += 6) {
    int y = y0 + h / 2 - d * h / 30;
    vg_rect(x0, y, w, 1, d ? 0x131D31 : CELL2);
    char b[8]; vg_text(d ? vg_itoa(d, b) : "0", x0 - 6, y - 7, VG_SMALL, MUTED, VG_RIGHT);
  }
  float px = 0, py = 0;
  for (int k = 0; k <= w; k += 2) {
    float hz = 20.0f * vd_exp2(9.9658f * k / w), db = vd_clamp(eq_response_db(hz), -15, 15);
    float x = x0 + k, y = y0 + h / 2 - db * h / 30;
    if (k) vg_line(px, py, x, y, 2.0f, ACC);
    px = x; py = y;
  }
  if (vd_absf(V.s.eq_mid) > 0.05f || V.shift || V.sub_eq) {
    int x = x0 + (int)(w * vd_log2(eq_mid_hz(V.s.eq_mid_f) / 20.0f) / 9.9658f);
    vg_rect(x, y0, 1, h, YELLOW);
  }
}

static void page_tune(void) {
  pitch_curve(44, 58, CW - 44, 122);
  char b[24] = ""; char t[8];
  if (V.pitch_now > 0) {
    int n = (int)(V.pitch_now + 0.5f), ct = (int)((V.pitch_now - n) * 100);
    note_name(n, b); cat(b, " "); if (ct >= 0) cat(b, "+"); cat(b, vg_itoa(ct, t)); cat(b, "ct");
    vg_round(276, 62, 128, 30, 5, 0x0B1220);
    vg_text(b, 340, 66, VG_BIG, ct > 15 || ct < -15 ? YELLOW : GREEN, VG_CENTER);
    if (V.tune_target > 0) { char nb[8]; b[0] = 0; cat(b, "→ "); cat(b, note_name(V.tune_target, nb)); vg_text(b, 340, 96, VG_MED, ACC, VG_CENTER); }
  }
  vg_text(tune_amount() > 0.001f ? "grey = sung · teal = corrected · blue = scale"
                                : "How you sing. Turn AMOUNT up = auto-tune (100 % = flat)", MID, 186, VG_SMALL, MUTED, VG_CENTER);
}

static void note_box(int x, int y, int w, const char *label, int note, uint32_t col) {
  char b[8];
  vg_round(x, y, w, 64, 6, CELL);
  vg_text(label, x + w / 2, y + 6, VG_SMALL, DIM, VG_CENTER);
  vg_text(note > 0 ? note_name(note, b) : "–", x + w / 2, y + 26, VG_BIG, note > 0 ? col : MUTED, VG_CENTER);
}

static void page_harmony(void) {
  int sung = V.pitch_now > 0 ? (int)(V.pitch_now + 0.5f) : 0;
  note_box(12, 62, 92, "VOICE", V.dry ? 0 : sung, FG);
  int n = V.dry ? 0 : V.harm_n;
  for (int i = 0; i < MAX_HARM; i++) note_box(112 + i * 100, 62, 92, i == 0 ? "HARMONY" : "", i < n ? V.harm_notes[i] : 0, i == 0 ? ACC : i == 1 ? PURPLE : 0x5EA8FF);
  const char *hint = V.dry ? "DRY is on (SHIFT + MIXER)"
    : (int)V.s.harm == HM_KEYS ? "hold keys = these notes sing along"
    : "follows KEY + SCALE (VOX > TUNE) – chrom. = major";
  vg_text(hint, MID, 142, VG_SMALL, MUTED, VG_CENTER);
  char b[48] = ""; char t[8];
  cat(b, "Key "); cat(b, NOTE_N[(int)V.tune_key]); cat(b, "  ·  Auto-Tune "); cat(b, tune_amount() > 0.001f ? vg_itoa((int)(tune_amount() * 100 + 0.5f), t) : "off");
  if (tune_amount() > 0.001f) cat(b, " %");
  vg_text(b, MID, 164, VG_SMALL, DIM, VG_CENTER);
}

static void page_vox(void) {
  chips(PURPLE);
  if ((int)V.s.vox_mode == VOX_TUNE) { page_tune(); return; }
  if ((int)V.s.vox_mode == VOX_HARMONY) { page_harmony(); return; }
  if ((int)V.s.vox_mode == VOX_OFF) {                              /* what this area does */
    vg_round(12, 60, CW - 12, 58, 6, CELL);
    vg_text("VOCODER", 22, 64, VG_MED, PURPLE, VG_LEFT);
    vg_text("Your voice speaks through a synth:", 22, 84, VG_SMALL, FG, VG_LEFT);
    vg_text("a chord on the keys = Daft Punk, Kraftwerk.", 22, 100, VG_SMALL, DIM, VG_LEFT);
    vg_round(12, 124, CW - 12, 58, 6, CELL);
    vg_text("HARMONY", 22, 128, VG_MED, 0x5EA8FF, VG_LEFT);
    vg_text("Up to 3 extra voices sing along:", 22, 148, VG_SMALL, FG, VG_LEFT);
    vg_text("third, fifth, choir – in key and scale.", 22, 164, VG_SMALL, DIM, VG_LEFT);
    vg_text("K3 again or encoder 1 = the mode", MID, 186, VG_SMALL, MUTED, VG_CENTER);
    return;
  }
  int n = V.band_n ? V.band_n : (int)V.s.bands, x0 = 12, w = CW - 12, y0 = 60, h = 92;
  float bw = w / (float)n;
  vg_rect(x0, y0, w, h, 0x0E1628);
  int on = !V.dry;
  for (int i = 0; i < n; i++) {
    float v = on ? lvl01(V.band_env[i] * 4) : 0;
    int bh = (int)(h * v), x = x0 + (int)(i * bw) + 2;
    if (bh > 0) vg_rect(x, y0 + h - bh, (int)bw - 4, bh, vg_mix(ACC, PURPLE, i / (float)n));
  }
  if (!on) vg_text("DRY is on (SHIFT + MIXER)", MID, 98, VG_BOLD, DIM, VG_CENTER);
  char b[64] = ""; char t[8];
  int notes[MAX_VOICES], k = carrier_notes(notes, MAX_VOICES);
  vg_text("CARRIER", 12, 166, VG_BOLD, DIM, VG_LEFT);
  if ((int)V.s.carrier == CAR_IN2) cat(b, V.in2_live ? "IN 2 – a synth on the second input" : "IN 2 empty (mono mic) – choose SUPER");
  else if (!k) cat(b, "keys = the vocoder's chord");
  else for (int i = 0; i < k; i++) { if (i) cat(b, " "); cat(b, note_name(notes[i], t)); }
  vg_text(b, 72, 166, k ? VG_BOLD : VG_SMALL, k ? FG : MUTED, VG_LEFT);
  b[0] = 0; cat(b, "OCT "); if (V.octave > 0) cat(b, "+"); cat(b, vg_itoa(V.octave, t)); cat(b, "  ·  ENTER = HOLD");
  vg_text(b, CW, 184, VG_SMALL, MUTED, VG_RIGHT);
}

/* PITCH: what comes out of the voice */
static void page_pitch(void) {
  int sung = V.pitch_now > 0 ? (int)(V.pitch_now + 0.5f) : 0, out = V.pitch_out > 0 && !V.dry ? (int)(V.pitch_out + 0.5f) : 0;
  note_box(12, 46, 120, "VOICE", sung, FG);
  vg_text("→", 146, 66, VG_BIG, MUTED, VG_LEFT);
  note_box(176, 46, 120, "OUT", out, RED);
  int f = (int)(V.s.formant_st < 0 ? V.s.formant_st - 0.5f : V.s.formant_st + 0.5f);
  vg_text("FORMANT", 308, 50, VG_SMALL, DIM, VG_LEFT);
  vg_text(f == 0 ? "NATURAL" : f > 0 ? "BRIGHTER" : "DARKER", 308, 66, VG_BOLD, f ? PURPLE : MUTED, VG_LEFT);
  vg_text("COLOR", 308, 86, VG_SMALL, DIM, VG_LEFT);
  static const char *const COL_N[] = { "OFF", "RADIO", "PHONE", "MEGAPHONE", "ROBOT", "LO-FI" };
  vg_text(COL_N[(int)V.s.colour], 308, 102, VG_BOLD, (int)V.s.colour ? YELLOW : MUTED, VG_LEFT);
  /* doubler as two little voices left and right */
  vg_text("DOUBLER", 12, 126, VG_SMALL, DIM, VG_LEFT);
  vg_rect(80, 130, CW - 80, 6, CELL);
  vg_rect(80, 130, (int)((CW - 80) * V.s.doubler), 6, RED);
  vg_text("pitch +/-12 · formant +/-8: monster, chipmunk", MID, 152, VG_SMALL, MUTED, VG_CENTER);
  vg_text("presets for it: F2 PRESETS or the pads", MID, 170, VG_SMALL, MUTED, VG_CENTER);
}

static void page_fx(void) {                      /* 1.6: chips DELAY · REVERB · MOD, the open one's kinds, the scope */
  chips(BLUE);
  { const char *const *nm = V.sub_fx == 1 ? REV_N : V.sub_fx == 2 ? MOD_N : DLY_N;
    int cnt = V.sub_fx == 1 ? RT_COUNT : V.sub_fx == 2 ? MT_COUNT : DT_COUNT;
    int cur = (int)((V.sub_fx == 1 ? V.s.rev_type : V.sub_fx == 2 ? V.s.mod_type : V.s.dly_type) + 0.5f);
    float amt = V.sub_fx == 1 ? V.s.hall : V.sub_fx == 2 ? V.s.mod_mix : V.s.delay;
    int bw = (CW - 12 - (cnt - 1) * 6) / cnt;
    for (int k = 0; k < cnt; k++) {
      int bx = 12 + k * (bw + 6), on = k == cur;
      vg_round(bx, 62, bw, 24, 6, on ? (amt > 0.001f ? BLUE : vg_mix(CELL, BLUE, 0.45f)) : CELL);
      vg_text(nm[k], bx + bw / 2, 67, VG_BOLD, on ? 0x111214 : DIM, VG_CENTER);
    }
  }
  int x0 = 12, y0 = 92, w = CW - 12, h = 66;
  vg_rect(x0, y0, w, h, 0x0E1628);
  int start = (V.scope_pos + 1) & 511;
  float px = x0, py = y0 + h / 2;
  for (int k = 0; k < w; k++) {
    float v = V.scope[(start + k) & 511], x = x0 + k, y = y0 + h / 2 - vd_clamp(v, -1, 1) * h * 0.48f;
    if (k) vg_line(px, py, x, y, 1.2f, BLUE);
    px = x; py = y;
  }
  char b[64] = ""; char t[8];
  cat(b, "A tempo "); cat(b, vg_itoa((int)(V.bpm + 0.5f), t));
  cat(b, " · B dry/wet "); cat(b, vg_itoa((int)(V.s.wet * 100 + 0.5f), t)); cat(b, " %");
  cat(b, " · DATA program");
  vg_text(b, MID, 168, VG_SMALL, DIM, VG_CENTER);
}

/* LOOP: four tracks as four channel strips, the way a loop station looks – per track the square button (its state, a
   bar running along with the loop), the STOP field (blinks while the track waits for its loop's end), the loop as a
   small waveform with the play head, then three vertical faders: PEGEL, SEND 1 DELAY, SEND 2 HALL (the row the
   encoders set is lit – K6 again = the next row) and a meter beside the level. */
static void vfader(int x, int y, int w, int h, float v, uint32_t col, int lit) {
  vg_rect(x + w / 2 - 2, y, 4, h, CELL);                                      /* the slot */
  int hy = y + h - (int)(h * vd_clamp(v, 0, 1));
  vg_rect(x + w / 2 - 2, hy, 4, y + h - hy, lit ? col : vg_mix(CELL, col, 0.45f));
  vg_round(x, hy - 4, w, 8, 2, lit ? FG : DIM);                                /* the cap */
}
static void page_loop(void) {
  static const char *const LS[] = { "EMPTY", "REC", "PLAY", "DUB", "STOP" };
  static const uint32_t LC[] = { CELL, RED, GREEN, ORANGE, CELL2 };
  static const char *const RL[3] = { "VOL", "DLY", "REV" };
  int blink = (int)(v_ms() / 250) & 1, row = V.loop_row < 0 || V.loop_row > 2 ? -1 : V.loop_row;
  float msec = 0; int have_master = loop_master_len(&msec);
  chips(ORANGE);
  const int oy = LOOP_OY;                                                     /* (the strips below the chips) */
  for (int i = 0; i < NLT; i++) {
    const lt_state *t = &V.lt[i]; int st = t->state, on = i == V.loop_sel;
    int x = 12 + i * 116, cx = x + 52;
    if (on) vg_round(x - 4, 38 + oy, 112, 242 - 38 - oy, 6, 0x111A2E);        /* the chosen strip: a darker field */
    /* the square button */
    uint32_t fill = LC[st], ink = 0x111214; const char *word = LS[st];
    int waiting = st == LP_REC && t->len <= 0 && t->pos <= 0;                   /* (REC without a frame yet: waits for the head) */
    if (st == LP_REC) { fill = blink ? RED : 0x7A2A26; if (waiting) word = "WAIT"; }
    if (st == LP_DUB && blink) fill = vg_mix(ORANGE, RED, 0.5f);
    if (st == LP_EMPTY || st == LP_STOP) ink = DIM;
    if (on) vg_round(x + 2, 40 + oy, 100, 50, 7, FG);
    vg_round(x + 4, 42 + oy, 96, 46, 6, fill);
    vg_text(word, cx, 49 + oy, VG_BOLD, ink, VG_CENTER);
    char b[32] = "", n[8];
    if (st == LP_REC) { if (!waiting) { cat(b, vg_ftoa(t->pos, 1, n)); cat(b, " s"); } else cat(b, "from top"); }
    else if (st != LP_EMPTY) {
      cat(b, vg_ftoa(t->len, 1, n)); cat(b, " s");
      if (have_master && t->len > msec * 1.5f) { cat(b, " x"); cat(b, vg_itoa((int)(t->len / msec + 0.5f), n)); }
    } else cat(b, "pad = REC");
    vg_text(b, cx, 68 + oy, VG_SMALL, ink, VG_CENTER);
    if ((st == LP_PLAY || st == LP_DUB) && t->len > 0)                          /* the bar runs along with the loop */
      vg_rect(x + 8, 83 + oy, (int)(88 * vd_clamp(t->pos / t->len, 0, 1)), 3, 0x111214);
    vf_button(FO_PRESET + i, x + 4, 42 + oy, 96, 46);
    /* the STOP field (pad 5-8): blinks while it waits for the loop's end, lit while it stands */
    int pend = t->stop_at_end && st == LP_PLAY;
    uint32_t sc = pend ? (blink ? RED : CELL2) : st == LP_STOP ? vg_mix(CELL2, RED, 0.5f) : st == LP_EMPTY ? CELL : CELL2;
    vg_round(x + 4, 92 + oy, 96, 16, 4, sc);
    vg_rect(cx - 4, 96 + oy, 8, 8, pend || st == LP_STOP ? FG : st == LP_EMPTY ? MUTED : DIM);
    /* the loop as a small waveform, the play head · a recording: how far */
    int y0 = 112 + oy, h = 26;
    vg_rect(x + 2, y0, LT_COLS, h, 0x0E1628);
    if (st == LP_REC) {
      float frac = waiting ? 0 : t->len > 0 ? vd_minf(t->pos / t->len, 1) : vd_minf(t->pos / 24, 1);
      vg_rect(x + 2, y0, (int)(LT_COLS * frac), h, 0x3A1A22);
    } else if (st != LP_EMPTY) {
      for (int c = 0; c < LT_COLS; c++) {
        int a = (int)(vd_clamp(t->over[c], 0, 1) * (h / 2 - 1));
        if (a > 0) vg_rect(x + 2 + c, y0 + h / 2 - a, 1, 2 * a, st == LP_DUB ? vg_mix(ORANGE, ACC, 0.4f) : st == LP_STOP ? MUTED : ORANGE);
      }
      if (t->len > 0 && st != LP_STOP) { int px = x + 2 + (int)(LT_COLS * t->pos / t->len); vg_rect(px, y0, 2, h, FG); }
    }
    if (loop_shift()) continue;                                                 /* (row LOOP: the global values in the row below) */
    /* three vertical faders: PEGEL (with a meter), DELAY, HALL */
    int fy = 150 + oy, fh = 84 - oy;
    float val[3] = { t->level, t->send1, t->send2 };
    static const int FX[3] = { 6, 46, 76 };
    static const uint32_t FC[3] = { ORANGE, BLUE, PURPLE };
    for (int k = 0; k < 3; k++) {
      int lit = k == row;
      vfader(x + FX[k], fy, 22, fh, val[k], FC[k], lit);
      { lt_state *w = &V.lt[i]; float *pv = k == 0 ? &w->level : k == 1 ? &w->send1 : &w->send2;   /* (FOCUS: each fader on its own) */
        vf_value(FO_FADER + i * 3 + k, x + FX[k] - 2, fy - 18, 26, fh + 36, pv, 0, 1, 0.02f); }
      char vb[8]; vg_itoa((int)(val[k] * 100 + 0.5f), vb);
      vg_text(vb, x + FX[k] + 11, fy - 16, VG_SMALL, lit ? FG : MUTED, VG_CENTER);
      vg_text(RL[k], x + FX[k] + 11, fy + fh + 6, VG_SMALL, lit ? FC[k] : MUTED, VG_CENTER);
    }
    float m = t->peak > 0.001f ? vd_clamp(1 + 6.0206f * vd_log2(t->peak) / 48, 0, 1) : 0;   /* the meter: 48 dB */
    int mh = (int)(fh * m);
    vg_rect(x + 32, fy, 5, fh, CELL);
    vg_rect(x + 32, fy + fh - mh, 5, mh, t->peak > 0.9f ? RED : t->peak > 0.5f ? YELLOW : GREEN);
  }
  {
    char b[64];
    if (V.shift || (V.shift_tap > 0 && v_ms() - V.shift_tap < 1500)) { b[0] = 0; cat(b, "PAD 1-8 = SELECT TRACK"); vg_text(b, MID, 262, VG_SMALL, YELLOW, VG_CENTER); return; }
    if (loop_shift()) return;
    vg_text("SHIFT + PAD = SELECT TRACK", MID, 262, VG_SMALL, MUTED, VG_CENTER);   /* (the row: the chips above) */
  }
}

/* MIXER: voice against effects, what is on, the levels */
static void page_mix(void) {
  int x0 = 12, w = CW - 12, y = 46;
  vg_text("VOICE", x0, y, VG_BOLD, FG, VG_LEFT);
  vg_text("EFFECT", CW, y, VG_BOLD, ACC, VG_RIGHT);
  vg_rect(x0, y + 20, w, 8, CELL);
  vg_rect(x0, y + 20, (int)(w * V.s.wet), 8, vg_mix(FG, ACC, V.s.wet));
  int kx = x0 + (int)(w * V.s.wet); vg_rect(kx - 2, y + 16, 5, 16, FG);
  /* what the effect side holds right now */
  char b[96] = ""; char t[8]; int any = 0;
  if (tune_amount() > 0.001f) { cat(b, "AUTO-TUNE "); any = 1; }
  if (vd_absf(V.s.pitch_st) > 0.01f) { cat(b, "PITCH "); if (V.s.pitch_st > 0) cat(b, "+"); cat(b, vg_itoa((int)V.s.pitch_st, t)); cat(b, " "); any = 1; }
  if (vd_absf(V.s.formant_st) > 0.01f) { cat(b, "FORMANT "); any = 1; }
  if ((int)V.s.vox_mode == VOX_VOCODER) { cat(b, "VOCODER "); any = 1; }
  if ((int)V.s.vox_mode == VOX_HARMONY) { cat(b, "HARMONY "); any = 1; }
  if ((int)V.s.colour) { static const char *const C[] = { "", "RADIO ", "PHONE ", "MEGAPHONE ", "ROBOT ", "LO-FI " }; cat(b, C[(int)V.s.colour]); any = 1; }
  if (V.s.doubler > 0.01f) { cat(b, "DOUBLER "); any = 1; }
  vg_text("IN THE EFFECT:", x0, 90, VG_SMALL, DIM, VG_LEFT);
  vg_text(V.dry ? "nothing – DRY is on" : any ? b : "nothing – choose a pad or an area", 118, 90, VG_SMALL, any && !V.dry ? FG : MUTED, VG_LEFT);
  meter(116, "INPUT", V.in_peak, v_ms() < V.clip_until, -99);
  meter(140, "OUTPUT", V.out_peak, 0, -99);
  vg_text("SHIFT + MIXER = DRY · DATA = program", MID, 170, VG_SMALL, MUTED, VG_CENTER);
}

/* PRESETS: the 16 programs, laid out like the pads */
static void page_presets(void) {
  for (int i = 0; i < N_PRESETS; i++) {
    int cx = i % 4, cy = i / 4, x = 12 + cx * 100, y = 44 + cy * 30, on = V.preset == i;
    uint32_t c = prog_colour(i);
    vg_round(x, y, 94, 26, 4, on ? c : CELL); vf_button(FO_PRESET + i, x, y, 94, 26);
    vg_rect(x + 5, y + 9, 4, 8, on ? 0x111214 : c);
    vg_text(prog_name(i), x + 50, y + 7, VG_BOLD, on ? 0x111214 : FG, VG_CENTER);
    if (V.prog_user & (1 << i)) vg_circle(x + 87, y + 6, 2.5f, on ? 0x111214 : YELLOW);   /* your own sound */
  }
  vg_text("tap a pad = it stays · hold = punch-in", MID, 166, VG_SMALL, DIM, VG_CENTER);
  vg_text("hold SAVE + pad = save there · dot = your own", MID, 182, VG_SMALL, MUTED, VG_CENTER);
}

static void page_global(void) {
  vg_round(12, 44, CW - 12, 120, 6, CELL);
  vg_text("GLOBAL – for all programs", 22, 52, VG_BOLD, FG, VG_LEFT);
  vg_text("input, key + scale, looper, tempo, output", 22, 76, VG_SMALL, DIM, VG_LEFT);
  vg_text("are saved when you leave this page and with SAVE.", 22, 92, VG_SMALL, DIM, VG_LEFT);
  vg_text("Programs: SAVE (F5).", 22, 108, VG_SMALL, DIM, VG_LEFT);
  vg_text("PADS PUNCH-IN: hold = only while held", 22, 132, VG_SMALL, MUTED, VG_LEFT);
  vg_text("SHIFT + SAVE = the pad back to its factory sound", 22, 148, VG_SMALL, MUTED, VG_LEFT);
}

void ui_draw(void) {
  vg_clear(BG);
  vf_begin();
  header();
  switch (V.page) {
    case P_INPUT: page_input(); break;
    case P_EQ: page_eq(); break;
    case P_VOX: page_vox(); break;
    case P_PITCH: page_pitch(); break;
    case P_FX: page_fx(); break;
    case P_LOOP: page_loop(); break;
    case P_MIX: page_mix(); break;
    case P_PRESETS: page_presets(); break;
    case P_GLOBAL: page_global(); break;
  }
  if (V.page != P_LOOP || loop_shift()) params_row();   /* (LOOP: the strips' vertical faders are the encoders) */
  toast_draw();
  softkeys();
  /* (FOCUS: the header's controls last – the frame starts on the page's first control) */
  vf_encoder(VE_B, 330, 7, 68, 22); vf_encoder(VE_A, 402, 7, 70, 22);            /* DRY/WET = encoder B, the tempo = A */
  vf_key(VB_K1 + page_prev(), hdr_ax - 4, 6, 15, 24);                            /* ◀ = the previous page */
  vf_key(VB_K1 + page_next(), hdr_ax + 13, 6, hdr_nw + 18, 24);                   /* the name and ▶ = the next page */
  vf_end(ACC);
}

void ui_lights(void) {
  for (int i = 0; i < 6; i++) vibe_led_button(VB_K1 + i, KEY_C[i], V.page == i ? 1.0f : 0.16f);
  vibe_led_button(VB_F1, V.dry ? YELLOW : ACC, V.page == P_MIX || V.dry ? 1 : 0.15f);
  vibe_led_button(VB_F2, ACC, V.page == P_PRESETS ? 1 : 0.15f);
  vibe_led_button(VB_F3, ACC, 0.15f); vibe_led_button(VB_F4, ACC, 0.15f);
  vibe_led_button(VB_F5, RED, V.save_held || V.loop_saving ? 1 : 0.15f);
  vibe_label(VB_F5, V.page == P_LOOP ? "SAVE WAV" : "SAVE");
  vibe_led_button(VB_F6, ACC, V.page == P_GLOBAL ? 1 : V.loop_undo ? 0.5f : 0.15f);
  vibe_led_button(VB_SHIFT, ACC, V.shift ? 1 : 0);
  vibe_led_button(VB_OCT_DOWN, ACC, V.octave < 0 ? 1 : 0);
  vibe_led_button(VB_OCT_UP, ACC, V.octave > 0 ? 1 : 0);
  vibe_led_button(VB_ENTER, V.input_on ? PURPLE : RED, V.input_on ? (V.hold ? 1 : 0.15f) : ((int)(v_ms() / 400) & 1));
  int st = V.loop_state, blink = (int)(v_ms() / 250) & 1;
  if (V.page == P_LOOP) {                      /* the pads are the tracks: 1-4 their state, 5-8 STOP (lit while stopped) */
    static const uint32_t TC[] = { CELL2, RED, GREEN, ORANGE, GREEN };
    for (int i = 0; i < NLT; i++) {
      int s = V.lt[i].state;
      vibe_led_pad(i, TC[s], s == LP_REC ? (V.lt[i].len <= 0 && V.lt[i].pos <= 0 ? (blink ? 1.0f : 0.2f) : 1.0f) : s == LP_PLAY || s == LP_DUB ? 1.0f : s == LP_STOP ? 0.3f : 0.05f);
      int pend = V.lt[i].stop_at_end && s == LP_PLAY;
      vibe_led_pad(NLT + i, 0xFF5A4D, pend ? (blink ? 1.0f : 0.2f) : s == LP_STOP ? 0.8f : s == LP_EMPTY ? 0.03f : 0.15f);
    }
    if (V.shift_tap > 0 && v_ms() - V.shift_tap < 1500) for (int i = 0; i < 2 * NLT; i++) vibe_led_pad(i, i % NLT == V.loop_sel ? 0xFFFFFF : 0xF5C93F, blink ? 0.8f : 0.3f);
    for (int i = 2 * NLT; i < N_PRESETS; i++) vibe_led_pad(i, 0x5B6577, 0.03f);
  } else for (int i = 0; i < N_PRESETS; i++) vibe_led_pad(i, prog_colour(i), V.preset == i ? 1.0f : 0.12f);
  vibe_led_button(VB_REC, RED, st == LP_REC ? 1 : st == LP_DUB ? (blink ? 1 : 0.4f) : 0.1f);
  vibe_led_button(VB_PLAY, GREEN, st == LP_PLAY || st == LP_DUB ? 1 : st == LP_STOP ? 0.3f : 0.1f);
  vibe_led_button(VB_STOP, FG, st == LP_STOP ? 1 : 0.1f);
}
