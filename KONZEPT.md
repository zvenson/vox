# VOX – Stimmprozessor + Looper für VIBE (Konzept, 19.10.2026)

Ziel: ein Effekt, der mit Stimme sofort Spaß macht und dabei den **Audio-Eingang** richtig testet
(Pegel, Latenz, CPU, Mic/Hi-Z/Line). SDK-Grundlage: `vibe_on_audio(in, …)` = IN 1, `vibe_input(1)` = IN 2,
`vibe_input_mode()`, `vibe_request(VIBE_REQ_AUDIO_INPUT)`.

## Signalweg

```
IN 1 (Mic) → Gate → Kompressor → TUNE (Auto-Tune) → VOX (Vocoder / Harmonizer / Pitch+Formant)
           → Doubler → LOOPER → Delay → Hall → OUT
```
Jede Stufe einzeln an/aus.

## Effekte, von leicht bis krass

| Stufe | leicht | krass |
|---|---|---|
| Auto-Tune | sanfte Korrektur | harter Sprung (T-Pain/Roboter) – ein Regler „Speed“ |
| Pitch/Formant | etwas tiefer/heller | Monster, Chipmunk, Alien |
| Vocoder | dezent unter die Stimme | Daft Punk, Kraftwerk |
| Harmonizer | eine Terz dazu | 4-stimmiger Chor |
| Doubler/Delay/Hall | Raum | Kathedrale, Echo-Wand |

## Looper

- Stimmen übereinander schichten (Beatbox, Chor, Bass mit Vocoder …).
- Aufnahme **nach** den Stimm-Effekten, **vor** Delay/Hall (Raum bleibt live regelbar).
- PLAY = Aufnahme/Overdub/Wiedergabe, STOP = Stopp, Doppel-STOP = Löschen, Undo der letzten Schicht.
- Länge frei (erste Schicht legt sie fest) oder in Takten zum Tempo/MIDI-Clock.
- Später: Ideen aus SLOOP übernehmen (mehrere Spuren).

## Bedienung

- **Tasten** je nach Modus: TUNE = erlaubte Zieltöne (sonst Tonart/Skala) · VOCODER = Trägerakkord spielen ·
  HARMONY = Zweitstimmen.
- **Pads 1–16 = Presets** (Natürlich, Radio, Telefon, Megafon, T-Pain, Roboter, Daft Punk, Chor, Monster, Chipmunk,
  Alien, Kathedrale …); gedrückt halten = nur kurz an (wie ein Pedal).
- **F1–F6 = Seiten, je 4 Encoder:** F1 INPUT (Modus, Gain, Gate, Kompressor) · F2 TUNE (Tonart, Skala, Speed, Stärke) ·
  F3 VOX (Modus, Bänder, Träger, Mix) · F4 PITCH (Tonhöhe, Formant, Doubler) · F5 FX (Delay, Hall) ·
  F6 LOOP (Länge, Overdub-Pegel, Undo, Out-Mix).
- **Display:** Pegel, **Tonhöhenkurve** (gesungen vs. korrigiert), Vocoder-Bänder, Looper-Fortschritt.

## Technik

- Vocoder: Filterbank (z. B. 16 Bänder, Biquads + Hüllkurven) – günstig. Träger: interner Synth (Säge/Puls, polyfon).
- Auto-Tune: Tonhöhe per YIN, Verschiebung per PSOLA (monofone Stimme), ~10–20 ms Latenz.
- Hall: FDN/Freeverb. Alles für H618 und WASM machbar.

## Reihenfolge

1. INPUT-Seite, Gate, Kompressor, Pegel- + Tonhöhenanzeige, **Vocoder**, Delay, Hall.
2. **Looper.**
3. **Auto-Tune** (Speed leicht→krass, Tonarten/Skalen, Tasten als Zieltöne).
4. Harmonizer, Formant, Doubler, Pad-Presets.

## Stand

**Stufe 1 (VOX 0.1, 19.10.2026)** – läuft im Emulator und nativ (`tools/build-native.sh vox`, auch `--arm`).

