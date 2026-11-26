// The board as an instrument. See he_synth.h for what it does and for the VIA bytes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "he_synth.h"
#include "he_perkey.h"
#include "he_midi.h"

#ifdef MIDI_ENABLE
#    include "qmk_midi.h"
void he_synth_out(uint8_t kind, uint8_t channel, uint8_t note, uint8_t value) {
    if (kind == 1) midi_send_noteon(&midi_device, channel, note, value);
    else if (kind == 0) midi_send_noteoff(&midi_device, channel, note, value);
    else midi_send_aftertouch(&midi_device, channel, note, value);
}
#    define HE_SYNTH_HAS_MIDI 1
#else
__attribute__((weak)) void he_synth_out(uint8_t kind, uint8_t channel, uint8_t note, uint8_t value) {}
#    define HE_SYNTH_HAS_MIDI 0
#endif

extern const sensor_to_matrix_map_t sensor_to_matrix_map[];

__attribute__((weak)) void he_synth_changed(bool on) {}

// ---- the scan rate
static uint32_t scans = 0, rate_scans = 0, rate_at = 0;
static uint16_t rate = 0;   // 0 until the first half second has been measured

void he_synth_scan_begin(void) {
    scans++;
    uint32_t now = timer_read32(), gone = now - rate_at;
    if (gone >= 500) {
        rate       = (uint16_t)(((scans - rate_scans) * 1000UL) / gone);
        rate_scans = scans;
        rate_at    = now;
    }
}
uint16_t he_scan_rate(void) { return rate; }

// ---- settings (not stored)
static bool    on = false;
static uint8_t depth_a = 12, depth_b = 60, depth_rel = 30, channel = 0, octave = 2, pressure = 1;
static uint16_t notes_started = 0, last_tenths = 0;
static uint8_t  last_note = 0, last_velocity = 0;

// ---- which key is which note: a piano laid over the board. A key's place decides (its matrix row and
// column, the same on both boards), not what the key types, so the picture holds whatever the keymap is.
// The home row, A to ', is the white keys from middle C. The row above holds the black keys, each over
// the gap it has on a piano: W E, then T Y U, then O P, then ]. Q, R, I and [ sit where a piano has no
// black key and go on typing. Z and X move the octave down and up. It is the layout the computer keyboard
// has in Live and GarageBand, so a player who has used either knows it.
#define NN 0xFF
#define WHITE_ROW 2
#define BLACK_ROW 1
#define SHIFT_ROW 3
#define SHIFT_DOWN_COL 1
#define SHIFT_UP_COL 2
static const uint8_t SYN_WHITE[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17};          // columns 1 to 11: C D E F G A B C D E F
static const uint8_t SYN_BLACK[] = {NN, 1, 3, NN, 6, 8, 10, NN, 13, 15, NN, 18};    // columns 1 to 12: . C# D# . F# G# A# . C# D# . F#
#define NO_NOTE 0xFF
enum { ROLE_NONE, ROLE_NOTE, ROLE_DOWN, ROLE_UP, ROLE_CONTROL, ROLE_DRUM };   // a control: a key the configurator has made a controller (he_midi.c); a drum key: see below

