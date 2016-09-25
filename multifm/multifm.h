#pragma once

#include <tsl/diag.h>

#define MFM_MSG(sev, sys, msg, ...) MESSAGE("MULTIFM", sev, sys, msg, ##__VA_ARGS__)