| Bedienung | was passiert |
|---|---|
| F1 INPUT | Encoder: Modus (LINE/MIC/HI-Z/USB – USB = der Computer am USB-Kabel des VIBE, L+R, seit 12.11.), Gain (−12…+40 dB), Gate (AUS, −80…−20 dB), Kompressor 0–100 %. Display: Eingangspegel mit Gate-Marke, CLIP, Gate offen/zu, Kompressor-Absenkung, Ausgangspegel |
| F2 TUNE | Tonhöhenkurve (die letzten ~2,7 s) + Ton mit Cent-Abweichung. Auto-Tune folgt in Stufe 3 |
| F3 VOX | Encoder: Modus (AUS/VOCODER), Bänder (8–24), Träger (SÄGE/PULS/SUPER/IN 2), Mix. Display: die Bänder live, gehaltene Töne |
| F5 FX | Delay, Zeit (1/16 … 1/2 zum Tempo), Feedback, Hall. Display: Ausgang als Wellenform |
| F4 PITCH, F6 LOOP | Platzhalter (Stufe 4, Stufe 2) |
| A / B / DATA | Tempo / Raumgröße / Ausgangspegel |
| Tasten, OCT −/+ | Trägerakkord des Vocoders (externes MIDI-Keyboard geht auch) |
| K1 HOLD, K2 DRY | Akkord bleibt liegen · nur die Stimme ohne Effekte |
| ENTER | Eingang einschalten (falls der Browser noch nicht gefragt hat) |

Technik: YIN-Tonhöhe auf halber Abtastrate alle ~10,7 ms. Vocoder mit 2 Bandpässen hintereinander je Band und Seite,
Hüllkurven 3/30 ms, Zischlaute über einen Hochpass direkt aus der Stimme, automatischer Pegel (so laut wie die
trockene Stimme). Master: Limiter bei −3 dB + Soft-Clip. Der Looper kommt in `main.c` an die markierte Stelle
(nach dem Vocoder, vor `fx_process`).

Tests ohne Mikro: `build/test-vox/` (`mkvoice.py` erzeugt eine künstliche Stimme, `dry.json` / `vocoder.json`):
`node tools/wasmtest.mjs build/vox.wasm build/test-vox/vocoder.json build/test-vox` (wasmtest kann jetzt
`"input": "datei.wav"` für IN 1 und `"input2"` für IN 2).

Nebenbei gefunden und behoben: `vd_log2` im SDK lieferte den natürlichen Logarithmus des Mantissenteils statt log2
(Fehler bis ~0,3 Oktaven). Betrifft auch `vd_pow` → VIBE GROOVE klingt bei Filter-Cutoff, Drum-Sweeps und
Master-Kompressor minimal anders (jetzt so, wie es gemeint war).

### VOX 1.0 (20.10.2026) – rudimentär fertig, alle vier Stufen drin

Signalweg jetzt: IN 1 → Gate → Kompressor → **Tonhöhen-Engine** (Auto-Tune, Pitch, Formant, Harmonien) → Vocoder →
**Farbe** → **Doubler** → **Looper** → Delay → Hall → OUT.

