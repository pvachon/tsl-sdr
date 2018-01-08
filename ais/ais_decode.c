#include <ais/ais_decode.h>
#include <ais/ais_demod.h>
#include <ais/ais_msg_format.h>

#include <tsl/safe_alloc.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

struct ais_decode {
    struct ais_demod *demod;
    uint32_t freq;
};

static
uint32_t _ais_decode_get_bitfield(const uint8_t *packet, size_t packet_len,
        size_t offset, size_t len)
{
    uint64_t acc = 0;
    size_t nr_bytes = 0,
           start_byte = offset/8,
           end_byte = (offset + len + 7)/8;

    nr_bytes = end_byte - start_byte;

    TSL_BUG_ON(nr_bytes + start_byte > packet_len);

    for (size_t i = 0; i < nr_bytes; i++) {
        acc <<= 8;
        acc |= packet[i + start_byte];
    }

    size_t end_rem_bits = (offset + len) % 8;

    acc >>= 8 - end_rem_bits;
    acc &= ((1ull << len) - 1);

    return (uint32_t)acc;
}

static
int32_t _ais_decode_get_bitfield_signed(const uint8_t *packet, size_t packet_len,
        size_t offset, size_t len)
{
    uint32_t t = _ais_decode_get_bitfield(packet, packet_len, offset, len);
    int32_t v = t << (32 - len);
    return v >> (32 - len);
}

static
aresult_t _ais_decode_position_report(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi)
{
    aresult_t ret = A_OK;

    uint32_t nav_stat = 0,
             position_acc = 0,
             course = 0;
    int32_t longitude = 0,
            rate_of_turn = 0,
            speed_over_ground = 0,
            latitude = 0;

    TSL_ASSERT_ARG(NULL != decode);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    nav_stat = _ais_decode_get_bitfield(packet, packet_len, 38, 4);
    rate_of_turn = _ais_decode_get_bitfield_signed(packet, packet_len, 42, 8);
    speed_over_ground = _ais_decode_get_bitfield(packet, packet_len, 50, 10);
    position_acc = _ais_decode_get_bitfield(packet, packet_len, 60, 1);
    longitude = _ais_decode_get_bitfield_signed(packet, packet_len, 61, 28);
    latitude = _ais_decode_get_bitfield_signed(packet, packet_len, 89, 27);
    course = _ais_decode_get_bitfield(packet, packet_len, 116, 12);

    printf("  Nav Stat = %1u RoT = %3f SoG = %4.2f (%9.6f, %9.6f), CoG = %u\n",
            nav_stat, (double)rate_of_turn, (double)speed_over_ground/10.0, (double)longitude/600000.0,
            (double)latitude/600000.0, course);

    return ret;
}

static
aresult_t _ais_decode_demod_on_msg(struct ais_demod *demod, void *state, const uint8_t *packet,
        size_t packet_len, bool fcs_valid)
{
    aresult_t ret = A_OK;

    uint8_t msg_id = 0,
            repeat = 0;
    uint32_t mmsi = 0;
    struct ais_decode *decode = state;

    TSL_ASSERT_ARG(NULL != demod);
    TSL_ASSERT_ARG(NULL != state);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    /* Extract the message type */
    msg_id = (packet[0] >> 2) & 0x3f;

    /* Extract repeat indicator */
    repeat = packet[0] & 0x3;

    /* Extract the MMSI from the packet */
    mmsi  = (uint32_t)packet[1] << 22;
    mmsi |= (uint32_t)packet[2] << 14;
    mmsi |= (uint32_t)packet[3] << 6;
    mmsi |= ((uint32_t)packet[4] >> 2) & 0x3f;

    printf("MsgId: %02u Rpt: %1u MMSI: %9u (Len: %zu bytes)\n", msg_id, repeat, mmsi, packet_len);

    switch (msg_id) {
    case AIS_MESSAGE_POSITION_REPORT_SOTDMA:
    case AIS_MESSAGE_POSITION_REPORT_SOTDMA2:
    case AIS_MESSAGE_POSITION_REPORT_ITDMA:
        _ais_decode_position_report(decode, packet, packet_len, msg_id, repeat, mmsi);
        break;
    case AIS_MESSAGE_BASE_STATION_REPORT:
        break;
    case AIS_MESSAGE_SHIP_STATIC_INFO:
        break;
    }

    return ret;
}

aresult_t ais_decode_new(struct ais_decode **pdecode, uint32_t freq)
{
    aresult_t ret = A_OK;

    struct ais_decode *decode = NULL;

    TSL_ASSERT_ARG(NULL != pdecode);

    if (FAILED(ret = TZAALLOC(decode, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    if (FAILED(ret = ais_demod_new(&decode->demod, decode, _ais_decode_demod_on_msg, freq))) {
        goto done;
    }

    decode->freq = freq;

    *pdecode = decode;

done:
    if (FAILED(ret)) {
        if (NULL != decode) {
            if (NULL != decode->demod) {
                TSL_BUG_IF_FAILED(ais_demod_delete(&decode->demod));
            }
            TFREE(decode);
        }
    }
    return ret;
}

aresult_t ais_decode_delete(struct ais_decode **pdecode)
{
    aresult_t ret = A_OK;

    struct ais_decode *decode = NULL;

    TSL_ASSERT_ARG(NULL != pdecode);
    TSL_ASSERT_ARG(NULL != *pdecode);

    decode = *pdecode;

    if (NULL != decode->demod) {
        TSL_BUG_IF_FAILED(ais_demod_delete(&decode->demod));
    }

    TFREE(decode);

    *pdecode = NULL;

    return ret;
}

aresult_t ais_decode_on_pcm(struct ais_decode *decode, const int16_t *samples, size_t nr_samples)
{
    TSL_ASSERT_ARG(NULL != decode);
    TSL_ASSERT_ARG(NULL != samples);
    TSL_ASSERT_ARG(0 != nr_samples);
    return ais_demod_on_pcm(decode->demod, samples, nr_samples);
}

