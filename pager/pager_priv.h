#pragma once

#include <tsl/diag.h>

/**
 * A pagerlib debug message
 */
#define PAG_MSG(sev, sys, msg, ...) MESSAGE("PAGER", sev, sys, msg, ##__VA_ARGS__)