// ---- the drum layout (the configurator's drum machine; owner, 2026-11-26: "for the synth drums, and for the piano
// keys, can we light up only the LEDs that correspond with the layout?" and, of the four ways a drum machine uses
// its lights, "everything listed if they don't conflict"). While it is on, the piano is off: the keys of the
// layout neither type nor play a note here (the configurator plays the drums from the travel stream), and Z and X
// are pads, not octave keys. By place, as the piano is:
//   pads   rows 3, 2, 1, columns 1 to 4 (lowest row first): KICK RIM SNARE CLAP / HAT (free) SHAKER COWBELL / three toms, CYMBAL
//   steps  rows 0 to 3, columns 7 to 10: a row is a beat
//   and up to six more keys the configurator names (the two that write a step's own values, the long key as a second kick)
// The lights, each taking one property so that none hides another: a pad is its voice's colour; a step with a hit
// is the colour of the voice in hand, as bright as its level, and paler if it holds a value of its own; the first
// step of a beat is marked faintly; the step being played is full white, and the board moves that light itself,
// on the DAW's clock or from the time and tempo the configurator gave it, so it is right when the browser is busy.
#define DRUM_FREE 11
static bool     drums = false;
static uint8_t  drum_extra[6], drum_extras = 0;
static uint8_t  dr_hue = 0, dr_hand = 0xFF, dr_steps[16], dr_mode = 0;   // a step: its level 0 to 100, with 0x80 if it holds a value of its own
static uint32_t dr_t0 = 0;                                                // own clock: when step 0 was
static uint16_t dr_step16 = 2000;                                         // own clock: milliseconds a step lasts, times 16
static int8_t   dr_cur = -1;
static const uint8_t DR_HUE[11] = {0, 23, 46, 70, 93, 116, 139, 162, 185, 209, 232};   // round the colour wheel, a voice a colour
static int8_t drum_pad_of(uint8_t r, uint8_t c) {
    if (c < 1 || c > 4) return -1;
    if (r == 3) return c - 1;
    if (r == 2) return c == 1 ? 4 : c == 2 ? DRUM_FREE : c + 2;
    if (r == 1) return c + 6;
    return -1;
}
static int8_t drum_step_of(uint8_t r, uint8_t c) { return (r <= 3 && c >= 7 && c <= 10) ? (int8_t)(r * 4 + (c - 7)) : -1; }
static bool drum_is_extra(uint8_t s) { for (uint8_t i = 0; i < drum_extras; i++) if (drum_extra[i] == s) return true; return false; }
static int8_t drum_now(void) {   // the step being played, or -1
    if (dr_mode == 1) return he_midi_running() ? (int8_t)((he_midi_tick() / 6) % 16) : -1;
    if (dr_mode == 2 && dr_step16) return (int8_t)((((timer_read32() - dr_t0) * 16) / dr_step16) % 16);
    return -1;
}
static uint8_t note_of[SENSOR_COUNT], role_of[SENSOR_COUNT];

static bool lights_dirty;
static void map_notes(void) {
    lights_dirty = true;
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
        uint8_t r = sensor_to_matrix_map[s].row, c = sensor_to_matrix_map[s].col, st = NN;
        if (drums) {
            note_of[s] = NO_NOTE;
            role_of[s] = (drum_pad_of(r, c) >= 0 || drum_step_of(r, c) >= 0 || drum_is_extra(s)) ? ROLE_DRUM : ROLE_NONE;
            continue;
        }
        if (r == WHITE_ROW && c >= 1 && c <= (uint8_t)sizeof(SYN_WHITE)) st = SYN_WHITE[c - 1];
        if (r == BLACK_ROW && c >= 1 && c <= (uint8_t)sizeof(SYN_BLACK)) st = SYN_BLACK[c - 1];
        int16_t n  = st == NN ? -1 : 60 + st + 12 * ((int16_t)octave - 2);
        note_of[s] = (n < 0 || n > 127) ? NO_NOTE : (uint8_t)n;
        role_of[s] = note_of[s] != NO_NOTE ? ROLE_NOTE : (r == SHIFT_ROW && c == SHIFT_DOWN_COL) ? ROLE_DOWN : (r == SHIFT_ROW && c == SHIFT_UP_COL) ? ROLE_UP : he_midi_is_control(s) ? ROLE_CONTROL : ROLE_NONE;
    }
}
void he_synth_remap(void) { if (on) map_notes(); }

