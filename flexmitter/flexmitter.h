#pragma once

#define FLX_MSG(sev, sys, msg, ...) MESSAGE("FLEXMITTER", sev, sys, msg, ##__VA_ARGS__)

