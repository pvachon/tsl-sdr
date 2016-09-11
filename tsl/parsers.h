#pragma once

#include <tsl/result.h>

#include <stdint.h>

/**
 * Given a string containing a number and (optionally) an order of magnitude,
 * return that string converted to bytes. Orders of magnitude are expressed in
 * powers of two.
 *
 * The orders of magnitude can be:
 * \li E or e for exabytes
 * \li T or t for terabytes
 * \li G or g for gigabytes
 * \li M or m for megabytes
 * \li K or k for kilobytes
 *
 * The presence of no order of magnitude is interpreted as a value in bytes.
 *
 * \param str The string to parse
 * \param value The value parsed from the string, in bytes
 *
 * \return A_OK on succes, A_E_INVAL on invalid structure, an error code otherwise.
 */
aresult_t tsl_parse_mem_bytes(const char *str, uint64_t *value);

/**
 * Parse a string containing a time interval specified in units of time into a time
 * interval in nanoseconds.
 *
 * Supported units of time are:
 * \li ns for Nanoseconds
 * \li us for Microseconds
 * \li ms for Milliseconds
 * \li s for Seconds
 *
 * The presence of no unit of time implies the value is to be interpreted as nanoseconds.
 *
 * \param str The string to parse
 * \param value The value parsed from the string, in nanoseconds
 *
 * \return A_OK on success, A_E_INVAL on invalid structure, an error code otherwise.
 */
aresult_t tsl_parse_time_interval(const char *str, uint64_t *value);
