#pragma once

#define BUG_ON(x) \
    do { \
        if ((x)) { \
            fprintf(stderr, "BUG: " #x " == TRUE\n"); \
            abort() \
        } \
    } while (0)


