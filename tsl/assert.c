#include <tsl/assert.h>

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <execinfo.h>

#define WARN_ON_BACKTRACE_LEN           6

void __tsl_do_warn(int line_no, const char *filename, const char *msg, ...)
{
    va_list ap;
    size_t bt_len = 0;
    void *bt_symbols[WARN_ON_BACKTRACE_LEN];

    TSL_BUG_ON(NULL == filename);
    TSL_BUG_ON(NULL == msg);

    bt_len = backtrace(bt_symbols, BL_ARRAY_ENTRIES(bt_symbols));

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, " (%s:%d)\n", filename, line_no);

    backtrace_symbols_fd(bt_symbols, bt_len, STDERR_FILENO);

    fprintf(stderr, "-----8<----- Cut Here -----8<-----\n");
}