| Bedienung | was passiert |
|---|---|
| F2 TUNE | Tonart (C…H), Skala (CHROM./DUR/MOLL/PENTA/BLUES/TASTEN), Speed (HART … 400 ms), Stärke (AUS … 100 %). Display: gesungene Kurve grau, korrigierte türkis, erlaubte Töne blau, Zielton |
| F3 VOX | Modus AUS / VOCODER / **HARMONY**. HARMONY: Stimmen (TERZ, QUINTE, DREIKL., OKT. TIEF, CHOR, TASTEN), Breite (Stereo), Pegel. Folgt Tonart + Skala (Chrom. → Dur). Display: gesungener Ton + bis zu drei Harmonietöne |
| F4 PITCH | Tonhöhe ±12 HT, Formant ±8 HT, Doubler, Farbe (AUS/RADIO/TELEFON/MEGAFON/ROBOTER/LO-FI). Display: Ton rein → raus, die 16 Presets |
| F6 LOOP | Länge (FREI, 1/2/4/8 Takte zum Tempo), Overdub (wie viel von den alten Schichten bleibt), Loop-Pegel. Display: Zustand, Wellenform mit Abspielkopf, Zeit, Schichten |
| PLAY | leer → Aufnahme · Aufnahme → Loop läuft · läuft → Overdub · Overdub → läuft · gestoppt → läuft. Pedal-Schalter = PLAY |
| REC | leer → Aufnahme · Aufnahme → schließen + gleich Overdub · läuft ↔ Overdub |
| STOP / 2× STOP | Stopp (zurück an den Anfang) / Loop löschen |
| K3 UNDO (seit 1.1: SHIFT + F6) | letzte Overdub-Schicht weg, nochmal = wieder da |
| Pads 1–16 | Presets (seit 1.1: Programme, speicherbar): NATÜRLICH, RADIO, TELEFON, MEGAFON, T-PAIN, ROBOTER, DAFT PUNK, KRAFTWERK, CHOR, TERZ, MONSTER, CHIPMUNK, ALIEN, KATHEDRALE, ECHO-WAND, LO-FI (oben links = 1, wie im Emulator; Computertastatur 1–8 und Q–I). Tippen = bleibt, länger als 0,45 s halten = nur solange gehalten. DAFT PUNK/KRAFTWERK legen selbst einen Akkord auf HOLD, wenn keiner gegriffen ist. MIDI-Kanal 10, Noten 36–51 = Pads |
| Tasten | Vocoder-Akkord, Zieltöne bei Skala TASTEN, Harmonietöne bei Stimmen TASTEN |

Technik:
- Tonhöhen-Engine (`voice.c`): TD-PSOLA. Analysemarken im Abstand der erkannten Periode, je Ausgangsstimme ein
  Hann-Korn (2 Perioden) im neuen Abstand. Formant = Korn schneller/langsamer lesen. Bis zu 4 Stimmen. Latenz
  gemessen 14–19 ms. Automatischer Pegel (0,8…4×), weil verschobene Stimmen sonst leiser werden (+12 HT: −8 dB).
  Ein-/Ausschalten mit 10 ms Überblendung, ohne Verschiebung läuft die Stimme unverzögert durch.
- Auto-Tune je Tonhöhenschätzung (~10,7 ms): nächster erlaubter Ton mit Hysterese, Korrektur gleitet mit SPEED hin.
- Looper: 30 s stereo, 16 bit, Undo-Puffer wird beim Overdub Sample für Sample mitgeschrieben (kein Kopieren beim
  Tastendruck), 4-ms-Blenden an der Nahtstelle.
- Gemessen (Teststimme, wasmtest): +12 HT trifft auf ±0,16 HT, Auto-Tune in C-Dur rastet auf H2 C3 D3 G3 A3 H3,
  alle 16 Presets ohne NaN, Audio-CPU im Node-WASM 1–3 %.

Tests: `build/test-vox/` – `tune.json`, `pitch12.json`, `harm.json`, `loop.json`, `padhold.json`, `padhold2.json`
(`*-nofx.json` = ohne Delay/Hall, zum Messen).

Offen / Ideen: Auto-Tune-Zieltöne per Pads, Looper mit mehreren Spuren (SLOOP), Presets speichern (`vibe_store_*`),
Expression-Pedal auf Harmonie-Pegel oder Formant, echte Stimme am Gerät abhören (bisher nur Teststimme).

### VOX 1.1 (20.10.2026) – Bedienung wie GROOVE, Mixer mit Dry/Wet, Programme speichern

| Taste | was passiert |
|---|---|
| Farbtasten K1–K6 | die Bereiche INPUT · TUNE · VOX · PITCH · FX · LOOP – stehen immer rechts im Display, der offene leuchtet |
| F1 MIXER | DRY/WET (0 = nur die Stimme, 100 = nur der Effekt), Delay, Hall, Loop-Pegel; zeigt, was gerade im Effekt steckt. SHIFT + F1 = DRY (alles aus) |
| F2 PRESETS | die 16 Programme wie die Pads angeordnet, Encoder 1 wählt. Punkt = eigener Klang |
| F3 / F4 PRG − / + | Programm zurück / vor (auch LEFT / RIGHT) |
| F5 SAVE | tippen = aktuellen Klang aufs aktuelle Pad · SAVE halten + Pad = dorthin · SHIFT + SAVE = Pad zurück auf Werksklang. Das Pad übernimmt Name und Farbe des Klangs |
| F6 GLOBAL | Tempo, Ausgang, PADS (PUNCH-IN = halten nur solange / UMSCHALT. = Pads schalten immer um). SHIFT + F6 = Loop-Undo |
| ENTER | HOLD (Akkord bleibt liegen); solange der Eingang aus ist: Eingang einschalten |

