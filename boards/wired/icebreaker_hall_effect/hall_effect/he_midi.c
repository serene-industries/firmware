// The bridge between a DAW and the configurator's SYNTH suite. See he_midi.h for what it does and the bytes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "he_midi.h"
#include "he_stream.h"
#include "he_perkey.h"
#include "he_synth.h"

#ifdef MIDI_ENABLE
#    include "qmk_midi.h"
static void on_realtime(MidiDevice *device, uint8_t byte) { he_midi_in(byte, 0, 0); }
static void on_position(MidiDevice *device, uint8_t b0, uint8_t b1, uint8_t b2) { he_midi_in(b0, b1, b2); }
void he_midi_out(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t kind = status & 0xF0;
    midi_send_data(&midi_device, (kind == 0xC0 || kind == 0xD0) ? 2 : 3, status, d1, d2);
}
static void listen(void) {
    static bool done = false;
    if (done) return;
    done = true;
    midi_register_realtime_callback(&midi_device, on_realtime);
    midi_register_songposition_callback(&midi_device, on_position);
}
#else
__attribute__((weak)) void he_midi_out(uint8_t status, uint8_t d1, uint8_t d2) {}
static void listen(void) {}
#endif

#ifdef RAW_ENABLE
#    include "raw_hid.h"
#endif

extern const sensor_to_matrix_map_t sensor_to_matrix_map[];

static bool mode_on = false, quiet = false;

// ---- the clock the DAW sends
static bool     running = false, first_tick = false;
static uint32_t tick_no = 0, tick_at = 0, seen_at = 0;   // the last tick: its number since start and when it came
static bool     seen = false;
static uint32_t ring[25];                               // when the last 25 ticks came, for the tempo over a quarter note
static uint8_t  ring_n = 0, ring_i = 0;
static uint16_t tempo10 = 0;
static bool     tell = false;                           // something the page should hear about now
static uint32_t told_at = 0;

// ---- the board as the clock, when no DAW is one (set d[1] 5). It sends start, then 24 ticks a quarter note at
// the tempo the configurator gave, and counts its own ticks exactly as it counts a DAW's, so the page hears one
// clock whoever keeps it. A DAW that starts sending takes the clock back: the DAW is the master whenever it is one.
static bool     master = false;
static uint16_t m_tempo10 = 1200;
static uint32_t m_last = 0, m_owed = 0;                 // microseconds since the last tick was sent

// ---- the knob (set d[1] 6): while the configurator has it, a turn and a press are counted for the page and do
// nothing else. The counts ride in the clock report.
static bool    knob_taken = false;
static uint8_t knob_turns = 0, knob_presses = 0;

static void clock_tick(uint32_t now) {
    seen    = true;
    seen_at = now;
    ring[ring_i] = now;
    ring_i       = (ring_i + 1) % 25;
    if (ring_n < 25) ring_n++;
    if (ring_n == 25) {
        uint32_t span = now - ring[ring_i];             // 24 ticks: a quarter note
        tempo10       = span ? (uint16_t)(600000UL / span) : 0;
    }
    if (!running) return;
    if (first_tick) first_tick = false; else tick_no++;
    tick_at = now;
    if (tick_no % 6 == 0) tell = true;
}

void he_midi_in(uint8_t b0, uint8_t b1, uint8_t b2) {
    uint32_t now = timer_read32();
    if (master && (b0 == 0xF8 || b0 == 0xFA || b0 == 0xFB || b0 == 0xFC || b0 == 0xF2)) {   // a DAW has a clock: it is the master
        master  = false;
        running = false;
        ring_n  = 0;
        tell    = true;
    }
    switch (b0) {
        case 0xF8: clock_tick(now); break;
        case 0xFA: running = true; first_tick = true; tick_no = 0; tick_at = now; tell = true; break;
        case 0xFB: running = true; tell = true; break;
        case 0xFC: running = false; tell = true; break;
        case 0xF2: tick_no = (((uint32_t)(b2 & 0x7F) << 7) | (b1 & 0x7F)) * 6; first_tick = true; tick_at = now; tell = true; break;
        default: break;
    }
}