// ---- the lights. While the mode is on only the keys that do something in it are lit, so the board shows the
// instrument that is laid over it (owner, 2026-11-26: "can we light up only the LEDs that correspond with the
// layout?"): a white key is white, a black key amber, an octave key or a key the configurator made a control is
// dim, every other key is dark. A key that is down is at full brightness, the others at two fifths of it. The
// brightness is the one the lights had; a board whose lights are off stays dark. The board file paints
// (he_lights_paint: it owns the map from a sensor to its LED) and puts the lighting back as it was on leaving.
// The strip is written at most forty times a second, and once a second whatever else painted over it is undone.
// Nothing is stored.
static bool     lights = true, lights_shown = false;
static uint8_t  light_down[SENSOR_COUNT];
static uint32_t lights_at = 0;
static uint8_t light_kind(uint8_t s) {
    if (role_of[s] == ROLE_NONE) return HE_LIGHT_OFF;
    if (role_of[s] != ROLE_NOTE) return HE_LIGHT_CONTROL;
    return sensor_to_matrix_map[s].row == BLACK_ROW ? HE_LIGHT_BLACK : HE_LIGHT_WHITE;
}
static uint8_t lights_count(void) { uint8_t n = 0; for (uint8_t s = 0; s < SENSOR_COUNT; s++) if (light_kind(s) != HE_LIGHT_OFF) n++; return n; }
static void lights_paint_drums(void) {
    uint8_t hsv[SENSOR_COUNT * 3];
    int8_t  cur = drum_now();
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
        uint8_t r = sensor_to_matrix_map[s].row, c = sensor_to_matrix_map[s].col, h = 0, sa = 0, v = 0;
        int8_t  pad = drum_pad_of(r, c), st = drum_step_of(r, c);
        if (pad >= 0 && pad != DRUM_FREE) { h = DR_HUE[pad]; sa = 255; v = light_down[s] ? 255 : (uint8_t)pad == dr_hand ? 150 : 70; }
        else if (st >= 0) {
            uint8_t level = dr_steps[st] & 0x7F;
            if (st == cur) { v = 255; }                                                             // the running light
            else if (level) { h = dr_hue; sa = (dr_steps[st] & 0x80) ? 110 : 255; v = (uint8_t)(40 + ((uint16_t)(level > 100 ? 100 : level) * 215) / 100); }
            else if ((st & 3) == 0) { v = 18; }                                                     // a beat starts here
        } else if (drum_is_extra(s)) { v = light_down[s] ? 255 : 30; }
        hsv[s * 3] = h; hsv[s * 3 + 1] = sa; hsv[s * 3 + 2] = v;
    }
    dr_cur        = cur;
    lights_shown  = he_lights_paint_hsv(hsv);
    lights_dirty  = false;
    lights_at     = timer_read32();
}
static void lights_paint(void) {
    if (drums) { lights_paint_drums(); return; }
    uint8_t kinds[SENSOR_COUNT];
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) kinds[s] = light_kind(s) | (light_down[s] ? HE_LIGHT_DOWN : 0);
    lights_shown = he_lights_paint(kinds);
    lights_dirty = false;
    lights_at    = timer_read32();
}
void he_synth_lights_task(void) {
    if (!on || !lights) return;
    if (he_config.he_calibration_mode) { lights_dirty = true; return; }   // calibration paints the keys itself; ours come back after it
    if (drums && drum_now() != dr_cur) lights_dirty = true;               // the running light has moved on
    uint32_t since = timer_elapsed32(lights_at);
    if ((lights_dirty && since >= 25) || since >= 1000) lights_paint();
}

// ---- one key
typedef struct {
    uint8_t  phase;       // 0 at rest, 1 between the two depths, 2 sounding
    uint8_t  note;        // the note that was started (the map may change while it sounds)
    uint8_t  sent;        // the pressure last sent
    uint16_t prev;        // travel at the last scan, in tenths of a percent
    uint32_t cross_a;     // when the first depth was passed: scans * 256, placed between scans
    uint32_t deep;        // the last time the key was at or past the second depth, same unit
    uint32_t sent_at;     // the scan the pressure was last sent in
} synth_key_t;
static synth_key_t key[SENSOR_COUNT];

