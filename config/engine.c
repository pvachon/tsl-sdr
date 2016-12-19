#include <config/engine.h>

#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/errors.h>
#include <tsl/safe_string.h>

#include <jansson.h>

#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define CONFIG_MSG(sev, ident, message, ...) \
    MESSAGE("CONFIG", sev, ident, message, ##__VA_ARGS__)

#define CONFIG_DIRECTORY_ENV_VAR "TSL_CONFIG"

static const char *config_directory = NULL;

aresult_t config_get_type(struct config *cfg, enum config_atom_type *type)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != type);

    *type = cfg->atom_type;

    return ret;
}

static
aresult_t __config_from_json(struct config *atom, json_t *json)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != atom);
    TSL_ASSERT_ARG(NULL != json);

    switch (json_typeof(json)) {
    case JSON_ARRAY:
        atom->atom_type = CONFIG_ATOM_ARRAY;
        atom->atom_array.array = json;
        /* Double check this is an array */
        if (!json_is_array(json)) {
            ret = A_E_INVAL;
            goto done;
        }
        atom->atom_array.length = json_array_size(json);
        break;
    case JSON_INTEGER:
        atom->atom_type = CONFIG_ATOM_INTEGER;
        atom->atom_integer = json_integer_value(json);
        break;
    case JSON_STRING:
        atom->atom_type = CONFIG_ATOM_STRING;
        atom->atom_string = (char *)json_string_value(json);
        break;
    case JSON_TRUE:
        atom->atom_type = CONFIG_ATOM_BOOLEAN;
        atom->atom_bool = true;
        break;
    case JSON_FALSE:
        atom->atom_type = CONFIG_ATOM_BOOLEAN;
        atom->atom_bool = false;
        break;
    case JSON_REAL:
        atom->atom_type = CONFIG_ATOM_FLOAT;
        atom->atom_float = json_real_value(json);
        break;
    case JSON_NULL:
        atom->atom_type = CONFIG_ATOM_NULL;
        break;
    case JSON_OBJECT:
        atom->atom_type = CONFIG_ATOM_NESTED;
        atom->atom_nested = json;
        break;
    }

done:
    return ret;
}

aresult_t config_new(struct config **pcfg)
{
    aresult_t ret = A_OK;
    struct config *cfg = NULL;

    TSL_ASSERT_ARG(NULL != pcfg);

    *pcfg = NULL;

    if (FAILED(ret = TZALLOC(cfg))) {
        goto done;
    }

    /* Create an empty JSON object to store the configuration */
    cfg->atom_type = CONFIG_ATOM_NESTED;
    cfg->atom_nested = json_object();

    if (NULL == cfg->atom_nested) {
        ret = A_E_NOMEM;
        goto done;
    }

    *pcfg = cfg;

done:
    if (FAILED(ret)) {
        if (NULL != cfg) {
            if (NULL != cfg->atom_nested) {
                json_decref((json_t *)cfg->atom_nested);
                cfg->atom_nested = NULL;
            }
            TFREE(cfg);
        }
    }
    return ret;
}

aresult_t config_add(struct config *cfg, const char *filename)
{
    aresult_t ret = A_OK;
    json_t *file_json = NULL;
    json_error_t err;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_STRING(filename);

    if (cfg->atom_type != CONFIG_ATOM_NESTED) {
        ret = A_E_INVAL;
        goto done;
    }

    TSL_ASSERT_ARG(NULL != cfg->atom_nested);

    file_json = json_load_file(filename, JSON_REJECT_DUPLICATES, &err);

    if (NULL == file_json) {
        CONFIG_MSG(SEV_FATAL, "Parse", "Error during JSON load & parse: %s (at line %d, source %s)", err.text, err.line, err.source);
        ret = A_E_INVAL;
        goto done;
    }

    if (0 > json_object_update((json_t *)cfg->atom_nested, file_json)) {
        DIAG("Error merging in file '%s' to configuration", filename);
        ret = A_E_INVAL;
        goto done;
    }

done:
    if (NULL != file_json) {
        json_decref(file_json);
        file_json = NULL;
    }

    return ret;
}

aresult_t config_set_system_config_directory(const char *directory)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_STRING(directory);

    if (NULL != config_directory) {
        CONFIG_MSG(SEV_WARNING, "SYSTEM-CONFIG-DIR-RESET", "Resetting system config directory from %s to %s",
                   config_directory, directory);
    }

    config_directory = directory;

    return ret;
}