bool he_midi_running(void) { return running; }
uint32_t he_midi_tick(void) { return tick_no; }
static uint8_t flags(uint32_t now) { return (running ? 1 : 0) | ((seen && now - seen_at < 1000) ? 2 : 0) | (master ? 4 : 0); }

static void master_set(bool want, uint16_t t10) {
    uint32_t now = timer_read32();
    if (t10) m_tempo10 = t10 < 300 ? 300 : t10 > 3000 ? 3000 : t10;
    if (want == master) { if (master) { tempo10 = m_tempo10; tell = true; } return; }
    master = want;
    if (want) {
        he_midi_out(0xFA, 0, 0);
        running = true; first_tick = true; tick_no = 0; tick_at = now; ring_n = 0;
        tempo10 = m_tempo10;
        m_last  = now; m_owed = 0;
    } else {
        he_midi_out(0xFC, 0, 0);
        running = false;
    }
    tell = true;
}
static void master_task(uint32_t now) {
    uint32_t per = 25000000UL / m_tempo10;              // microseconds a tick lasts: a quarter note is 24
    m_owed += (now - m_last) * 1000UL;
    m_last  = now;
    if (m_owed > per * 4) m_owed = per * 4;             // after a long stall it does not burst
    while (m_owed >= per) {
        m_owed -= per;
        he_midi_out(0xF8, 0, 0);
        clock_tick(now);
        tempo10 = m_tempo10;
    }
}

uint8_t he_midi_features(void) {
    return HE_FEATURE2_MIDI_BRIDGE | HE_FEATURE2_CLOCK_MASTER
#ifdef ENCODER_ENABLE
           | HE_FEATURE2_KNOB
#endif
        ;
}
bool he_midi_knob(bool clockwise) {
    if (!mode_on || !knob_taken) return false;
    knob_turns += clockwise ? 1 : 0xFF;
    tell = true;
    return true;
}
bool he_midi_knob_held(void) { return mode_on && knob_taken; }
bool he_midi_knob_press(void) {
    if (!mode_on || !knob_taken) return false;
    knob_presses++;
    tell = true;
    return true;
}

static void report(uint32_t now) {
#ifdef RAW_ENABLE
    uint8_t r[32] = {0};
    r[0] = HE_STREAM_CLOCK_ID;
    r[1] = flags(now);
    r[2] = tick_at & 0xFF;
    r[3] = (tick_at >> 8) & 0xFF;
    r[4] = tick_no & 0xFF;
    r[5] = (tick_no >> 8) & 0xFF;
    r[6] = (tick_no >> 16) & 0xFF;
    r[7] = (tick_no >> 24) & 0xFF;
    r[8] = tempo10 & 0xFF;
    r[9] = tempo10 >> 8;
    r[10] = knob_turns;
    r[11] = knob_presses;
    r[12] = knob_taken ? 1 : 0;
    raw_hid_send(r, 32);
#endif
}

// ---- what the page plays, waiting for its millisecond
typedef struct { uint16_t due; uint8_t status, d1, d2; bool used; } out_event_t;
static out_event_t queue[HE_OUT_QUEUE];
static uint8_t     waiting = 0, dropped = 0;
static uint16_t    sent = 0, chans = 0;                 // channels a note has gone out on, to quieten them when the mode ends

static void send(uint8_t status, uint8_t d1, uint8_t d2) {
    if ((status & 0xF0) == 0x90) chans |= (uint16_t)1 << (status & 0x0F);
    he_midi_out(status, d1, d2);
    sent++;
}

