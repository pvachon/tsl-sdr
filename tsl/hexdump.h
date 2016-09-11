#pragma once

#include <tsl/result.h>

#include <stddef.h>
#include <stdio.h>

aresult_t hexdump_dump_hex(const void *buf, size_t length);
aresult_t hexdump_dumpf_hex(FILE* f, const void *buf, size_t length);

