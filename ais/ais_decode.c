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
    ais_decode_on_position_report_func_t on_position_report;
    ais_decode_on_base_station_report_func_t on_base_station_report;
    ais_decode_on_static_voyage_data_func_t on_static_voyage_data;
};

#define DUMP(...)

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
        char v = _ais_decode_get_bitfield(packet, packet_len, base, 6);
        /* Convert out of the 6-bit ASCII format */
        dest[i] = v > 0x1f ? v : v + 0x40;
        base += 6;
    }
}

static
aresult_t _ais_decode_position_report(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi, const char *raw_msg)
{
    aresult_t ret = A_OK;

    struct ais_position_report rpt;

    TSL_ASSERT_ARG(NULL != decode);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    memset(&rpt, 0, sizeof(rpt));

    rpt.mmsi = mmsi;
    rpt.nav_stat = _ais_decode_get_bitfield(packet, packet_len, 38, 4);
    rpt.rate_of_turn = _ais_decode_get_bitfield_signed(packet, packet_len, 42, 8);
    rpt.speed_over_ground = (float)_ais_decode_get_bitfield(packet, packet_len, 50, 10)/10.0;
    rpt.position_acc = _ais_decode_get_bitfield(packet, packet_len, 60, 1);
    rpt.longitude = (float)_ais_decode_get_bitfield_signed(packet, packet_len, 61, 28)/600000.0;
    rpt.latitude = (float)_ais_decode_get_bitfield_signed(packet, packet_len, 89, 27)/600000.0;
    rpt.course = _ais_decode_get_bitfield(packet, packet_len, 116, 12);
    rpt.heading = _ais_decode_get_bitfield(packet, packet_len, 128, 9);
    rpt.timestamp = _ais_decode_get_bitfield(packet, packet_len, 137, 6);

    DUMP("  Nav Stat = %1u RoT = %3f SoG = %4.2f (%9.6f, %9.6f), CoG = %u Heading = %u Timestamp = %u\n",
            rpt.nav_stat, (double)rpt.rate_of_turn, (double)rpt.speed_over_ground, (double)rpt.latitude,
            (double)rpt.longitude, rpt.course, rpt.heading, rpt.timestamp);

    if (NULL != decode->on_position_report) {
        decode->on_position_report(decode, NULL, &rpt, raw_msg);
    }

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
};

static
aresult_t _ais_decode_base_station_report(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi, const char *raw_msg)
{
    aresult_t ret = A_OK;

    struct ais_base_station_report bsr;

    memset(&bsr, 0, sizeof(bsr));

    bsr.mmsi = mmsi;

    bsr.year = _ais_decode_get_bitfield(packet, packet_len, 38, 14);
    bsr.month = _ais_decode_get_bitfield(packet, packet_len, 52, 4);
    bsr.day = _ais_decode_get_bitfield(packet, packet_len, 56, 5);
    bsr.hour = _ais_decode_get_bitfield(packet, packet_len, 61, 5);
    bsr.minute = _ais_decode_get_bitfield(packet, packet_len, 66, 6);
    bsr.second = _ais_decode_get_bitfield(packet, packet_len, 72, 6);

    bsr.longitude = (float)_ais_decode_get_bitfield_signed(packet, packet_len, 79, 28)/600000.0;
    bsr.latitude = (float)_ais_decode_get_bitfield_signed(packet, packet_len,  107, 27)/600000.0;

    bsr.epfd_type = _ais_decode_get_bitfield(packet, packet_len, 134, 4);
    bsr.epfd_name = _ais_decode_epfd_type[bsr.epfd_type & 0xf];

    DUMP("  %04u-%02u-%02u-%02u:%02u:%02u - (%9.6f, %9.6f) - EPFD: %s (%u)\n",
            bsr.year, bsr.month, bsr.day, bsr.hour, bsr.minute, bsr.second, bsr.latitude,
            bsr.longitude, bsr.epfd_name, bsr.epfd_type);

    if (NULL != decode->on_base_station_report) {
        decode->on_base_station_report(decode, NULL, &bsr, raw_msg);
    }

    return ret;
}

