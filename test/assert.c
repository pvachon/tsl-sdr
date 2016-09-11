#include <test/assert.h>

#include <stdio.h>
#include <stdbool.h>

#include <tsl/errors.h>

void test_print_char(char x)
{
    printf("'%c'", x);
}

void test_print_int(long long int x)
{
    printf("%lld", x);
}

void test_print_unsigned(unsigned long long x)
{
    printf("0x%llx", x);
}

void test_print_string(const char *str)
{
    printf("%s", str);
}

void test_print_pointer(const void *ptr)
{
    printf("0x%zx", (intptr_t)ptr);
}

void test_print_boolean(bool val)
{
    printf("%s", val ? "true" : "false");
}

static
void _t_generic_result(bool is_error, bool is_warn, unsigned facil, unsigned code)
{
    printf("Error Code Unknown: %s%sFacil: %u Code: %u", is_error ? "ERROR " : "", is_warn ? "WARNING " : "", facil, code);
}

/* TODO: make this use tsl_result_to_string (possibly that will need better facilities) */
void test_print_aresult(aresult_t ret)
{
    bool is_error = (ret & (1 << ARESULT_ERROR_BIT));
    bool is_warn = (ret & (1 << ARESULT_WARNING_BIT));

    unsigned int facil = (ret >> ARESULT_FACILITY_OFFSET) & ((1 << ARESULT_FACILITY_SIZE) - 1);
    unsigned int code = ret & ((1 << ARESULT_CODE_SIZE) - 1);

    if (A_OK == ret) {
        printf("A_OK - Execution completed successfully");
    } else if (facil == FACIL_SYSTEM) {
        switch (ret) {
        case A_E_NOMEM:
            printf("A_E_NOMEM - Out of Memory");
            break;
        case A_E_BADARGS:
            printf("A_E_BADARGS - Invalid argument(s) provided to invocation");
            break;
        case A_E_NOTFOUND:
            printf("A_E_NOTFOUND - Requested resource or item was not found");
            break;
        case A_E_BUSY:
            printf("A_E_BUSY - Resource is in use at this time");
            break;
        case A_E_INVAL:
            printf("A_E_INVAL - A provided entity is invalid");
            break;
        case A_E_NOTHREAD:
            printf("A_E_NOTHREAD - Not thread of given ID was found in work pool");
            break;
        case A_E_EMPTY:
            printf("A_E_EMPTY - Container is empty");
            break;
        case A_E_NO_SOCKET:
            printf("A_E_NO_SOCKET - Socket not found or resource is not a socket");
            break;
        case A_E_NOENT:
            printf("A_E_NOENT - Entity not found");
            break;
        case A_E_INV_DATE:
            printf("A_E_INV_DATE - Date/Time specified is invalid");
            break;
        case A_E_NOSPC:
            printf("A_E_NOSPC - Resource, container or queue is full");
            break;
        case A_E_EXIST:
            printf("A_E_EXIST - Specified entity already exists");
            break;
        case A_E_UNKNOWN:
            printf("A_E_UNKNOWN - An unknown resource or data type is specified");
            break;
        case A_E_DONE:
            printf("A_E_DONE - Entity has finished its lifecycle and cannot be referenced");
            break;
        default:
            _t_generic_result(is_error, is_warn, facil, code);
        }
    } else {
        _t_generic_result(is_error, is_warn, facil, code);
    }
}
