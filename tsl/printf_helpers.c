#include <tsl/printf_helpers.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/basic.h>

#include <assert.h>
#include <stdio.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <printf.h>
#include <string.h>

static CAL_THREAD_LOCAL
char _sockaddr_t_format_buffer[250];

const char *format_sockaddr_t(struct sockaddr *saddr)
{
    if (NULL == saddr) {
        return "[NULL]";
    }

    /* How to format depends on what sort of address it is */
    switch (saddr->sa_family) {
    case AF_INET: {
        struct sockaddr_in *sin = (struct sockaddr_in *)saddr;
        uint32_t addr = sin->sin_addr.s_addr;
        snprintf(_sockaddr_t_format_buffer,
                 sizeof(_sockaddr_t_format_buffer),
                 "%d.%d.%d.%d:%d",
                 (int)( addr & 0xff ),
                 (int)( (addr >> 8) & 0xff),
                 (int)( (addr >> 16) & 0xff ),
                 (int)( (addr >> 24) & 0xff ),
                 (int)htons(sin->sin_port));
        break;
    }

    case AF_INET6:
        return "[IPv6 not supported]"; /* TODO-IPV6 */

    default:
        snprintf(_sockaddr_t_format_buffer,
                 sizeof(_sockaddr_t_format_buffer),
                 "[unknown sa_family=%d]", saddr->sa_family);
        break;
    }

    /* Assure that the buffer is null-terminated before returning it */
    _sockaddr_t_format_buffer[sizeof(_sockaddr_t_format_buffer) - 1] = 0;
    return _sockaddr_t_format_buffer;
}

