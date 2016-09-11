#pragma once

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/result.h>
#include <tsl/safe_alloc.h>

typedef _Bool (*file_fn)(char* filename, void* att);

static inline
aresult_t for_each_in_dir(const char* directory, file_fn fun, void* att, int* count) {
    struct dirent* in_file = NULL;
    struct dirent* prev_file = NULL;
    int cnt = 0;
    int result = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != directory);
    TSL_ASSERT_ARG_DEBUG(NULL != fun);
    TSL_ASSERT_ARG_DEBUG(NULL != count);

    DIR *dirp = opendir(directory);
    if (NULL == dirp) {
        result = errno;
        return result;
    }

    int len_entry = offsetof(struct dirent, d_name) + fpathconf(dirfd(dirp), _PC_NAME_MAX) + 1;

    prev_file = malloc(len_entry);

    if (NULL == prev_file) {
        result = -1;
        return result;
    }

    while (1)
    {
        result = readdir_r(dirp, prev_file, &in_file);
        if (0 != result || (NULL == in_file)) {
            goto done;
        }

        /* DIAG("looking at %s", in_file->d_name); */
        if (!strcmp (in_file->d_name, "."))
            continue;
        if (!strcmp (in_file->d_name, ".."))
            continue;

        cnt++;

        if (!fun(in_file->d_name, att)) {
            goto done;
        }
    }

done:
    *count = cnt;
    TFREE(prev_file);

    return result;
}
