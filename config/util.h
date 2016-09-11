#pragma once

#include <tsl/cpumask.h>
#include <tsl/assert.h>
#include <tsl/errors.h>
#include <tsl/diag.h>

#include <config/engine.h>

/**
 * Generate a new CPU mask from a configuration object.
 *
 * The configuration field specified in field_name must be one of an integer
 * or an array of integers. An appropriate CPU mask will be generated and returned
 * in pmask, but it is up to the application to apply the mask.
 */
aresult_t cpu_mask_from_config(struct cpu_mask **pmask, struct config *cfg, const char *field_name);
