#include <config/util.h>

#include <tsl/safe_string.h>

#include <tsl/assert.h>
#include <tsl/cpumask.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/safe_alloc.h>

aresult_t cpu_mask_from_config(struct cpu_mask **pmask, struct config *cfg, const char *field_name)
{
    aresult_t ret = A_OK;

    struct cpu_mask *mask = NULL;
    int core_id = -1;
    struct config core_arr;

    TSL_ASSERT_ARG(NULL != pmask);
    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != field_name);

    *pmask = NULL;

    if (AFAILED(ret = cpu_mask_new(&mask))) {
        goto done;
    }

    if (!AFAILED(ret = config_get_integer(cfg, &core_id, field_name))) {
        if (0 > core_id) {
            DIAG("Negative core ID specified, aborting.");
            ret = A_E_INVAL;
            goto done;
        }

        if (AFAILED(ret = cpu_mask_set(mask, core_id))) {
            DIAG("Failed to set CPU Core mask: %d", core_id);
            goto done;
        }
    } else if (!AFAILED(ret = config_get(cfg, &core_arr, field_name))) {
        size_t nr_entries = 0;
        bool failed = false;
        size_t num_set = 0;

        if (AFAILED(ret = config_array_length(&core_arr, &nr_entries))) {
            DIAG("Array is malformed.");
            goto done;
        }

        if (0 == nr_entries) {
            DIAG("Array is empty, need to specify an array of CPU core ID integers.");
            ret = A_E_INVAL;
            goto done;
        }

        /* TODO: walk this with the new array helpers */
        for (size_t i = 0; i < nr_entries; i++) {
            int arr_core_id = -1;
            if (AFAILED(ret = config_array_at_integer(&core_arr, &arr_core_id, i))) {
                DIAG("Array entry %zu is not an integer, skipping.", i);
                failed = true;
                continue;
            }

            if (0 > arr_core_id) {
                DIAG("Core ID at %zu is invalid (%d is less than 0)", i, arr_core_id);
                failed = true;
                continue;
            }

            if (AFAILED(ret = cpu_mask_set(mask, arr_core_id))) {
                DIAG("Invalid core ID specified: %d at offset %zu", arr_core_id, i);
                failed = true;
                continue;
            }

            num_set++;
        }

        if (true == failed || 0 == num_set) {
            DIAG("Failed to populate CPU core, malformed array entries were found.");
            ret = A_E_INVAL;
            goto done;
        }
    } else {
        DIAG("Failed to find CPU core configuration field '%s'", field_name);
        ret = A_E_NOENT;
        goto done;
    }

    *pmask = mask;

done:
    if (AFAILED(ret)) {
        if (NULL != mask) {
            TSL_BUG_IF_FAILED(cpu_mask_delete(&mask));
        }
    }

    return ret;
}
