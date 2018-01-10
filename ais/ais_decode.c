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
           end_byte = (offset + len + 7)/8,
           end_rem_bits = 0;

    nr_bytes = end_byte - start_byte;

    TSL_BUG_ON(nr_bytes + start_byte > packet_len);

    for (size_t i = 0; i < nr_bytes; i++) {
        acc <<= 8;
        acc |= packet[i + start_byte];
    }

    end_rem_bits = (end_byte * 8) - (offset + len);

    acc >>= end_rem_bits;
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
void _ais_decode_get_string(const uint8_t *packet, size_t packet_len,
        size_t offset, size_t nr_chars, char *dest)
{
    size_t base = offset;
    memset(dest, 0, nr_chars);

    for (size_t i = 0; i < nr_chars; i++) {
        dest[i] = _ais_decode_get_bitfield(packet, packet_len, base, 6);
        base += 6;
    }
}

static
aresult_t _ais_decode_position_report(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi)
{
    aresult_t ret = A_OK;

    uint32_t nav_stat = 0,
             position_acc = 0,
             course = 0,
             heading = 0,
             timestamp = 0;
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
    heading = _ais_decode_get_bitfield(packet, packet_len, 128, 9);
    timestamp = _ais_decode_get_bitfield(packet, packet_len, 137, 6);

    printf("  Nav Stat = %1u RoT = %3f SoG = %4.2f (%9.6f, %9.6f), CoG = %u Heading = %u Timestamp = %u\n",
            nav_stat, (double)rate_of_turn, (double)speed_over_ground/10.0, (double)longitude/600000.0,
            (double)latitude/600000.0, course, heading, timestamp);

    return ret;
}

static
const char *_ais_decode_epfd_type[] = {
    [ 0] = "Undefined",
    [ 1] = "GPS",
    [ 2] = "GLONASS",
    [ 3] = "Combined GPS/GLONASS",
    [ 4] = "Loran-C",
    [ 5] = "Chayka",
    [ 6] = "Integrated Navigation System",
    [ 7] = "Surveyed",
    [ 8] = "Galileo",
    [ 9] = "Unknown 9",
    [10] = "Unknown 10",
    [11] = "Unknown 11",
    [12] = "Unknown 12",
    [13] = "Unknown 13",
    [14] = "Unknown 14",
    [15] = "Unknown 15",
    [16] = "Unknown 16",
};

static
aresult_t _ais_decode_base_station_report(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi)
{
    aresult_t ret = A_OK;

    uint32_t year = 0,
             month = 0,
             day = 0,
             hour = 0,
             minute = 0,
             second = 0,
             epfd_type = 0;
    int32_t longitude = 0,
            latitude = 0;

    year = _ais_decode_get_bitfield(packet, packet_len, 38, 14);
    month = _ais_decode_get_bitfield(packet, packet_len, 52, 4);
    day = _ais_decode_get_bitfield(packet, packet_len, 56, 5);
    hour = _ais_decode_get_bitfield(packet, packet_len, 61, 5);
    minute = _ais_decode_get_bitfield(packet, packet_len, 66, 6);
    second = _ais_decode_get_bitfield(packet, packet_len, 72, 6);

    longitude = _ais_decode_get_bitfield_signed(packet, packet_len, 79, 28);
    latitude = _ais_decode_get_bitfield_signed(packet, packet_len,  107, 27);

    epfd_type = _ais_decode_get_bitfield(packet, packet_len, 134, 4);

    printf("  %04u-%02u-%02u-%02u:%02u:%02u - (%9.6f, %9.6f) - EPFD: %s (%u)\n",
            year, month, day, hour, minute, second, (float)longitude/600000.0,
            (float)latitude/600000.0, _ais_decode_epfd_type[epfd_type & 0xf], epfd_type);

    return ret;
}

static
aresult_t _ais_decode_static_voyage_data(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi)
{
    aresult_t ret = A_OK;

    uint32_t version = 0,
             imo_number = 0,
             ship_type = 0,
             dim_to_bow = 0,
             dim_to_stern = 0,
             dim_to_port = 0,
             dim_to_starboard = 0,
             fix_type = 0,
             eta_month = 0,
             eta_day = 0,
             eta_hour = 0,
             eta_minute = 0,
             draught = 0;

    char callsign[8],
         ship_name[21],
         destination[21];

    TSL_ASSERT_ARG(NULL != decode);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    version = _ais_decode_get_bitfield(packet, packet_len, 38, 2);
    imo_number = _ais_decode_get_bitfield(packet, packet_len, 40, 30);

    _ais_decode_get_string(packet, packet_len, 70, 7, callsign);
    callsign[7] = '\0';
    _ais_decode_get_string(packet, packet_len, 112, 20, ship_name);
    ship_name[20] = '\0';

    ship_type = _ais_decode_get_bitfield(packet, packet_len, 232, 8);
    dim_to_bow = _ais_decode_get_bitfield(packet, packet_len, 240, 9);
    dim_to_stern = _ais_decode_get_bitfield(packet, packet_len, 249, 9);
    dim_to_port = _ais_decode_get_bitfield(packet, packet_len, 258, 6);
    dim_to_starboard = _ais_decode_get_bitfield(packet, packet_len, 264, 6);
    fix_type = _ais_decode_get_bitfield(packet, packet_len, 270, 4);

    eta_month = _ais_decode_get_bitfield(packet, packet_len, 274, 4);
    eta_day = _ais_decode_get_bitfield(packet, packet_len, 278, 5);
    eta_hour = _ais_decode_get_bitfield(packet, packet_len, 283, 5);
    eta_minute = _ais_decode_get_bitfield(packet, packet_len, 288, 6);

    draught = _ais_decode_get_bitfield(packet, packet_len, 294, 8);

    _ais_decode_get_string(packet, packet_len, 302, 20, destination);
    destination[20] = '\0';

    printf("  V=%u Imo=%9u Callsign=[%s] Vessel=[%s] ShipType=%3u (%u, %u, %u, %u) Fix=%s ETA=%u-%u %u:%u Draught=%4.1f",
            version, imo_number, callsign, ship_name, ship_type, dim_to_bow, dim_to_stern,
            dim_to_port, dim_to_starboard, _ais_decode_epfd_type[fix_type & 0xf],
            eta_month, eta_day, eta_hour, eta_minute, (float)draught/10.0);

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
        _ais_decode_base_station_report(decode, packet, packet_len, msg_id, repeat, mmsi);
        break;
    case AIS_MESSAGE_SHIP_STATIC_INFO:
        _ais_decode_static_voyage_data(decode, packet, packet_len, msg_id, repeat, mmsi);
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