static uint16_t fine(uint8_t s, uint16_t raw) {   // travel in tenths of a percent, whichever way the sensor reads
    uint16_t fl = he_key_configs[s].noise_floor, ce = he_key_configs[s].noise_ceiling;
    if (ce == fl) return 0;
    int32_t num = ce > fl ? (int32_t)raw - fl : (int32_t)fl - raw, den = ce > fl ? ce - fl : fl - ce;
    if (num <= 0) return 0;
    if (num >= den) return 1000;
    return (uint16_t)((num * 1000) / den);
}

// Where between the last scan and this one a depth was passed: the part of a scan (0 to 256) ago.
static uint32_t ago(uint16_t prev, uint16_t now, uint16_t depth) {
    uint16_t span = now > prev ? now - prev : prev - now, past = now > depth ? now - depth : depth - now;
    return span ? ((uint32_t)past * 256) / span : 0;
}

// Time between two depths to velocity: 2 ms and under is the hardest, 80 ms and over the softest, even steps
// for even ratios of time between (a strike twice as slow loses the same amount wherever it starts).
static const uint16_t V_TENTHS[] = {20, 30, 40, 60, 80, 120, 160, 240, 320, 480, 640, 800};
static const uint8_t  V_VALUE[]  = {127, 113, 103, 89, 80, 66, 56, 42, 32, 18, 9, 1};
static uint8_t velocity_for(uint16_t tenths) {
    if (tenths <= V_TENTHS[0]) return 127;
    for (uint8_t i = 1; i < sizeof(V_VALUE); i++) {
        if (tenths <= V_TENTHS[i]) {
            uint16_t span = V_TENTHS[i] - V_TENTHS[i - 1], in = tenths - V_TENTHS[i - 1];
            return (uint8_t)(V_VALUE[i - 1] - ((uint16_t)(V_VALUE[i - 1] - V_VALUE[i]) * in) / span);
        }
    }
    return 1;
}
static uint16_t tenths_of(uint32_t q8) {   // scans * 256 to tenths of a millisecond, at the measured scan rate
    uint32_t r = rate ? rate : 1000;
    if (q8 > 300000UL) q8 = 300000UL;      // over a second at any rate: the softest anyway
    uint32_t t = (q8 * 10000UL / 256UL) / r;
    return t > 0xFFFF ? 0xFFFF : (uint16_t)t;
}

// The board's own notes are kept in while the configurator plays them itself (he_midi.h, quiet): counted, not sent.
static void note_out(uint8_t kind, uint8_t ch, uint8_t note, uint8_t value) { if (!he_midi_quiet()) he_synth_out(kind, ch, note, value); }

static void end_note(synth_key_t *k, uint8_t vel) {
    if (k->phase == 2) note_out(0, channel, k->note, vel);
    k->phase = 0;
}