Gespeichert wird mit `vibe_store_*` (Browser: IndexedDB, Gerät: microSD): `programs` = alle 16 Pads, `global` = Eingang,
Tonart/Skala, Looper, Tempo, Ausgang, Pad-Modus, letztes Programm (wird beim Start geladen). Global wird beim Verlassen
der GLOBAL-Seite und mit SAVE geschrieben. Neue Dateien: `prog.c` (Werksklänge, Laden/Speichern).

Hinweis Dry/Wet: Mit Tonhöhen-Effekt liegt der Effektweg ~17 ms hinter der trockenen Stimme – in der Mitte klingt das
bei Pitch 0 wie ein kurzer Doubler, bei Pitch ≠ 0 wie eine Zweitstimme. Gewollt.

Spickzettel im Emulator (rechts neben dem Gerät, Knopf „Spickzettel“ oben): `cheatsheet.html` in diesem Ordner.

### VOX 1.2 (26.10.2026) – De-Esser und ein richtiger Kompressor (Sven: „auf jeden Fall De-Esser, guter Kompressor“)

Vorher die Recherche (Nectar, FabFilter Pro-DS/Pro-C 2, Waves, Logic DeEsser 2, Airwindows …): Split-Band-De-Esser mit
**relativer** Erkennung, zweistufiger Kompressor (erst Spitzen, dann ausgleichen), programmabhängiges Release, alles
ohne Lookahead (keine Latenz).

| Bedienung (F1 INPUT) | was passiert |
|---|---|
| GAIN · GATE · KOMPR. · DE-ESS | die vier beim Singen; MODUS jetzt auf SHIFT |
| SHIFT: MODUS · KOMP-ART · S-FREQ | Eingang · STIMME / FET / OPTO / CLEAN · Zischfrequenz 4–10 kHz (Standard 6,5) |
| Display | neue Zeile DE-ESS: wie weit die Zischlaute gerade abgesenkt werden |

Technik (`dsp.c`):
- Kompressor, ein Regler. STIMME = Stufe 1 (Spitze, 4:1, Knie 6 dB, 2 ms / 60 ms) + Stufe 2 (30-ms-RMS, 2,5:1, Knie
  10 dB, 15 ms, Release 80 ms solange >3 dB über dem Ziel, sonst 900 ms). FET = nur Stufe 1, 6:1, 0,8 ms. OPTO = nur
  Stufe 2, 3:1, Knie 12, Release bis 1,2 s. CLEAN = Stufe 2 neutral (1+4·Regler):1. Pegelausgleich **fest** aus Schwelle
  und Ratio – ein der Absenkung folgender Ausgleich hob genau die lauten Stellen wieder an (gemessen).
- De-Esser: Tiefpass (2-polig) bei S-FREQ, Höhen = Rest (zusammen exakt die Stimme); Höhen-Pegel gegen Gesamtpegel,
  Schwelle −4…−18 dB (je mehr DE-ESS), Absenkung höchstens 3…12 dB, Attack 0,5 ms, Release 40 ms; nicht in der Stille.
- Gespeichert mit den globalen Einstellungen; ältere Speicherstände werden weiter gelesen (dann die Standards:
  STIMME, DE-ESS 30 %, 6,5 kHz).

Gemessen (Teststimme, wasmtest): Zischlaute (5–9 kHz) −5,3 dB, das Stimmband 200–3000 Hz unverändert. Ausgleich
(Stimme 18 dB leise→laut): KOMPR. 0 → 17,6 dB, STIMME 40 % → 15,5, STIMME 80 % → 11,4, OPTO 80 % → 10,3, FET 80 % → 8,2.
Tests: `build/test-vox/ess-*.json`, `comp0/voice40/voice80/fet80/opto80.json`. vibe-check bestanden.

