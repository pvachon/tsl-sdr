#pragma once

#include <tsl/result.h>
#include <stdbool.h>

/**
 * Create the specified directory, or return an error if it already exists.
 */
aresult_t test_filesystem_mkdir(const char *path);

/**
 * Check if a file exists, and return its size in bytes, optionally.
 */
aresult_t test_filesystem_check_exists(const char *path, bool *pexist, size_t *plen_bytes, bool *pis_dir);

/**
 * Delete the specified path, and, if recursion is specified, walk the directory structure downwards and delete the entire tree.
 */
aresult_t test_filesystem_delete(const char *path, bool recurse);

