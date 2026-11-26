// The board as an instrument: a mode in which note keys send MIDI notes instead of typing.
//
// The mode is off at power-up and is entered and left on purpose: a key (the keymap's SYNTH keycode)
// or the configurator toggles it. While it is on, the board carries a piano, by where a key sits and
// not by what it types: the home row A to ' is the white keys from middle C, the row above holds the
// black keys over the gaps a piano has them in (W E, T Y U, O P, ]), and Z and X move the octave down
// and up. Those keys do not type; they play. Every other key types as usual.
// (Until 1.0.6 / 0.4.2.16 the notes followed the base layer's keycodes in the tracker layout; a build
// with this layout says so in d[18] of the get.)
//
// A note is struck between two depths. The time the key takes from the first depth to the second,
// counted in scans and placed between scans from the two readings either side of each crossing, is
// its velocity; the note sounds at the second depth. Past that depth, travel is pressure (polyphonic
// aftertouch). The note ends when the key comes back above the release depth, and how fast it came
// back from the second depth is the release velocity.
//
// VIA custom channel, value id HE_VAL_SYNTH (27). d[0] is the id itself.
//   get  d[1] 0 -> d[2] 1 while the mode is on, d[3..4] scans per second (high byte first),
//                  d[5] first depth %, d[6] second depth %, d[7] release depth %, d[8] channel 0-15,
//                  d[9] octave shift + 2 (0 to 4), d[10] 1 if pressure is sent,
//                  d[11..12] notes started since power-up, d[13] the last note, d[14] its velocity,
//                  d[15..16] the time between the two depths of that note in tenths of a millisecond,
//                  d[17] 1 if this build has the MIDI interface (0: events are counted, not sent),
//                  d[18] the note layout: 1 the piano by place (0, older builds: the tracker layout by keycode)
//                  d[19] the lights: bit 0 this build has them, bit 1 they are wanted, bit 2 they are painted now,
//                  d[20] how many keys are lit (0 while they are not painted);
//                  d[19] also: bit 3 this build knows the drum layout, bit 4 that layout is on now; d[21] the drum step being played, from 1 (0 none)
//   set  d[1] 0, d[2] 1 or 0            the mode on or off
//   set  d[1] 1, d[2] first depth, d[3] second depth, d[4] release depth, d[5] channel,
//        d[6] octave shift + 2, d[7] pressure 1 or 0     (0xFF leaves a value; the depths are kept
//        apart: first 5 to 50, second at least 10 past it and at most 90, release under the second)
//   set  d[1] 3, d[2] 1 or 0, d[3] n, then n pairs (row, column), at most six     the drum layout on or off, and the keys beside the pads and steps that belong to it
//   set  d[1] 4, d[2] hue, d[3] the pad in hand (0 to 10, 0xFF none), d[4..19] sixteen steps: level 0 to 100, with 0x80 if the step holds a value of its own
//   set  d[1] 5, d[2] 0 stopped, 1 on the DAW's clock, 2 own; own: d[3..4] the board's clock (low 16 bits, high byte first) when step 0 was, d[5..6] ms a step lasts times 16
//   set  d[1] 2, d[2] 1 or 0            the lights: only the instrument's keys while the mode is on (the default), or the lighting as it is
// Nothing here is stored: the settings are the defaults again after a power cycle.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "he_switch_matrix.h"

#define HE_VAL_SYNTH 27
#define HE_FEATURE_SYNTH       0x20   // HE_VAL_SYNTH, and the scan rate in HE_VAL_INFO d[7..8]
#define HE_FEATURE_FAST_STREAM 0x40   // the stream's start value 2 asks for 250 frames a second
#define HE_FEATURE_MOVING_STREAM 0x80 // the stream's start value 3: only the keys that move, each millisecond
#define HE_SYNTH_LAYOUT_PIANO  1

void     he_synth_scan_begin(void);   // once per scan: counts scans and works out the scan rate
uint16_t he_scan_rate(void);          // scans per second, measured over the last half second
// One key in one scan. True: the key is a note in synth mode and has been dealt with here, so the
// typing path must not see it. *changed is set if a matrix bit had to be cleared.
bool he_synth_key(matrix_row_t current_matrix[], uint8_t sensor, uint8_t row, uint8_t col, uint16_t raw, bool *changed);
bool he_synth_on(void);
void he_synth_set_on(bool on);
void he_synth_toggle(void);
void he_synth_remap(void);            // the table of control keys changed (he_midi.c): which key does what is worked out again
void he_synth_end_all(void);          // every note that sounds is ended, where it was started
void he_synth_changed(bool on);       // weak: the keymap blinks the lights
void he_synth_lights_task(void);      // once a scan: repaints the lights when a key went down or came up
void he_synth_get(uint8_t *d);
void he_synth_set(uint8_t *d);
// Where the events go. With the MIDI interface: to it. Without (and on a host): a weak stand-in.
// kind 0 note off, 1 note on, 2 pressure.
void he_synth_out(uint8_t kind, uint8_t channel, uint8_t note, uint8_t value);

// The lights (he_synth.c decides, the board's rgb file paints: it owns the map from a sensor to its LED).
// kinds[sensor]: one of the four below, with HE_LIGHT_DOWN added while the key is down.
#define HE_LIGHT_OFF     0
#define HE_LIGHT_WHITE   1    // a white key
#define HE_LIGHT_BLACK   2    // a black key
#define HE_LIGHT_CONTROL 3    // an octave key, or a key the configurator made a control
#define HE_LIGHT_DOWN    0x80
bool he_lights_paint(const uint8_t *kinds);   // false: the lights are off or busy, and nothing was painted
bool he_lights_paint_hsv(const uint8_t *hsv); // the same for the drum layout: three bytes a sensor, hue, saturation, and brightness as a share (0 to 255) of the brightness the lights had
void he_lights_end(void);                     // the lighting as it was before the first paint