Nächste Schritte (Recherche-Empfehlung): AIR/PRESENCE als Regler, LISTEN für den De-Esser, Presets NATURAL/POP/RAP,
DAW-Parameter (SDK 8) und VOX als Effekt-Plugin (VIBE FM VST FX).

### VOX 1.3 (29.10.2026) – den Loop als Sample speichern (Sven: „es sollte schon Samples speichern können“)

Auf der LOOP-Seite heißt **F5 ALS WAV**: der Loop wird als WAV geschrieben – 32 Bit Float, Stereo, mit der Rate des
Geräts (nichts abgeschnitten: der Loop hält 6 dB über Vollaussteuerung) – nach `VIBE FM/vox/` auf der Quelle, auf die der
Host schreibt (SDK 13, Protokoll 13a): am VIBE die Karte (`/root/vibe-media`), im Browser der Media-Ordner, im Plugin
dessen Medien-Ordner. Name `VOX Loop.wav`, mit Takten `VOX Loop 120 BPM.wav`; der Host macht ihn eindeutig (`(2)`).
Beim Drücken wird der Loop kopiert (er läuft und overdubbt weiter); geschrieben wird aus draw und READY, nie aus dem
Audio. Meldungen: GESPEICHERT: <Name> · KEIN ORT ZUM SPEICHERN · HIER KEINE DATEIEN (Host vor SDK 13).
Test: `build/test-vox/save.json` (zweimal gespeichert, Host antwortet erst „noch nicht“), `save-ro.json` (keine
beschreibbare Quelle). vibe-check bestanden.

### VOX 1.4 (30.10.2026) – vier Loop-Spuren nebeneinander (Sven: „wie eine Boss RC-505 MKII, halt mit 4 wegen der Tasten aufm MPK“)

`loop.c` neu: **vier Spuren** statt eines Loops, je 24 s stereo + Undo-Puffer (zusammen ~42 MB mit dem WAV-Puffer, Grenze 64 MB).
Auf der **LOOP-Seite gehören die Pads dem Looper** – keine Programme, kein Punch-in (Svens Wunsch: „die Punch-in-Effekte
nur auf der anderen Seite“):

| Bedienung (LOOP-Seite) | was passiert |
|---|---|
| Pad 1–4 (obere MPK-Reihe) | die Spur: leer → REC · REC → Loop läuft · läuft → OVERDUB · Overdub → läuft · gestoppt → läuft im Takt · **halten 1 s = Undo/Redo** |
| Pad 5–8 (untere Reihe) | STOP der Spur (beim Aufnehmen: Loop schließen und stoppen) · **halten 1 s = Spur löschen** |
| Encoder 1–4 | Pegel je Spur (SHIFT: LÄNGE, OVERDUB, LOOPS = Gesamtpegel) · MIXER: LOOP = Gesamtpegel |
| PLAY · REC · STOP · Pedal · SHIFT+F6 | wie bisher, auf der **gewählten Spur** (das zuletzt gedrückte Pad) · SHIFT+STOP = alle Stopp · SHIFT+REC = alle löschen |
| F5 ALS WAV | die gewählte Spur als `VOX Loop n.wav` |
| Display | vier Streifen: runde Taste (Zustand, Punkt läuft mit), STOP-Feld, kleine Wellenform mit Abspielkopf, Länge (·x2 = Vielfaches), Schichten; die Pegel darunter auf den Encodern |

Takt: Der erste Loop setzt die Länge (FREI oder Takte zum Tempo). Jede weitere Spur wartet auf den Loop-Anfang (WAIT) und
endet auf einem Vielfachen („… bis zum Loop-Ende“) – eine Uhr (`G`) für alle, Position = (G − Kopf) mod Länge, darum kommt
eine gestoppte Spur in Phase zurück; läuft nichts mehr, springt die Uhr beim nächsten Start auf den Loop-Anfang. Eine Spur
nimmt zur Zeit auf oder overdubbt (eine Stimme). Globale Einstellungen speichern die vier Pegel mit (ältere Stände werden
weiter gelesen). Test: `build/test-vox/loop4.json` (Shots in out-loop4); `loop.json` läuft unverändert.

