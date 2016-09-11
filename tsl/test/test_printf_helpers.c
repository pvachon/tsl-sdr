#include <tsl/test/helpers.h>

#include <tsl/printf_helpers.h>

#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

TEST_DECL(test_format_sockaddr_t_null)
{
    TEST_ASSERT_EQUALS(strcmp(format_sockaddr_t(NULL), "[NULL]"), 0);

    return TEST_OK;
}

TEST_DECL(test_format_sockaddr_t_ipv4)
{
    struct sockaddr_in ipv4;
    ipv4.sin_family = AF_INET;
    ipv4.sin_addr.s_addr = 0x0100007F;
    ipv4.sin_port = 0xABCD;
    TEST_ASSERT_EQUALS(strcmp(format_sockaddr_t((struct sockaddr *) &ipv4), "127.0.0.1:52651"), 0);

    return TEST_OK;
}

/* TODO-IPV6 */
TEST_DECL(test_format_sockaddr_t_ipv6)
{
    struct sockaddr ipv6;
    ipv6.sa_family = AF_INET6;
    TEST_ASSERT_EQUALS(strcmp(format_sockaddr_t(&ipv6), "[IPv6 not supported]"), 0);

    return TEST_OK;
}

