#pragma once
#include "host.h"
// as in QMK's timer.h
#define timer_expired32(current, future) ((uint32_t)((current) - (future)) < UINT32_MAX / 2)
