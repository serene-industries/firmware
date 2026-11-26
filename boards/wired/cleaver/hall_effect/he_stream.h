/*
 * Real-time travel stream for the SI configurator's visualizer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// VIA custom value id (channel 0) that starts/stops the stream. The host
// repeats "on" as a keepalive; the stream stops by itself when it goes quiet.
#define HE_STREAM_VALUE_ID 21

// 0 stops it, 1 asks for 125 frames a second, 2 for 250 (each frame is three reports on an endpoint
// that carries one a millisecond, so 250 leaves one slot in four for the configurator's commands).
// 3 asks for the keys that move only: each millisecond one report with the keys whose travel changed
// (see he_stream.c), a whole frame four times a second to stand on, and a beat while nothing moves.
void he_stream_set(uint8_t on);
void he_stream_task(void);
bool he_stream_is_moving(void);   // the keys-that-move stream is on: the clock report (he_midi.c) rides with it