aresult_t config_add_system_config(struct config *cfg, const char *name)
{
    aresult_t ret = A_OK;
    char *filename CAL_CLEANUP(free_string) = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_STRING(name);

    /* Set the system config directory if it hasn't been */
    if (NULL == config_directory) {
        config_directory = getenv(CONFIG_DIRECTORY_ENV_VAR);
        if (NULL == config_directory) {
            config_directory = CONFIG_DIRECTORY_DEFAULT;
        }
    }

    /* Load the appropriate file */
    if (FAILED(ret = tasprintf(&filename, "%s/%s.json", config_directory, name))) {
        CONFIG_MSG(SEV_ERROR, "SYSTEM-CONFIG-FAILED", "Failed to load system config %s (no memory)", name);
        goto done;
    }

    if (FAILED(ret = config_add(cfg, filename))) {
        CONFIG_MSG(SEV_ERROR, "SYSTEM-CONFIG-FAILED", "Failed to load system config %s (from %s)", name, filename);
        goto done;
    }

    CONFIG_MSG(SEV_INFO, "SYSTEM-CONFIG", "Loaded system config %s (from %s)", name, filename);

done:
    return ret;
}

aresult_t config_add_array(struct config *cfg, const char **filenames, size_t nr_filenames)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != filenames);
    /* Zero filenames is OK -- that will happen e.g. if no arguments are passed to a program */

    for (size_t i = 0; i < nr_filenames; ++i) {
        if (FAILED(ret = config_add(cfg, filenames[i]))) {
            goto done;
        }
    }

done:
    return ret;
}

aresult_t config_add_string(struct config *cfg, const char *json_string)
{
    aresult_t ret = A_OK;
    json_t *string_json = NULL;
    json_error_t err;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != json_string);
    TSL_ASSERT_ARG('\0' != *json_string);

    if (cfg->atom_type != CONFIG_ATOM_NESTED) {
        ret = A_E_INVAL;
        goto done;
    }

    TSL_ASSERT_ARG(NULL != cfg->atom_nested);

    string_json = json_loads(json_string, JSON_REJECT_DUPLICATES, &err);

    if (NULL == string_json) {
        CONFIG_MSG(SEV_FATAL, "Parse", "Error during JSON load & parse: %s (at line %d, source %s)", err.text, err.line, err.source);
        ret = A_E_INVAL;
        goto done;
    }

    if (0 > json_object_update((json_t *)cfg->atom_nested, string_json)) {
        DIAG("Error merging in file JSON string into given configuration object");
        ret = A_E_INVAL;
        goto done;
    }

done:
    if (NULL != string_json) {
        json_decref(string_json);
        string_json = NULL;
    }

    return ret;
}

aresult_t config_get(struct config *cfg, struct config *atm, const char *item_id)
{
    aresult_t ret = A_OK;
    char *path = NULL;
    char *current_pos = NULL;
    char *next_pos = NULL;
    json_t *item = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != atm);
    TSL_ASSERT_STRING(item_id);

    if (FAILED(ret = tstrdup(&path, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_NESTED != cfg->atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    item = (json_t *)cfg->atom_nested;
    current_pos = path;

    /* Walk a . separated path into the JSON */
    while (true) {
        if (item == NULL) {
            ret = A_E_NOTFOUND;
            goto done;
        }

        next_pos = strchr(current_pos, '.');
        if (next_pos != NULL) {
            *next_pos++ = '\0';
        } else {
            next_pos = current_pos + strlen(current_pos);
        }
        if (next_pos == current_pos) {
            break;
        }

        item = json_object_get(item, current_pos);
        current_pos = next_pos;
    }

    ret = __config_from_json(atm, item);

done:
    TFREE(path);
    return ret;
}

aresult_t config_serialize(struct config *cfg, char **config)
{
    aresult_t ret = A_OK;
    char *ser = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != config);

    *config = NULL;

    if (NULL == (ser = json_dumps((json_t *)cfg->atom_nested, JSON_INDENT(2)))) {
        DIAG("Failed to serialize configuration to JSON, aborting.");
        ret = A_E_NOMEM;
        goto done;
    }

    *config = ser;

done:
    return ret;
}

aresult_t config_delete(struct config **pcfg)
{
    aresult_t ret = A_OK;
    struct config *cfg = NULL;

    TSL_ASSERT_ARG(NULL != pcfg);
    TSL_ASSERT_ARG(NULL != *pcfg);

    cfg = *pcfg;

    TSL_ASSERT_ARG(CONFIG_ATOM_NESTED == cfg->atom_type);


    if (NULL != cfg->atom_nested) {
        json_decref((json_t *)cfg->atom_nested);
        cfg->atom_nested = NULL;
    }

    memset(cfg, 0, sizeof(*cfg));
    TFREE(*pcfg);

    return ret;
}

aresult_t config_array_length(struct config *atm, size_t *length)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != atm);
    TSL_ASSERT_ARG(NULL != length);

    if (CONFIG_ATOM_ARRAY != atm->atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    *length = atm->atom_array.length;

done:
    return ret;
}

