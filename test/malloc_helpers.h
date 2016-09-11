#pragma once

#include <tsl/result.h>

/**
 * Enable sporadic failure mode. This will result in failures at a pseudo-random interval.
 */
aresult_t test_malloc_set_sporadic_failure(void);

/**
 * Enable countdown to failure mode. This will result in malloc(3) failing after a specified
 * time interval.
 */
aresult_t test_malloc_set_countdown_failure(unsigned counter);

/**
 * Disable all malloc(3) failures.
 */
aresult_t test_malloc_disable_failures(void);