**Nachtrag 30.10.2026 (Sven: Knacken beim Stoppen, eckig, Lautstärke senkrecht, 2 Effekt-Sends, STOP am Loop-Ende):**
- **STOP am Loop-Ende** (Pad 5–8, STOP-Taste, SHIFT+STOP für alle, Doppeltipp am Schalter): die Spur spielt bis zu ihrem
  Ende und steht dann; STOP-Feld und Pad blinken solange. Nochmal STOP = sofort, das Spur-Pad = läuft weiter.
- **Kein Knacken:** jede Spur blendet in 5 ms ein und aus (Start, Stopp, Löschen – gelöscht wird erst, wenn sie leise ist),
  der Pegel gleitet (20 ms). Gemessen mit einem Sinus-Loop: Stopp genau am Loop-Ende (4,1997 s statt 4,2), größter
  Sprung beim Anhalten = der eines laufenden 440-Hz-Sinus, kein Klick (`build/test-vox/stopend.json`).
- **Eckige Tasten** statt runder, ein Balken läuft mit dem Loop unten in der Taste.
- **Senkrechte Fader** je Spur: VOL (mit Pegelmeter), **DLY = Send 1 ins Delay**, **HALL = Send 2 in den Hall** (post Fader).
  Encoder 1–4 = Spur 1–4 in der gewählten Reihe, **K6 nochmal** wechselt VOL → DLY → HALL. Die Loops laufen jetzt
  **trocken** am Delay/Hall der Stimme vorbei (vorher bekamen sie deren Anteil mit) – Raum nur noch über die Sends.
  `fx_process` nimmt dafür den Loop-Bus und die zwei Send-Busse. Sends werden mit den globalen Einstellungen gespeichert
  (ältere Stände werden weiter gelesen). Test `sends.json`: 40 % DLY auf Spur 1 klingt nach dem Stopp aus.
- **SHIFT wie beim VIBE DJ:** am MPK senden die Pads nichts, solange SHIFT gehalten wird. Darum auf LOOP: **SHIFT
  antippen, dann Pad 1–8 binnen 1,5 s = diese Spur wählen** (nichts nimmt auf oder spielt; die acht Pads leuchten gelb, die
  Hinweiszeile sagt es). Am VIBE geht auch SHIFT halten + Pad. Die SHIFT-Ebene der Encoder (Länge, Overdub, Loops)
  erscheint erst, wenn SHIFT länger als 0,4 s gehalten wird – ein Tippen lässt die Fader stehen. Test `shiftsel.json`.
- **Alles auf Englisch (Sven 30.10.2026: „alle Bezeichnungen immer auf english“):** Seiten, Encoder, Auswahlwerte, Presets,
  Meldungen, Hinweise im Display (z. B. TONART → KEY, SKALA → SCALE, STÄRKE → AMOUNT, HALL → REVERB, ALS WAV → SAVE WAV,
  NATÜRLICH → NATURAL; Tonnamen H → B). DAW-Parameter „Hall“ heißt jetzt „Reverb“ (ID `hall` bleibt). Die alten Texte
  stehen nur noch in den Abschnitten oben (Geschichte).
- **SHIFT halten ändert auf LOOP nichts mehr am Bild** – LENGTH · OVERDUB · LOOPS sind die vierte Reihe auf K6
  (VOL → DLY → REV → LENGTH/OVERDUB/LOOPS); SHIFT halten zeigt nur „PAD 1-8 = SELECT TRACK“. Test `en.json`.
- **SHIFT + DATA = jeder Regler einzeln (wie DECKS im VIBE DJ, Sven 30.10.2026):** neue SDK-Funktion `vf_shift_walk(1)`
  (opt-in, vibe_gfx.h / vibe_sdk.c): statt des SDK-12-Rahmens über F1–F6 zeigt SHIFT gehalten + Drehrad den FOCUS-Rahmen und
  wandert über alle Regler der Seite (auf LOOP: Spurtaste, VOL, DLY, REV je Spur – jeder Fader ein eigener Halt), dann die
  F-Tasten, zuletzt die Kopfzeile. Rad gedrückt = bearbeiten, weiterdrehen auch ohne SHIFT; der Rahmen bleibt nach dem
  Loslassen und geht mit RETURN, ENTER lang oder 6 s ohne Berührung – dann ist DATA wieder das Programm. Andere Firmwares
  ändern sich nicht (sie rufen es nicht auf). Test `shiftwalk.json`.