static
aresult_t _ais_decode_static_voyage_data(struct ais_decode *decode, const uint8_t *packet, size_t packet_len,
        unsigned msg_id, unsigned repeat, uint32_t mmsi, const char *raw_msg)
{
    aresult_t ret = A_OK;

    struct ais_static_voyage_data asd;

    TSL_ASSERT_ARG(NULL != decode);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    asd.mmsi = mmsi;

    asd.version = _ais_decode_get_bitfield(packet, packet_len, 38, 2);
    asd.imo_number = _ais_decode_get_bitfield(packet, packet_len, 40, 30);

    _ais_decode_get_string(packet, packet_len, 70, 7, asd.callsign);
    asd.callsign[7] = '\0';
    _ais_decode_get_string(packet, packet_len, 112, 20, asd.ship_name);
    asd.ship_name[20] = '\0';

    asd.ship_type = _ais_decode_get_bitfield(packet, packet_len, 232, 8);
    asd.dim_to_bow = _ais_decode_get_bitfield(packet, packet_len, 240, 9);
    asd.dim_to_stern = _ais_decode_get_bitfield(packet, packet_len, 249, 9);
    asd.dim_to_port = _ais_decode_get_bitfield(packet, packet_len, 258, 6);
    asd.dim_to_starboard = _ais_decode_get_bitfield(packet, packet_len, 264, 6);
    asd.fix_type = _ais_decode_get_bitfield(packet, packet_len, 270, 4);
    asd.epfd_name = _ais_decode_epfd_type[asd.fix_type & 0xf];

    asd.eta_month = _ais_decode_get_bitfield(packet, packet_len, 274, 4);
    asd.eta_day = _ais_decode_get_bitfield(packet, packet_len, 278, 5);
    asd.eta_hour = _ais_decode_get_bitfield(packet, packet_len, 283, 5);
    asd.eta_minute = _ais_decode_get_bitfield(packet, packet_len, 288, 6);

    asd.draught = (float)_ais_decode_get_bitfield(packet, packet_len, 294, 8)/10.0;

    _ais_decode_get_string(packet, packet_len, 302, 20, asd.destination);
    asd.destination[20] = '\0';

    DUMP("  V=%u Imo=%9u Callsign=[%s] Vessel=[%s] ShipType=%3u (%u, %u, %u, %u) Fix=%s ETA=%u-%u %u:%u Draught=%4.1f Destination=[%s]\n",
            asd.version, asd.imo_number, asd.callsign, asd.ship_name, asd.ship_type, asd.dim_to_bow,
            asd.dim_to_stern, asd.dim_to_port, asd.dim_to_starboard, asd.epfd_name,
            asd.eta_month, asd.eta_day, asd.eta_hour, asd.eta_minute, asd.draught, asd.destination);

    if (NULL != decode->on_static_voyage_data) {
        decode->on_static_voyage_data(decode, NULL, &asd, raw_msg);
    }

    return ret;
}

static
char _ais_decode_to_ascii_armor(uint8_t in)
{
    if (in <= 39) {
        return in + 48;
    } else {
        return in - 40 + 96;
    }
}


static
aresult_t _ais_decode_demod_on_msg(struct ais_demod *demod, void *state, const uint8_t *packet,
        size_t packet_len, bool fcs_valid)
{
    aresult_t ret = A_OK;

    uint8_t msg_id = 0,
            repeat = 0;
    uint32_t mmsi = 0;
    size_t offs = 0;
    struct ais_decode *decode = state;
    char msg_ascii_6[(168+(4*256)+5)/6];

    TSL_ASSERT_ARG(NULL != demod);
    TSL_ASSERT_ARG(NULL != state);
    TSL_ASSERT_ARG(NULL != packet);
    TSL_ASSERT_ARG(0 != packet_len);

    memset(msg_ascii_6, 0, sizeof(msg_ascii_6));

    /* Convert the raw message to ASCII for storage */
    for (size_t i = 0; i < sizeof(msg_ascii_6) && offs < packet_len; i += 4) {
        uint32_t accum = 0;
        for (size_t j = offs; j < offs + 3 && j < packet_len; j++) {
            accum <<= 8;
            accum |= packet[j];
        }
        offs += 3;
        for (size_t j = 0; j < 4; j++) {
            msg_ascii_6[i + j] = _ais_decode_to_ascii_armor((accum >> ((3 - j) * 6)) & 0x3f);
        }
    }

    /* Extract the message type */
    msg_id = (packet[0] >> 2) & 0x3f;

    /* Extract repeat indicator */
    repeat = packet[0] & 0x3;

    /* Extract the MMSI from the packet */
    mmsi  = (uint32_t)packet[1] << 22;
    mmsi |= (uint32_t)packet[2] << 14;
    mmsi |= (uint32_t)packet[3] << 6;
    mmsi |= ((uint32_t)packet[4] >> 2) & 0x3f;

    DUMP("MsgId: %02u Rpt: %1u MMSI: %9u (Len: %zu bytes)\n", msg_id, repeat, mmsi, packet_len);

    switch (msg_id) {
    case AIS_MESSAGE_POSITION_REPORT_SOTDMA:
    case AIS_MESSAGE_POSITION_REPORT_SOTDMA2:
    case AIS_MESSAGE_POSITION_REPORT_ITDMA:
        _ais_decode_position_report(decode, packet, packet_len, msg_id, repeat, mmsi, msg_ascii_6);
        break;
    case AIS_MESSAGE_BASE_STATION_REPORT:
        _ais_decode_base_station_report(decode, packet, packet_len, msg_id, repeat, mmsi, msg_ascii_6);
        break;
    case AIS_MESSAGE_SHIP_STATIC_INFO:
        _ais_decode_static_voyage_data(decode, packet, packet_len, msg_id, repeat, mmsi, msg_ascii_6);
        break;
    }

    return ret;
}

aresult_t ais_decode_new(struct ais_decode **pdecode, uint32_t freq, ais_decode_on_position_report_func_t on_position_report, ais_decode_on_base_station_report_func_t on_base_station_report, ais_decode_on_static_voyage_data_func_t on_static_voyage_data)
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

    decode->on_position_report = on_position_report;
    decode->on_base_station_report = on_base_station_report;
    decode->on_static_voyage_data = on_static_voyage_data;

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