bool he_synth_key(matrix_row_t current_matrix[], uint8_t s, uint8_t row, uint8_t col, uint16_t raw, bool *changed) {
    if (!on || role_of[s] == ROLE_NONE) return false;
    synth_key_t *k = &key[s];
    if (current_matrix[row] & ((matrix_row_t)1 << col)) {   // it was down as a typing key when the mode came on
        current_matrix[row] &= ~((matrix_row_t)1 << col);
        *changed = true;
    }

    uint16_t t = fine(s, raw), a = depth_a * 10, b = depth_b * 10, rel = depth_rel * 10;
    { bool d = light_down[s] ? t + 30 >= a : t >= a;   // its light: down from the first depth, up three under it
      if (d != light_down[s]) { light_down[s] = d; lights_dirty = true; } }
    uint32_t now = scans << 8;
    if (role_of[s] == ROLE_DRUM) {      // a pad or a step key: the configurator plays it from the travel stream; here it only does not type
        k->prev = t;
        return true;
    }
    if (role_of[s] == ROLE_CONTROL) {   // its depth is a controller, a bend or pressure: he_midi.c sends it
        he_midi_control(s, t);
        k->prev = t;
        return true;
    }
    if (role_of[s] != ROLE_NOTE) {
        // An octave key: one step each time it passes the second depth, armed again above the release depth.
        // Notes that sound keep the pitch they were started at and end there; the next strike is in the new octave.
        if (!k->sent && k->prev < b && t >= b) {
            k->sent = 1;
            if (role_of[s] == ROLE_UP && octave < 4) { octave++; map_notes(); }
            if (role_of[s] == ROLE_DOWN && octave > 0) { octave--; map_notes(); }
        } else if (k->sent && t < rel) {
            k->sent = 0;
        }
        k->prev = t;
        return true;
    }
    if (k->phase == 0 && k->prev < a && t >= a) {
        k->phase   = 1;
        k->cross_a = now - ago(k->prev, t, a);
    }
    if (k->phase == 1) {
        if (t >= b) {
            uint32_t cross_b = now - ago(k->prev, t, b);
            last_tenths      = tenths_of(cross_b - k->cross_a);
            last_velocity    = velocity_for(last_tenths);
            last_note        = note_of[s];
            k->note          = last_note;
            k->phase         = 2;
            k->sent          = 0;
            k->sent_at       = scans;
            k->deep          = now;
            notes_started++;
            note_out(1, channel, k->note, last_velocity);
        } else if (t + 30 < a) {
            k->phase = 0;   // it went back before the second depth: no note
        }
    } else if (k->phase == 2) {
        if (t >= b) k->deep = now;
        if (t < rel) {
            end_note(k, velocity_for(tenths_of(now - ago(k->prev, t, rel) - k->deep)));
        } else if (pressure) {
            uint8_t  p    = t <= b ? 0 : (uint8_t)(((uint32_t)(t - b) * 127) / (1000 - b));
            uint8_t  diff = p > k->sent ? p - k->sent : k->sent - p;
            uint32_t gap  = (rate ? rate : 1000) / 100;   // at most a hundred a second for one key
            if (diff >= 2 && scans - k->sent_at >= gap) {
                k->sent    = p;
                k->sent_at = scans;
                note_out(2, channel, k->note, p);
            }
        }
    }
    k->prev = t;
    return true;
}

bool he_synth_on(void) { return on; }

void he_synth_set_on(bool want) {
    if (want == on) return;
    if (!want) {
        for (uint8_t s = 0; s < SENSOR_COUNT; s++) end_note(&key[s], 64);   // nothing is left sounding
        drums = false;
    } else {
        drums = false;
        map_notes();
        memset(key, 0, sizeof(key));
        for (uint8_t s = 0; s < SENSOR_COUNT; s++) key[s].prev = 1000;      // a key held while the mode comes on does not strike
    }
    on = want;
    he_midi_mode(on);
    bool lit;                                // the lights say the mode changed; without them the keymap blinks
    if (on) { memset(light_down, 0, sizeof(light_down)); if (lights) lights_paint(); lit = lights_shown; }
    else    { lit = lights_shown; if (lights_shown) he_lights_end(); lights_shown = false; }
    if (!lit) he_synth_changed(on);
}
void he_synth_toggle(void) { he_synth_set_on(!on); }
void he_synth_end_all(void) { for (uint8_t s = 0; s < SENSOR_COUNT; s++) end_note(&key[s], 64); }

static uint8_t within(uint8_t v, uint8_t lo, uint8_t hi) { return v < lo ? lo : v > hi ? hi : v; }