### Nachtrag 01.11.2026 – Unterseiten als Chips, Maus im Plugin-Fenster, VST = VIBE

Sven: „Screen kann man kaum lesen“ (im VST), „den Bildschirm mit der Maus steuern“, „VIBE und VST müssen das gleiche
werden“, „Pills einfügen, damit man weiß, dass es Unterseiten gibt – überall“.

- **Ursache fürs Unlesbare:** im Plugin auf dem Mac lag noch die VOX vom 24.10. (1.2), die 1.3/1.4 gingen nur per
  `deploy-device.sh` auf den VIBE – der Firmware-Ordner des Plugins (`~/Library/Application Support/VIBE FM/Firmware`)
  wird nicht mitversorgt, dort muss `build/vox.wasm` + `firmware.json` (als `vox.json`) von Hand hin (01.11. gemacht,
  alter Stand in `replaced-<Datum>/`). Dazu zeigte das Plugin VOX zuerst in der **Editor-Ansicht mit 300 px Display**
  (0,63×) – jetzt 480 px (1:1, ab 1000 px Fensterbreite; `runtime/web/css/vibe.css`), und `panel.js` `crisp()`/`paint()`
  zeichnen das Bild bei krummen Faktoren (1,25×, 1,35×) erst auf ein ganzzahliges Vielfaches und lassen den Browser den
  Rest glätten – vorher verdoppelte Nearest-Neighbour jede vierte Zeile einer 14-px-Schrift. Die Werte-Zeile über den
  F-Tasten in VG_BOLD statt VG_SMALL.
- **Maus im Display** (SDK 18 `VIBE_EV_TOUCH`, Plugin und Browser, nie am VIBE; `vf_clicks(1)` in `vibe_on_init`, seit
  SDK-Commit 23:53 opt-in): Kopfzeile **◀ Seite ▶** = vorige/nächste Farbtaste (vf_key), Chips = Unterseite,
  PRESETS-Zellen und LOOP-Spurtasten (vf_button), Fader (vf_value: Klick = diese Reihe für die Encoder, Rahmen; Ziehen
  auf dem Streifen = Encoder der Spur), STOP-Feld = Pad 5–8, Wellenform = Spur wählen (`touch()` in main.c). F1–F6 und
  die Encoder-Spalten nimmt die Seite selbst (panel.js `displayTouch`). Test `touch.json`.
- **Unterseiten als Chips** (zweite Kopfzeile wie ZVAMPLER, `chips()` in ui.c, `sub_count/sub_now/sub_set` in main.c):
  INPUT **LEVELS · SETUP** (SETUP = die alte SHIFT-Ebene: MODE, COMP TYPE, S-FREQ; SHIFT halten zeigt sie weiter),
  EQ **BANDS · MID** (MID FREQ, MID Q), VOX **OFF · TUNE · VOCODER · HARMONY** (= der Modus, Encoder 1 geht weiter),
  LOOP **VOL · DLY · REV · LENGTH** (die Encoder-Reihe). **Dieselbe Farbtaste nochmal = nächste Unterseite** (wie bisher
  K6 auf LOOP), rechts steht „K1 AGAIN = NEXT“. Die Seiteninhalte rücken dafür 16–20 px nach unten (`LOOP_OY`).
  PITCH, FX, MIXER, PRESETS, GLOBAL haben keine Unterseiten – keine Chips. Test `chips.json` (Shots in out-chips).
- Auf dem VIBE (deploy-device.sh) und im Firmware-Ordner des Mac-Plugins; Plugin auf dem Mac mit der neuen Seite
  gebaut (`~/vibe-sdk/plugin/out/plugins`), Installation nach `/Library/Audio/Plug-Ins` braucht sudo – Sven.

