#pragma once

#include <tsl/diag.h>

#define FIL_MSG(sev, sys, msg, ...)                 MESSAGE("FILTER", sev, sys, msg, ##__VA_ARGS__)