void he_synth_get(uint8_t *d) {
    if (d[1] != 0) return;
    d[2]  = on ? 1 : 0;
    d[3]  = rate >> 8;
    d[4]  = rate & 0xFF;
    d[5]  = depth_a;
    d[6]  = depth_b;
    d[7]  = depth_rel;
    d[8]  = channel;
    d[9]  = octave;
    d[10] = pressure;
    d[11] = notes_started >> 8;
    d[12] = notes_started & 0xFF;
    d[13] = last_note;
    d[14] = last_velocity;
    d[15] = last_tenths >> 8;
    d[16] = last_tenths & 0xFF;
    d[17] = HE_SYNTH_HAS_MIDI;
    d[18] = HE_SYNTH_LAYOUT_PIANO;
    d[19] = 1 | (lights ? 2 : 0) | (lights_shown ? 4 : 0) | 8 | (drums ? 16 : 0);   // the lights: this build has them, they are wanted, they are painted now; it knows the drum layout; that layout is on
    d[21] = drums ? (uint8_t)(drum_now() + 1) : 0;             // the drum step the running light is on, from 1; 0 none
    d[20] = lights_shown ? lights_count() : 0;                 // how many keys are lit
}

void he_synth_set(uint8_t *d) {
    if (d[1] == 0) {
        he_synth_set_on(d[2] != 0);
    } else if (d[1] == 1) {
        if (d[5] != 0xFF || d[6] != 0xFF) {   // the channel or the notes move: what sounds is ended first, where it was started
            for (uint8_t s = 0; s < SENSOR_COUNT; s++) end_note(&key[s], 64);
        }
        if (d[2] != 0xFF) depth_a = within(d[2], 5, 50);
        if (d[3] != 0xFF) depth_b = d[3];
        depth_b = within(depth_b, depth_a + 10, 90);
        if (d[4] != 0xFF) depth_rel = d[4];
        depth_rel = within(depth_rel, 5, depth_b - 5);
        if (d[5] != 0xFF) channel = d[5] & 0x0F;
        if (d[6] != 0xFF) {
            octave = within(d[6], 0, 4);
            map_notes();
        }
        if (d[7] != 0xFF) pressure = d[7] ? 1 : 0;
    } else if (d[1] == 3) {                  // the drum layout on or off, with up to six more keys (row, column) that belong to it
        if (!on) return;
        for (uint8_t s = 0; s < SENSOR_COUNT; s++) end_note(&key[s], 64);
        drums       = d[2] != 0;
        drum_extras = 0;
        for (uint8_t i = 0; drums && i < d[3] && i < 6; i++) {
            for (uint8_t s = 0; s < SENSOR_COUNT; s++) if (sensor_to_matrix_map[s].row == d[4 + i * 2] && sensor_to_matrix_map[s].col == d[5 + i * 2]) { drum_extra[drum_extras++] = s; break; }
        }
        memset(dr_steps, 0, sizeof(dr_steps)); dr_hand = 0xFF; dr_mode = 0; dr_cur = -1;
        memset(key, 0, sizeof(key));
        for (uint8_t s = 0; s < SENSOR_COUNT; s++) key[s].prev = 1000;   // a key held across the change does not strike
        map_notes();
        if (lights) lights_paint();
    } else if (d[1] == 4) {                  // the drum lights: the colour of the voice in hand, which pad that is, and its sixteen steps
        dr_hue = d[2]; dr_hand = d[3];
        memcpy(dr_steps, &d[4], sizeof(dr_steps));
        lights_dirty = true;
    } else if (d[1] == 5) {                  // the running light: 0 stopped, 1 on the DAW's clock, 2 from a time and a tempo
        dr_mode = d[2] > 2 ? 0 : d[2];
        if (dr_mode == 2) {
            uint32_t now = timer_read32();
            dr_t0     = now - (uint32_t)(int32_t)(int16_t)((uint16_t)now - (((uint16_t)d[3] << 8) | d[4]));   // step 0 was at this board-clock time (its low 16 bits), up to half a minute back, or a moment ahead
            dr_step16 = ((uint16_t)d[5] << 8) | d[6];
        }
        lights_dirty = true;
    } else if (d[1] == 2) {                  // the lights: 1 only the instrument's keys while the mode is on, 0 the lighting as it is
        lights = d[2] != 0;
        if (on && lights) lights_paint();
        else if (lights_shown) { he_lights_end(); lights_shown = false; }
    }
}
