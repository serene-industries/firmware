// The bridge between a DAW and the configurator's SYNTH suite, through the board's own MIDI interface.
//
// The owner, 2026-11-26: "make sure all of this is always in sync with the DAWs". A browser page cannot join
// a DAW's clock by itself (no virtual MIDI port on every system, and Ableton Link is not open to pages), but
// the board is a MIDI device the DAW already talks to and a HID device the page already talks to. So:
//
//   clock in   The DAW sends MIDI clock (24 a quarter note), start, stop, continue and song position to the
//              board. The board stamps each tick with its own millisecond clock, works out the tempo, and
//              tells the page in a small report inside the travel stream (start value 3), on the same clock
//              the stream's reports carry. The page's arpeggio, drums and takes run on that clock.
//   notes out  What the page plays is handed to the board with the time it is due on the board's clock; the
//              board sends it to the DAW at that millisecond. The DAW records it as from any controller.
//   controls   A key can be a control: its depth is a controller number, pitch bend up or down, or channel
//              pressure, sent by the board itself, so it works with no page open once it has been set.
//
// Only while the instrument mode is on. Nothing is stored: after a power cycle the table is empty.
//
// The clock report (32 bytes, raw HID, sent only while the moving stream is on):
//   [0] HE_STREAM_CLOCK_ID  [1] flags: 1 running, 2 a clock tick was seen in the last second
//   [2..3] the board's clock in ms at the tick it describes, low byte first
//   [4..7] that tick's number since start (24 a quarter note), low byte first   [8..9] tempo x 10, low first
// It goes out on every sixth tick while running (a sixteenth note), on start, stop, continue and a new
// position, and four times a second otherwise.
//
// VIA custom channel, value id HE_VAL_MIDI (28). d[0] is the id itself.
//   get d[1] 0             -> d[2] flags as above, d[3..4] tempo x 10 (high byte first), d[5..8] tick number
//                             (high first), d[9..10] the board's clock in ms (high first), d[11] events
//                             waiting to go out, d[12..13] events sent (high first), d[14] events dropped
//                             (the queue was full), d[15] how many control slots there are
//   get d[1] 1, d[2] slot  -> d[3] row, d[4] col (0xFF: empty), d[5] kind, d[6] number, d[7] channel
//   set d[1] 1, d[2] n, then n x (due ms low, due ms high, status, data 1, data 2), n up to 5
//                             an event whose time has come, or is more than 30 s away, goes at once
//   set d[1] 2, d[2] slot, d[3] row, d[4] col (0xFF empties it), d[5] kind, d[6] number, d[7] channel
//                             kinds: 1 a controller number, 2 pitch bend up, 3 pitch bend down, 4 channel
//                             pressure. A note key or an octave key cannot be a control.
//   set d[1] 3             every slot emptied
//   set d[1] 4, d[2] 1/0   the board's own notes and pressure kept in (1) or sent (0): kept in while the
//                             page plays them itself, so the DAW does not hear both
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HE_VAL_MIDI 28
#define HE_STREAM_CLOCK_ID 0xF2
#define HE_FEATURE2_MIDI_BRIDGE 0x01   // HE_VAL_INFO d[9]
#define HE_CONTROLS 12
#define HE_OUT_QUEUE 48

enum { HE_CTL_NONE, HE_CTL_CC, HE_CTL_BEND_UP, HE_CTL_BEND_DOWN, HE_CTL_PRESSURE };

void he_midi_mode(bool on);          // the instrument mode came on or went off
void he_midi_task(void);             // once a scan
void he_midi_in(uint8_t b0, uint8_t b1, uint8_t b2);   // a realtime byte or a song position from the host
bool he_midi_is_control(uint8_t sensor);
void he_midi_control(uint8_t sensor, uint16_t travel); // that key's travel, tenths of a percent
bool he_midi_quiet(void);            // the board's own notes are kept in
bool he_midi_running(void);          // the DAW's clock is running
uint32_t he_midi_tick(void);         // the last tick's number since the song's start, 24 a quarter note
void he_midi_get(uint8_t *d);
void he_midi_set(uint8_t *d);
// Where the events go. With the MIDI interface: to it. Without (and on a host): a weak stand-in.
void he_midi_out(uint8_t status, uint8_t d1, uint8_t d2);