aresult_t config_array_at(struct config *array, struct config *item, size_t index)
{
    aresult_t ret = A_OK;
    json_t *arr_item = NULL;

    TSL_ASSERT_ARG(NULL != array);
    TSL_ASSERT_ARG(NULL != item);

    if (CONFIG_ATOM_ARRAY != array->atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    if (!json_is_array((json_t *)array->atom_array.array)) {
        ret = A_E_INVAL;
        goto done;
    }

    if (json_array_size((json_t *)array->atom_array.array) <= index) {
        ret = A_E_INVAL;
        goto done;
    }

    if (NULL == (arr_item = json_array_get(array->atom_array.array, index))) {
        ret = A_E_INVAL;
        goto done;
    }

    ret = __config_from_json(item, arr_item);
done:
    return ret;
}

aresult_t config_array_at_integer(struct config *array, int *item, size_t index)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != array);
    TSL_ASSERT_ARG(NULL != item);

    *item = 0;

    if (FAILED(ret = config_array_at(array, &atm, index))) {
        goto done;
    }

    if (atm.atom_type != CONFIG_ATOM_INTEGER) {
        ret = A_E_INVAL;
        goto done;
    }

    *item = atm.atom_integer;

done:
    return ret;
}

aresult_t config_array_at_float(struct config *array, double *item, size_t index)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != array);
    TSL_ASSERT_ARG(NULL != item);

    *item = 0;

    if (FAILED(ret = config_array_at(array, &atm, index))) {
        goto done;
    }

    if (atm.atom_type != CONFIG_ATOM_FLOAT) {
        ret = A_E_INVAL;
        goto done;
    }

    *item = atm.atom_float;

done:
    return ret;
}

aresult_t config_array_at_size(struct config *array, size_t *item, size_t index)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != array);
    TSL_ASSERT_ARG(NULL != item);

    *item = 0;

    if (FAILED(ret = config_array_at(array, &atm, index))) {
        goto done;
    }

    if (atm.atom_type != CONFIG_ATOM_INTEGER) {
        ret = A_E_INVAL;
        goto done;
    }

    if (atm.atom_integer < 0) {
        ret = A_E_INVAL;
        goto done;
    }

    *item = (size_t) atm.atom_integer;

done:
    return ret;
}

aresult_t config_array_at_string(struct config *array, const char **item, size_t index)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != array);
    TSL_ASSERT_ARG(NULL != item);

    *item = NULL;

    if (FAILED(ret = config_array_at(array, &atm, index))) {
        goto done;
    }

    if (atm.atom_type != CONFIG_ATOM_STRING) {
        ret = A_E_INVAL;
        goto done;
    }

    *item = atm.atom_string;

done:
    return ret;
}

aresult_t config_get_float_array(struct config *cfg, double **pvals, size_t *length, const char *item_id)
{
    aresult_t ret = A_OK;

    struct config atm;
    double *vals = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pvals);
    TSL_ASSERT_ARG(NULL != length);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    *pvals = NULL;

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (FAILED(ret = config_array_length(&atm, length))) {
        goto done;
    }

    if (FAILED(ret = TCALLOC((void **)&vals, *length, sizeof(double)))) {
        goto done;
    }

    for (size_t i = 0; i < *length; i++) {
        if (FAILED(ret = config_array_at_float(&atm, vals + i, i))) {
            goto done;
        }
    }

    *pvals = vals;

done:
    if (FAILED(ret)) {
        TFREE(vals);
    }

    return ret;
}