static void enqueue(uint16_t due, uint8_t status, uint8_t d1, uint8_t d2) {
    uint16_t now = (uint16_t)timer_read32(), ahead = (uint16_t)(due - now);
    if (ahead == 0 || ahead > 30000) { send(status, d1, d2); return; }   // due, late, or not a time on this clock
    for (uint8_t i = 0; i < HE_OUT_QUEUE; i++) {
        if (!queue[i].used) { queue[i] = (out_event_t){due, status, d1, d2, true}; waiting++; return; }
    }
    if (dropped < 255) dropped++;
}

// ---- keys as controls
typedef struct { uint8_t sensor, kind, num, chan, value; uint32_t at; } control_t;
static control_t control[HE_CONTROLS];

bool he_midi_is_control(uint8_t s) {
    for (uint8_t i = 0; i < HE_CONTROLS; i++) if (control[i].kind != HE_CTL_NONE && control[i].sensor == s) return true;
    return false;
}

void he_midi_control(uint8_t s, uint16_t travel) {
    uint8_t  v   = travel <= 80 ? 0 : (uint8_t)(((uint32_t)(travel - 80) * 127) / 920);   // the first 8 % of travel is rest
    uint32_t now = timer_read32();
    for (uint8_t i = 0; i < HE_CONTROLS; i++) {
        control_t *c = &control[i];
        if (c->kind == HE_CTL_NONE || c->sensor != s || c->value == v) continue;
        if (now - c->at < 10 && v != 0 && v != 127) continue;                             // a hundred a second for one key at most; the two ends always go
        c->value = v;
        c->at    = now;
        if (c->kind == HE_CTL_CC) send(0xB0 | c->chan, c->num, v);
        else if (c->kind == HE_CTL_PRESSURE) send(0xD0 | c->chan, v, 0);
        else {                                                                           // the bend keys of a channel make one bend between them
            int16_t sum = 0;
            for (uint8_t j = 0; j < HE_CONTROLS; j++) {
                if (control[j].chan != c->chan) continue;
                if (control[j].kind == HE_CTL_BEND_UP) sum += control[j].value;
                if (control[j].kind == HE_CTL_BEND_DOWN) sum -= control[j].value;
            }
            if (sum > 127) sum = 127;
            if (sum < -127) sum = -127;
            int16_t  amt = (int16_t)(((int32_t)sum * 8191) / 127);
            uint16_t raw = (uint16_t)(amt + 8192);
            send(0xE0 | c->chan, raw & 0x7F, (raw >> 7) & 0x7F);
        }
    }
}

bool he_midi_quiet(void) { return quiet; }

void he_midi_mode(bool on) {
    mode_on = on;
    if (on) { listen(); return; }
    // leaving: nothing waits, nothing is left sounding or bent, and the board is nobody's clock or knob
    if (master) { master = false; running = false; he_midi_out(0xFC, 0, 0); }
    knob_taken = false;
    for (uint8_t i = 0; i < HE_OUT_QUEUE; i++) queue[i].used = false;
    waiting = 0;
    for (uint8_t ch = 0; ch < 16; ch++) if (chans & ((uint16_t)1 << ch)) he_midi_out(0xB0 | ch, 123, 0);
    chans = 0;
    for (uint8_t i = 0; i < HE_CONTROLS; i++) {
        control_t *c = &control[i];
        if (c->kind == HE_CTL_NONE || c->value == 0) continue;
        c->value = 0;
        if (c->kind == HE_CTL_CC) he_midi_out(0xB0 | c->chan, c->num, 0);
        else if (c->kind == HE_CTL_PRESSURE) he_midi_out(0xD0 | c->chan, 0, 0);
        else he_midi_out(0xE0 | c->chan, 0, 0x40);
    }
    quiet = false;
}

void he_midi_task(void) {
    if (!mode_on) return;
    uint32_t now32 = timer_read32();
    uint16_t now   = (uint16_t)now32;
    if (master) master_task(now32);
    if (waiting) {
        for (uint8_t i = 0; i < HE_OUT_QUEUE; i++) {
            if (queue[i].used && (uint16_t)(now - queue[i].due) < 30000) {   // its millisecond has come
                queue[i].used = false;
                waiting--;
                send(queue[i].status, queue[i].d1, queue[i].d2);
            }
        }
    }
    if (he_stream_is_moving() && (tell || now32 - told_at >= 250)) {
        tell    = false;
        told_at = now32;
        report(now32);
    }
}

