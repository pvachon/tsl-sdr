#pragma once

#include <tsl/result.h>
#include <tsl/cal.h>

CAL_CHECKED
aresult_t tstrdup(char **dst, const char *src);

CAL_CHECKED
aresult_t tasprintf(char **dst, const char *fmt, ...);

