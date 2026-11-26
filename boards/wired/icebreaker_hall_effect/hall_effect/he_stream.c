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
 *
 * Start value 3, the keys that move: a whole frame is three reports and the endpoint carries one a
 * millisecond, so whole frames stop near 330 a second. This sends one report a millisecond instead,
 * holding only the keys whose travel changed since they were last sent:
 *   [0] HE_STREAM_MOVING_ID   [1] sequence   [2..3] the board's clock in ms, low byte first
 *   [4] how many keys follow (0 to 13)   [5..] pairs: matrix index, travel
 * A change of one step waits for a second step or for the next whole frame, so a sensor that flickers
 * at rest does not fill the endpoint. A whole frame (the report above) goes out four times a second:
 * it is what the host stands on at the start and after a report it missed. While nothing moves a
 * report with no keys goes out every 16 ms so the host's clock keeps running. One millisecond in
 * eight is left free for the configurator's commands.
 */
#include "he_stream.h"

#ifndef RAW_ENABLE
void he_stream_set(uint8_t on) { (void)on; }
void he_stream_task(void) {}
bool he_stream_is_moving(void) { return false; }
#else

#include <string.h>
#include "timer.h"
#include "raw_hid.h"
#include "he_switch_matrix.h"

#define HE_STREAM_REPORT_ID 0xF0
#define HE_STREAM_REPORT_SIZE 32
#define HE_STREAM_PAYLOAD (HE_STREAM_REPORT_SIZE - 3)
#define HE_STREAM_INTERVAL_MS 8      // ~125 frames/s
#define HE_STREAM_INTERVAL_FAST_MS 4 // ~250 frames/s, when the host asks for it
#define HE_STREAM_TIMEOUT_MS 1500 // stop this long after the last keepalive
#define HE_STREAM_MOVING_ID 0xF1
#define HE_STREAM_MOVING_PAIRS ((HE_STREAM_REPORT_SIZE - 5) / 2)
#define HE_STREAM_WHOLE_MS 250    // a whole frame this often in the moving stream
#define HE_STREAM_BEAT_MS 16      // a report this often while nothing moves
#define HE_STREAM_TOTAL (MATRIX_ROWS * MATRIX_COLS)

static bool     stream_on       = false;
static uint32_t stream_deadline = 0;
static uint32_t last_frame      = 0;
static uint8_t  seq             = 0;
static uint8_t  interval        = HE_STREAM_INTERVAL_MS;
static bool     moving          = false;
static uint8_t  told[HE_STREAM_TOTAL];   // what the host was last told about each key
static uint8_t  moving_seq      = 0;
static uint16_t cursor          = 0;     // where the next search for changed keys starts, so none is starved
static uint32_t last_whole      = 0;
static uint32_t last_moving     = 0;

void he_stream_set(uint8_t on) {
    bool was  = stream_on && moving;
    stream_on = on != 0;
    moving    = on == 3;
    interval  = on == 2 ? HE_STREAM_INTERVAL_FAST_MS : HE_STREAM_INTERVAL_MS;
    if (on) {
        stream_deadline = timer_read32() + HE_STREAM_TIMEOUT_MS;
    }
    if (moving && !was) {
        last_whole  = timer_read32() - HE_STREAM_WHOLE_MS;   // start on a whole frame
        last_moving = timer_read32();
        cursor      = 0;
    }
}

bool he_stream_is_moving(void) { return stream_on && moving; }

static void send_whole(void) {
    const uint8_t *src = &he_live_travel[0][0];
    uint8_t        report[HE_STREAM_REPORT_SIZE];
    uint8_t        chunk = 0;
    for (uint16_t off = 0; off < HE_STREAM_TOTAL; off += HE_STREAM_PAYLOAD, chunk++) {
        uint16_t n = HE_STREAM_TOTAL - off;
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

static void moving_task(void) {
    const uint8_t *src = &he_live_travel[0][0];
    uint32_t       now = timer_read32();
    if (timer_elapsed32(last_whole) >= HE_STREAM_WHOLE_MS) {
        last_whole = now;
        send_whole();
        memcpy(told, src, HE_STREAM_TOTAL);
    }
    if (now == last_moving || (now & 7) == 7) {
        return;   // one report a millisecond at most, and one millisecond in eight left free
    }
    uint8_t report[HE_STREAM_REPORT_SIZE];
    uint8_t n = 0;
    memset(report, 0, sizeof(report));
    for (uint16_t i = 0; i < HE_STREAM_TOTAL && n < HE_STREAM_MOVING_PAIRS; i++) {
        uint16_t at = cursor + i;
        if (at >= HE_STREAM_TOTAL) {
            at -= HE_STREAM_TOTAL;
        }
        uint8_t v = src[at], was = told[at];
        uint8_t d = v > was ? v - was : was - v;
        if (d >= 2) {
            report[5 + n * 2] = (uint8_t)at;
            report[6 + n * 2] = v;
            told[at]          = v;
            n++;
            if (n == HE_STREAM_MOVING_PAIRS) {
                cursor = at + 1 >= HE_STREAM_TOTAL ? 0 : at + 1;   // more may be waiting: go on from here next time
            }
        }
    }
    if (n < HE_STREAM_MOVING_PAIRS) {
        cursor = 0;
    }
    if (n == 0 && timer_elapsed32(last_moving) < HE_STREAM_BEAT_MS) {
        return;
    }
    last_moving = now;
    report[0]   = HE_STREAM_MOVING_ID;
    report[1]   = moving_seq++;
    report[2]   = now & 0xFF;
    report[3]   = (now >> 8) & 0xFF;
    report[4]   = n;
    raw_hid_send(report, HE_STREAM_REPORT_SIZE);
}

void he_stream_task(void) {
    if (!stream_on) {
        return;
    }
    if (timer_expired32(timer_read32(), stream_deadline)) {
        stream_on = false; // host went away without saying stop
        return;
    }
    if (he_is_suspended()) {
        return;
    }
    if (moving) {
        moving_task();
        return;
    }
    if (timer_elapsed32(last_frame) < interval) {
        return;
    }
    last_frame = timer_read32();
    send_whole();
}

#endif // RAW_ENABLE
