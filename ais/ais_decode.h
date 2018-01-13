#pragma once

#include <tsl/result.h>

struct ais_decode;

struct ais_position_report {
    uint32_t mmsi;
    uint32_t nav_stat;
    uint32_t position_acc;
    uint32_t course;
    uint32_t heading;
    uint32_t timestamp;

    float longitude;
    float latitude;

    int32_t rate_of_turn;
    float speed_over_ground;
};

struct ais_base_station_report {
    uint32_t mmsi;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;

    float longitude;
    float latitude;

    uint32_t epfd_type;
    const char *epfd_name;
};

struct ais_static_voyage_data {
    uint32_t mmsi;
    uint32_t version;
    uint32_t imo_number;
    uint32_t ship_type;
    uint32_t dim_to_bow;
    uint32_t dim_to_stern;
    uint32_t dim_to_port;
    uint32_t dim_to_starboard;
    uint32_t fix_type;
    const char *epfd_name;
    uint32_t eta_month;
    uint32_t eta_day;
    uint32_t eta_hour;
    uint32_t eta_minute;
    float draught;
    char callsign[8];
    char ship_name[21];
    char destination[21];
};

typedef aresult_t (*ais_decode_on_position_report_func_t)(struct ais_decode *decode, void *state, struct ais_position_report *rpt, const char *raw_msg);
typedef aresult_t (*ais_decode_on_base_station_report_func_t)(struct ais_decode *decode, void *state, struct ais_base_station_report *bsr, const char *raw_msg);
typedef aresult_t (*ais_decode_on_static_voyage_data_func_t)(struct ais_decode *decode, void *state, struct ais_static_voyage_data *svd, const char *raw_msg);

aresult_t ais_decode_new(struct ais_decode **pdecode, uint32_t freq, ais_decode_on_position_report_func_t on_position_report, ais_decode_on_base_station_report_func_t on_base_station_report, ais_decode_on_static_voyage_data_func_t on_static_voyage_data);
aresult_t ais_decode_delete(struct ais_decode **pdecode);
aresult_t ais_decode_on_pcm(struct ais_decode *decode, const int16_t *samples, size_t nr_samples);