aresult_t config_get_integer_array(struct config *cfg, int **pvals, size_t *length, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;
    int *vals = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pvals);
    TSL_ASSERT_ARG(NULL != length);
    TSL_ASSERT_ARG(NULL != item_id);

    *pvals = NULL;

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (FAILED(ret = config_array_length(&atm, length))) {
        goto done;
    }

    if (FAILED(ret = TCALLOC((void **) &vals, *length, sizeof(int)))) {
        goto done;
    }

    for (size_t i = 0; i < *length; ++i) {
        if (FAILED(ret = config_array_at_integer(&atm, vals + i, i))) {
            goto done;
        }
    }

    *pvals = vals;

done:
    return ret;
}

aresult_t config_get_size_array(struct config *cfg, size_t **pvals, size_t *length, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;
    size_t *vals = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pvals);
    TSL_ASSERT_ARG(NULL != length);
    TSL_ASSERT_ARG(NULL != item_id);

    *pvals = NULL;

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (FAILED(ret = config_array_length(&atm, length))) {
        goto done;
    }

    if (FAILED(ret = TCALLOC((void **) &vals, *length, sizeof(size_t)))) {
        goto done;
    }

    for (size_t i = 0; i < *length; ++i) {
        if (FAILED(ret = config_array_at_size(&atm, vals + i, i))) {
            goto done;
        }
    }

    *pvals = vals;

done:
    return ret;
}

aresult_t config_get_integer(struct config *cfg, int *val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_INTEGER != atm.atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = atm.atom_integer;

done:
    return ret;
}

aresult_t config_get_float(struct config *cfg, double *val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_FLOAT != atm.atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = atm.atom_float;

done:
    return ret;
}

aresult_t config_get_size(struct config *cfg, size_t *val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_INTEGER != atm.atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    if (atm.atom_integer < 0) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = (size_t) atm.atom_integer;

done:
    return ret;
}

aresult_t config_get_boolean(struct config *cfg, bool *val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_BOOLEAN != atm.atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = atm.atom_bool;

done:
    return ret;
}

aresult_t config_get_string(struct config *cfg, const char **val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    if (CONFIG_ATOM_STRING != atm.atom_type) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = atm.atom_string;

done:
    return ret;
}

static
aresult_t _config_parse_memory_size(const char *str, char **end, size_t *val)
{
    aresult_t ret = A_OK;
    char *endptr = NULL;
    size_t memval = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != str);
    TSL_ASSERT_ARG_DEBUG(NULL != end);
    TSL_ASSERT_ARG_DEBUG(NULL != val);

    *end = NULL;

    memval = strtoull(str, &endptr, 0);

    switch (*endptr) {
    case 'E':
    case 'e':
        memval <<= 10;
    case 'P':
    case 'p':
        memval <<= 10;
    case 'T':
    case 't':
        memval <<= 10;
    case 'G':
    case 'g':
        memval <<= 10;
    case 'M':
    case 'm':
        memval <<= 10;
    case 'K':
    case 'k':
        memval <<= 10;
        endptr++;
    default:
        break;
    }

    *end = endptr;
    *val = memval;

    return ret;
}

aresult_t config_get_byte_size(struct config *cfg, size_t *val, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    switch (atm.atom_type) {
    case CONFIG_ATOM_STRING: {
            char *endptr = NULL;
            if (FAILED(ret = _config_parse_memory_size(atm.atom_string, &endptr, val))) {
                DIAG("Failed to parse memory size from key '%s'", item_id);
                goto done;
            }
        }
        break;
    case CONFIG_ATOM_INTEGER:
        *val = atm.atom_integer;
        break;
    default:
        ret = A_E_INVAL;
    }

done:
    return ret;
}

static
aresult_t _config_parse_time_string(const char *str, char **end, uint64_t *val)
{
    aresult_t ret = A_OK;

    char *endptr = NULL;
    uint64_t n = 0;

    n = strtoull(str, &endptr, 0);

    /* If n is 0 or the endptr points to a null terminator, we're done our work. */
    if (0 == n || *endptr == '\0') {
        goto done;
    }

    switch (*endptr++) {
    case 'n':
        break;
    case 'u':
        n *= 1000ull;
        break;
    case 'm':
        n *= 1000000ull;
        break;
    case 's':
        n *= 1000000000ull;
        break;
    default:
        ret = A_E_INVAL;
        goto done;
    }

    if ('\0' != *endptr && 's' != *endptr) {
        DIAG("Endptr = %c", *endptr);
        ret = A_E_INVAL;
        goto done;
    }

    *val = n;

done:
    return ret;
}