void he_midi_get(uint8_t *d) {
    uint32_t now = timer_read32();
    if (d[1] == 0) {
        d[2]  = flags(now);
        d[3]  = tempo10 >> 8;
        d[4]  = tempo10 & 0xFF;
        d[5]  = (tick_no >> 24) & 0xFF;
        d[6]  = (tick_no >> 16) & 0xFF;
        d[7]  = (tick_no >> 8) & 0xFF;
        d[8]  = tick_no & 0xFF;
        d[9]  = (now >> 8) & 0xFF;
        d[10] = now & 0xFF;
        d[11] = waiting;
        d[12] = sent >> 8;
        d[13] = sent & 0xFF;
        d[14] = dropped;
        d[15] = HE_CONTROLS;
        d[16] = quiet ? 1 : 0;
        d[17] = master ? 1 : 0;
        d[18] = m_tempo10 >> 8;
        d[19] = m_tempo10 & 0xFF;
        d[20] = knob_turns;
        d[21] = knob_presses;
        d[22] = knob_taken ? 1 : 0;
    } else if (d[1] == 1) {
        uint8_t i = d[2];
        if (i >= HE_CONTROLS || control[i].kind == HE_CTL_NONE) { d[3] = 0xFF; d[4] = 0xFF; d[5] = 0; d[6] = 0; d[7] = 0; return; }
        d[3] = sensor_to_matrix_map[control[i].sensor].row;
        d[4] = sensor_to_matrix_map[control[i].sensor].col;
        d[5] = control[i].kind;
        d[6] = control[i].num;
        d[7] = control[i].chan;
    }
}

void he_midi_set(uint8_t *d) {
    if (d[1] == 1) {
        uint8_t n = d[2] > 5 ? 5 : d[2];
        if (!mode_on) return;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t *e = &d[3 + i * 5];
            if (e[2] < 0x80 || e[2] >= 0xF0) continue;   // channel messages only
            enqueue((uint16_t)e[0] | ((uint16_t)e[1] << 8), e[2], e[3] & 0x7F, e[4] & 0x7F);
        }
    } else if (d[1] == 2) {
        uint8_t i = d[2];
        if (i >= HE_CONTROLS) return;
        uint8_t s = (d[3] == 0xFF || d[4] == 0xFF) ? HE_NO_SENSOR : he_sensor_at(d[3], d[4]);
        if (s == HE_NO_SENSOR || d[5] == HE_CTL_NONE || d[5] > HE_CTL_PRESSURE) { control[i].kind = HE_CTL_NONE; control[i].sensor = HE_NO_SENSOR; he_synth_remap(); return; }
        control[i] = (control_t){s, d[5], d[6] & 0x7F, d[7] & 0x0F, 0, 0};
        he_synth_remap();
    } else if (d[1] == 3) {
        for (uint8_t i = 0; i < HE_CONTROLS; i++) { control[i].kind = HE_CTL_NONE; control[i].sensor = HE_NO_SENSOR; }
        he_synth_remap();
    } else if (d[1] == 4) {
        if (d[2] && !quiet) he_synth_end_all();   // what sounds is ended first, so nothing is left on in the DAW
        quiet = d[2] != 0;
    } else if (d[1] == 5) {                       // the board as the clock: 1 or 0, then tempo x 10 (high byte first; 0 keeps it)
        if (!mode_on) return;
        master_set(d[2] != 0, ((uint16_t)d[3] << 8) | d[4]);
    } else if (d[1] == 6) {                       // the knob: 1 the configurator has it, 0 it does what the keymap says
        knob_taken = mode_on && d[2] != 0;
        tell = true;
    }
}
