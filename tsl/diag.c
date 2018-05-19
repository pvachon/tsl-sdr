#include <tsl/diag.h>

#include <time.h>

/**
 * Private helper function to get a timestamp for log message display
 *
 * \param year The current year, returned by reference
 * \param month The current month, returned by reference
 * \param day The current day of month, returned by reference
 * \param hour The current hour, returned by reference
 * \param minutes The current minute of the current hour, returned by reference
 * \param seconds The current second of the current minute.
 *
 * \note This function does not check all its arguments, so be wise about how you use it.
 */
void __diag_get_time(unsigned int *year, unsigned int *month, unsigned int *day,
                     unsigned int *hour, unsigned int *minutes, unsigned int *seconds)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    *year = gmt->tm_year + 1900;
    *month = gmt->tm_mon + 1;
    *day = gmt->tm_mday;
    *hour = gmt->tm_hour;
    *minutes = gmt->tm_min;
    *seconds = gmt->tm_sec;
}

