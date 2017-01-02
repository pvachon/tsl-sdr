#pragma once

/**
 * The filter library. Contains various types of filters, including:
 *
 * - Direct FIR (includes an optional phase derotator)
 * - Polyphase FIR (supports rational resampling)
 *
 */

#include <filter/polyphase_fir.h>
#include <filter/direct_fir.h>

typedef int16_t sample_t;

#define Q_15_SHIFT          14

