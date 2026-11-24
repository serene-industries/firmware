/*
 * Real-time travel stream for the SI configurator's visualizer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdbool.h>

// VIA custom value id (channel 0) that starts/stops the stream. The host
// repeats "on" as a keepalive; the stream stops by itself when it goes quiet.
#define HE_STREAM_VALUE_ID 21

void he_stream_set(bool on);
void he_stream_task(void);
