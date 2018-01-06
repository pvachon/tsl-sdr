#pragma once

#include <ais/ais_demod.h>

/**
 * Expected input sample rate
 */
#define AIS_INPUT_SAMPLE_RATE       48000

/**
 * Bit rate of AIS messages
 */
#define AIS_BIT_RATE                9600

/**
 * The blind decimation rate for AIS reception. It should be 5, but let's parameterize
 */
#define AIS_DECIMATION_RATE         (AIS_INPUT_SAMPLE_RATE/AIS_BIT_RATE)

/**
 * Structure to track detecting the preamble for AIS
 */
struct ais_demod_detect {
    /**
     * Preamble detection shift registers
     */
    uint32_t preambles[AIS_DECIMATION_RATE];

    /**
     * The last sample we saw (for NRZI decoding)
     */
    uint8_t prior_sample[AIS_DECIMATION_RATE];

    /**
     * Next preamble decimation field to update
     */
    size_t next_field;

};

#define AIS_PACKET_BITS                 256
#define AIS_PACKET_BYTES                (AIS_PACKET_BITS/8)

#define AIS_PACKET_PREAMBLE_BITS        24

#define AIS_PACKET_START_FLAG_BITS      8
#define AIS_PACKET_START_FLAG           0x7e

#define AIS_PACKET_DATA_BITS            168
#define AIS_PACKET_FCS_BITS             16

#define AIS_PACKET_END_FLAG_BITS        8
#define AIS_PACKET_END_FLAG             0x7e


/**
 * Structure to track receiving the AIS message bits. This is before bit unstuffing
 * is performed, but it does detect the packet start/end flags
 */
struct ais_demod_rx {
    uint8_t packet[AIS_PACKET_BYTES];

    /**
     * The previous sample we got
     */
    uint8_t last_sample;

    /**
     * The current bit we're populating
     */
    size_t current_bit;

    /**
     * Number of 1's in a row (to deal with bit stuffing)
     */
    size_t nr_ones;
};

/**
 * State of the demodulator
 */
enum ais_demod_state {
    AIS_DEMOD_STATE_SEARCH_SYNC = 0,
    AIS_DEMOD_STATE_RECEIVING = 1,
};

/**
 * Structure for demodulating a stream of AIS data, single-channel, arriving at 48 kHz
 */
struct ais_demod {
    struct ais_demod_detect detector;
    struct ais_demod_rx packet_rx;
    enum ais_demod_state state;
    ais_demod_on_message_callback_func_t on_msg_cb;
    uint32_t freq;
    size_t sample_skip;
};

#define AIS_MSG(sev, sys, msg, ...)     MESSAGE("AIS", sev, sys, msg, ##__VA_ARGS__)

