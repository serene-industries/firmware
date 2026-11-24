/*
 * Real-time travel stream for the SI configurator's visualizer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * While the host keeps asking (VIA custom value HE_STREAM_VALUE_ID, repeated
 * as a keepalive), the board pushes unsolicited raw HID reports carrying the
 * per-key travel the matrix scan already computed. Nothing is read from the
 * ADC here, so the stream does not slow the scan. The configurator's VIA
 * layer ignores reports whose first byte is not a VIA command id, which is
 * why the stream id sits outside that range.
 *
 * Report (32 bytes):
 *   [0] HE_STREAM_REPORT_ID      [1] frame sequence   [2] chunk index
 *   [3..] travel bytes in matrix order (row * MATRIX_COLS + col), 0xFF = no sensor
 */
#include "he_stream.h"

#ifndef RAW_ENABLE
void he_stream_set(bool on) { (void)on; }
void he_stream_task(void) {}
#else

#include <string.h>
#include "timer.h"
#include "raw_hid.h"
#include "he_switch_matrix.h"

#define HE_STREAM_REPORT_ID 0xF0
#define HE_STREAM_REPORT_SIZE 32
#define HE_STREAM_PAYLOAD (HE_STREAM_REPORT_SIZE - 3)
#define HE_STREAM_INTERVAL_MS 8   // ~125 frames/s
#define HE_STREAM_TIMEOUT_MS 1500 // stop this long after the last keepalive

static bool     stream_on       = false;
static uint32_t stream_deadline = 0;
static uint32_t last_frame      = 0;
static uint8_t  seq             = 0;

void he_stream_set(bool on) {
    stream_on = on;
    if (on) {
        stream_deadline = timer_read32() + HE_STREAM_TIMEOUT_MS;
    }
}

void he_stream_task(void) {
    if (!stream_on) {
        return;
    }
    if (timer_expired32(timer_read32(), stream_deadline)) {
        stream_on = false; // host went away without saying stop
        return;
    }
    if (he_is_suspended() || timer_elapsed32(last_frame) < HE_STREAM_INTERVAL_MS) {
        return;
    }
    last_frame = timer_read32();

    const uint8_t *src   = &he_live_travel[0][0];
    const uint16_t total = MATRIX_ROWS * MATRIX_COLS;
    uint8_t        report[HE_STREAM_REPORT_SIZE];
    uint8_t        chunk = 0;
    for (uint16_t off = 0; off < total; off += HE_STREAM_PAYLOAD, chunk++) {
        uint16_t n = total - off;
        if (n > HE_STREAM_PAYLOAD) {
            n = HE_STREAM_PAYLOAD;
        }
        memset(report, 0, sizeof(report));
        report[0] = HE_STREAM_REPORT_ID;
        report[1] = seq;
        report[2] = chunk;
        memcpy(&report[3], src + off, n);
        raw_hid_send(report, HE_STREAM_REPORT_SIZE);
    }
    seq++;
}

#endif // RAW_ENABLE
