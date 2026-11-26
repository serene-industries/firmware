// The board as an instrument. See he_synth.h for what it does and for the VIA bytes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "he_synth.h"
#include "he_perkey.h"

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
enum { ROLE_NONE, ROLE_NOTE, ROLE_DOWN, ROLE_UP };
static uint8_t note_of[SENSOR_COUNT], role_of[SENSOR_COUNT];

static void map_notes(void) {
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
        uint8_t r = sensor_to_matrix_map[s].row, c = sensor_to_matrix_map[s].col, st = NN;
        if (r == WHITE_ROW && c >= 1 && c <= (uint8_t)sizeof(SYN_WHITE)) st = SYN_WHITE[c - 1];
        if (r == BLACK_ROW && c >= 1 && c <= (uint8_t)sizeof(SYN_BLACK)) st = SYN_BLACK[c - 1];
        int16_t n  = st == NN ? -1 : 60 + st + 12 * ((int16_t)octave - 2);
        note_of[s] = (n < 0 || n > 127) ? NO_NOTE : (uint8_t)n;
        role_of[s] = note_of[s] != NO_NOTE ? ROLE_NOTE : (r == SHIFT_ROW && c == SHIFT_DOWN_COL) ? ROLE_DOWN : (r == SHIFT_ROW && c == SHIFT_UP_COL) ? ROLE_UP : ROLE_NONE;
    }
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

static void end_note(synth_key_t *k, uint8_t vel) {
    if (k->phase == 2) he_synth_out(0, channel, k->note, vel);
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
    uint32_t now = scans << 8;
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
            he_synth_out(1, channel, k->note, last_velocity);
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
                he_synth_out(2, channel, k->note, p);
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
    } else {
        map_notes();
        memset(key, 0, sizeof(key));
        for (uint8_t s = 0; s < SENSOR_COUNT; s++) key[s].prev = 1000;      // a key held while the mode comes on does not strike
    }
    on = want;
    he_synth_changed(on);
}
void he_synth_toggle(void) { he_synth_set_on(!on); }

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
    }
}
