#include <test/filesystem.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <string.h>

#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ftw.h>

#define FS_MSG(sev, sys, msg, ...)      MESSAGE("TESTFS", sev, sys, msg, ##__VA_ARGS__)

aresult_t test_filesystem_mkdir(const char *path)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != path);
    TSL_ASSERT_ARG('\0' != *path);

    if (0 > mkdir(path, 0776)) {
        int errnum = errno;
        FS_MSG(SEV_ERROR, "CANNOT-MAKE-DIRECTORY", "Failed to create directory '%s'. Reason: %s (%d)", path, strerror(errnum), errnum);
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

aresult_t test_filesystem_check_exists(const char *path, bool *pexist, size_t *plen_bytes, bool *pis_dir)
{
    aresult_t ret = A_OK;

    struct stat st;

    TSL_ASSERT_ARG(NULL != path);
    TSL_ASSERT_ARG('\0' != *path);
    TSL_ASSERT_ARG(NULL != pexist);

    memset(&st, 0, sizeof(st));

    *pexist = false;
    *pis_dir = false;

    if (0 > stat(path, &st)) {
#ifdef _TSL_DEBUG
        int errnum = errno;
        DIAG("Failed to stat '%s'. Reason: %s (%d)", path, strerror(errnum), errnum);
#endif
        goto done;
    }

    *pexist = true;

    if (NULL != pis_dir) {
        *pis_dir = S_ISDIR(st.st_mode);
    }

    if (NULL != plen_bytes) {
        *plen_bytes = st.st_size;
    }

done:
    return ret;
}

aresult_t test_filesystem_delete(const char *path, bool recurse)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != path);
    TSL_ASSERT_ARG('\0' != *path);

    int _nftw_handle_delete(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
    {
        int func_ret = 0;

        DIAG("DEL '%s'", fpath);

        if (NULL == fpath || NULL == sb || NULL == ftwbuf) {
            func_ret = -1;
        }

        if (S_ISDIR(sb->st_mode)) {
            if (0 != rmdir(fpath)) {
                int errnum = errno;
                func_ret = -1;
                FS_MSG(SEV_ERROR, "CANNOT-RMDIR", "Unable to rmdir '%s'. Reason: %s (%d)", fpath, strerror(errnum), errnum);
                goto done;
            }
        } else {
            if (0 != unlink(fpath)) {
                int errnum = errno;
                func_ret = -1;
                FS_MSG(SEV_ERROR, "CANNOT-UNLINK", "Unable to unlink '%s'. Reason: %s (%d)", fpath, strerror(errnum), errnum);
                goto done;
            }
        }

    done:
        return func_ret;
    }

    if (true == recurse) {
        if (0 > nftw(path, _nftw_handle_delete, 100, FTW_DEPTH | FTW_PHYS)) {
            FS_MSG(SEV_ERROR, "RECURSIVE-DELETE-FAILED", "Failed to recursively delete contents of '%s', skipping", path);
            ret = A_E_INVAL;
            goto done;
        }
    } else {
        if (0 > unlink(path)) {
            int errnum = errno;
            ret = A_E_INVAL;
            FS_MSG(SEV_ERROR, "CANNOT-DELETE-SPECIFIED", "Unable to delete specified file '%s' Reason: %s (%d)", path, strerror(errnum), errnum);
            goto done;
        }
    }

done:
    return ret;
}