aresult_t config_get_time_interval(struct config *cfg, uint64_t *val_ns, const char *item_id)
{
    aresult_t ret = A_OK;
    struct config atm;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != val_ns);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get(cfg, &atm, item_id))) {
        goto done;
    }

    switch (atm.atom_type) {
    case CONFIG_ATOM_STRING: {
            char *endptr = NULL;
            if (FAILED(ret = _config_parse_time_string(atm.atom_string, &endptr, val_ns))) {
                DIAG("Failed to parse time interval from key '%s'", item_id);
                goto done;
            }
        }
        break;
    case CONFIG_ATOM_INTEGER:
        *val_ns = atm.atom_integer;
        break;
    default:
        ret = A_E_INVAL;
    }

done:
    return ret;
}

static
aresult_t _config_parse_sockaddr(const char *ip_string, struct sockaddr *saddr, size_t *plen)
{
    aresult_t ret = A_OK;

    const char *addr_string = NULL,
               *port_string = NULL;
    struct addrinfo ai_hints;
    struct addrinfo *ainfo = NULL,
                    *np = NULL;
    int ai_ret = 0;

    TSL_ASSERT_ARG(NULL != ip_string);
    TSL_ASSERT_ARG(NULL != saddr);
    TSL_ASSERT_ARG(NULL != plen);
    TSL_ASSERT_ARG(0 != *plen);

    if (NULL == (port_string = strrchr(ip_string, ':'))) {
        DIAG("Could not find colon separating port/service from address");
        ret = A_E_INVAL;
        goto done;
    }

    if (port_string == ip_string) {
        DIAG("0-length string for address");
        ret = A_E_INVAL;
        goto done;
    }

    if (NULL == (addr_string = strndup(ip_string, port_string - ip_string))) {
        ret = A_E_NOMEM;
        goto done;
    }

    port_string++;

    if (*port_string == '\0') {
        DIAG("0-length string for service ID");
        ret = A_E_INVAL;
        goto done;
    }

    DIAG("Looking up: '%s' - service '%s'", addr_string, port_string);

    memset(&ai_hints, 0, sizeof(ai_hints));

    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = 0;
    ai_hints.ai_flags = 0;
    ai_hints.ai_protocol = 0;

    if (0 != (ai_ret = getaddrinfo(addr_string, port_string, &ai_hints, &ainfo))) {
        DIAG("An error occurred while resolving %s:%s - %s", addr_string, port_string, gai_strerror(ai_ret));
        ret = A_E_INVAL;
        goto done;
    }

    /* TODO: clean this up to properly check all results */
    for (np = ainfo; np != NULL; np = np->ai_next) {
        if (np->ai_addrlen > *plen) {
            DIAG("Length of result is %u, provided sockaddr len is %zu",
                    (unsigned)np->ai_addrlen, *plen);
            ret = A_E_INVAL;
            goto done;
        }

        memcpy(saddr, np->ai_addr, np->ai_addrlen);

        *plen = np->ai_addrlen;
    }

done:
    TFREE(addr_string);

    if (NULL != ainfo) {
        freeaddrinfo(ainfo);
        ainfo = NULL;
    }

    return ret;
}

aresult_t config_get_sockaddr(struct config *cfg, struct sockaddr *saddr, size_t *plen, const char *item_id)
{
    aresult_t ret = A_OK;

    const char *ip_string = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != saddr);
    TSL_ASSERT_ARG(NULL != plen);
    TSL_ASSERT_ARG(0 != *plen);
    TSL_ASSERT_ARG(NULL != item_id);
    TSL_ASSERT_ARG('\0' != *item_id);

    if (FAILED(ret = config_get_string(cfg, &ip_string, item_id))) {
        goto done;
    }

    ret = _config_parse_sockaddr(ip_string, saddr, plen);

done:
    return ret;
}

aresult_t config_array_at_sockaddr(struct config *cfg, struct sockaddr *saddr, size_t *plen, size_t index)
{
    aresult_t ret = A_OK;

    const char *ip_string = NULL;

    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != saddr);
    TSL_ASSERT_ARG(NULL != plen);
    TSL_ASSERT_ARG(0 != *plen);

    if (FAILED(ret = config_array_at_string(cfg, &ip_string, index))) {
        goto done;
    }

    ret = _config_parse_sockaddr(ip_string, saddr, plen);
done:
    return ret;
}
