#pragma once

struct sockaddr;

/**
 * Formats a socket address into a thread-local buffer. Essentially, this exists to be used
 * directly in a list of printf arguments.
 */
const char *format_sockaddr_t(struct sockaddr *saddr);

#define FORMAT_SOCKADDR_T(saddr) _Generic((saddr), struct sockaddr_in *      : format_sockaddr_t((struct sockaddr *) saddr), \
                                                   struct sockaddr_storage * : format_sockaddr_t((struct sockaddr *) saddr))
