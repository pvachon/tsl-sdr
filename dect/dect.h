#pragma once

#include <tsl/diag.h>

#define DCT_MSG(sev, sys, msg, ...) MESSAGE("DECT", sev, sys, msg, ##__VA_ARGS__)

#define DECT_MASK(_ml)           (( 1u << (_ml) ) - 1)
#define DECT_EXTRACT(_x, _o, _l) (((_x) >> (_o)) & DECT_MASK((_l)))


CAL_PACKED
struct dect_frame_a_field {
    uint8_t header;
#define DECT_FRAME_A_FIELD_HEADER_TAIL_ID(_h)       DECT_EXTRACT((_h), 5, 3)
#define DECT_FRAME_A_FIELD_HEADER_B_FIELD_TYPE(_h)  DECT_EXTRACT((_h), 1, 3)
    uint8_t tail[5];
    uint16_t crc;
};

#define DECT_FRAME_A_FIELD_LENGTH                   64

#define DECT_FP_SYNC     0xAAAAE98Au
#define DECT_PP_SYNC     0x55551675u

#define DECT_HEADER_B_FIELD_NOT_PRESENT             7
#define DECT_HEADER_B_FIELD_HALF_SLOT               4
#define DECT_HEADER_B_FIELD_DOUBLE_SLOT             2

#define DECT_HEADER_B_FIELD_LEN_REGULAR             40
#define DECT_HEADER_B_FIELD_LEN_HALF                10
#define DECT_HEADER_B_FIELD_LEN_DOUBLE              100

enum dect_channel_frame_state {
    /**
     * Currently searching for the sync word
     */
    DECT_CHANNEL_FRAME_STATE_SYNC_SEARCH = 0,

    /**
     * Currently receiving the A Field
     */
    DECT_CHANNEL_FRAME_STATE_A_FIELD_WAIT = 1,

    /**
     * Found the sync word, accumulating the message
     */
    DECT_CHANNEL_FRAME_STATE_PROCESSING = 2,
};

struct dect_channel {
    /**
     * The DECT channel ID
     */
    unsigned chan_id;

    /**
     * File descriptor for the 
     */
    int fd;

    /**
     * Current state for the frame delineation and extraction process
     */
    enum dect_channel_frame_state state;

    uint8_t cur_byte;
    size_t cur_bit;

    uint32_t sync_word;
    size_t nr_bytes;
    size_t rem_bytes;
    size_t b_frame_bytes;
    uint8_t frame[420/8];
};

