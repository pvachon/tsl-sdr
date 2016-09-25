#pragma once

#include <tsl/cal.h>
#include <tsl/result.h>
#include <tsl/safe_alloc.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct sockaddr;

/** \file config/engine.h Configuration Engine
 *
 */

/**
 * The type of the value in a configuration atom
 */
enum config_atom_type {
    CONFIG_ATOM_UNINITIALIZED = 0,
    CONFIG_ATOM_INTEGER,
    CONFIG_ATOM_STRING,
    CONFIG_ATOM_ARRAY,
    CONFIG_ATOM_NESTED,
    CONFIG_ATOM_BOOLEAN,
    CONFIG_ATOM_FLOAT,
    CONFIG_ATOM_NULL
};

/**
 * An augmented array type. If this is set, the contained object is an array,
 * and the size will be set to the number of items in the array. The config
 * object can then be acted upon with the array iterators.
 */
struct config_array {
    void *array;
    size_t length;
};

/**
 * Structure defining a configuration atom. These are passed in by the
 * caller to a configuration getter/setter, and are solely the responsibility
 * of the caller to manage the lifecycle of. Typically the object can be
 * passed on stack to a getter/setter function without any sort of allocation.
 *
 * Config items are treated as a tree of config objects. Helper functions are
 * provided to access members of a config tree using the specified type cast,
 * but these assume you're operating on a leaf of the tree.
 */
struct config {
    enum config_atom_type atom_type;
    union {
        char *atom_string;
        struct config_array atom_array;
        void *atom_nested;
        int atom_integer;
        bool atom_bool;
        float atom_float;
    };
};

#define CONFIG_INIT_EMPTY           (struct config) { .atom_type = CONFIG_ATOM_UNINITIALIZED, .atom_nested = NULL }

aresult_t config_get_type(struct config *cfg, enum config_atom_type *type);

/**
 * Return the length of an array atom, if the specified atom is an array.
 * \param atm The atom to query
 * \param len The length, by reference
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_array_length(struct config *array, size_t *length);

/**
 * Return the value of an array at the given location
 * \param array The array to query
 * \param item The atom to receive the array item
 * \param index The index of the array item
 */
aresult_t config_array_at(struct config *array, struct config *item, size_t index);
aresult_t config_array_at_integer(struct config *array, int *item, size_t index);
aresult_t config_array_at_size(struct config *array, size_t *item, size_t index);
aresult_t config_array_at_string(struct config *array, const char **item, size_t index);
aresult_t config_array_at_sockaddr(struct config *array, struct sockaddr *saddr, size_t *plen, size_t index);

/**
 * Retrieve the item by type (substitutes the appropriate function call) for arrays.
 *
 * Do not use this outside of this header.
 *
 * Note: If a type is unsupported, this function will fail due to malformed syntax.
 */
#define CONFIG_ARRAY_GET_AS(__cfg, __dst, __offs) (_Generic((__dst), int : config_array_at_integer,          \
                                                                     const char * : config_array_at_string,  \
                                                                     struct config : config_array_at,      \
                                                                     default : PANIC("Nope."))((__cfg), &(__dst), (__offs)))

/**
 * Iterate through all items in the specified configuration array. Requires:
 *
 * \param __val The destination value (an integer, a string or a config object)
 * \param __arr The wrapped configuration array object
 * \param __ret The return value for the current context. If an error occurs during iteration,
 *              the iteration will break and a return value will be specified.
 * \param __ctr The counter. Gives you the offset of the object. Also set to the failing value
 *              if iteration fails.
 *
 * \note Use this like you would use a for loop body declaration.
 */
#define CONFIG_ARRAY_FOR_EACH(__val, __arr, __ret, __ctr) \
    for ((__ctr) = 0, (__ret) = A_E_INVAL;                                          \
            (__arr)->atom_type == CONFIG_ATOM_ARRAY &&                              \
            (__ctr) < (__arr)->atom_array.length &&                                 \
            !FAILED((__ret) = CONFIG_ARRAY_GET_AS((__arr), (__val), (__ctr)));     \
            (__ctr)++)

/**
 * Return the length of an array atom, if the specified atom is an array.
 * \param cfg The atom to query
 * \param pvals The pointer to the point which will accept the allocated array (YOU MUST TFREE THIS!).
 * \param length The length, by reference
 * \param item_id The ID of the config atom to parse this from
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_get_integer_array(struct config *cfg, int **pvals, size_t *length, const char *item_id);

/**
 * Return the length of an array atom, if the specified atom is an array.
 * \param cfg The atom to query
 * \param pvals The pointer to the point which will accept the allocated array (YOU MUST TFREE THIS!).
 * \param length The length, by reference
 * \param item_id The ID of the config atom to parse this from
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_get_size_array(struct config *cfg, size_t **pvals, size_t *length, const char *item_id);

aresult_t config_get_integer(struct config *cfg, int *val, const char *item_id);
aresult_t config_get_size(struct config *cfg, size_t *val, const char *item_id);
aresult_t config_get_string(struct config *cfg, const char **val, const char *item_id);
aresult_t config_get_boolean(struct config *cfg, bool *val, const char *item_id);
aresult_t config_get_sockaddr(struct config *cfg, struct sockaddr *saddr, size_t *plen, const char *item_id);

/**
 * For the key item_id, treat the value as a size indicator. This can either be a string with
 * a suffix of G for gigabytes, M for Megabytes, K for kilobytes, etc.,  or an integer specifying
 * the size in bytes of the field. Returns the appropriately handled value back to the caller.
 *
 * \param cfg The configuration to get the value out of
 * \param val The parsed value, returned in bytes
 * \param item_id The ID of the config atom to parse this from
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_get_byte_size(struct config *cfg, uint64_t *val, const char *item_id);

/**
 * For the key item_id, treat the value as a time indicator. This can either be a string with
 * an integer plus a suffix of s seconds, ms for milliseconds, us for microseconds, no suffix
 * for nanoseconds, or an integer to represent a value in nanoseconds.
 *
 * \param cfg The configuration to get the value out of.
 * \param val_ns The parsed value, in nanoseconds.
 * \param item_id The ID of the config atom to parse this from
 */
aresult_t config_get_time_interval(struct config *cfg, uint64_t *val_ns, const char *item_id);

/**
 * Get an item from the specified configuration object.
 * \param cfg Configuration object.
 * \param atm The atom to receive the item information
 * \param item_id The DOM-style name of the item.
 * \return A_OK on success, A_E_NOT_FOUND if item_id does not exist, an error code otherwise
 */
aresult_t config_get(struct config *cfg, struct config *atm, const char *item_id);

/**
 * Create a new configuration engine storage object
 * \param pcfg Pointer to receive the new configuration object.
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_new(struct config **cfg);

/**
 * Parse the given configuration file and add it to the configuration
 * structure.
 * \param cfg The configuration object to add the config to
 * \param filename The file to parse the configuration from
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_add(struct config *cfg, const char *filename);

/**
 * Sets the directory config_add_system_config pulls from (default to the contents of the
 * evironment variable TSL_CONFIG or the compile-time defined default if its empty).
 * \param directory The file to parse the configuration from
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_set_system_config_directory(const char *directory);

/**
 * Parse the given configuration file from the system config cache and add it to the configuration
 * structure. The location of the system config cache defaults to /opt/12sided/etc but may be
 * changed by setting the TWELVE_CONFIG environment variable.
 * \param cfg The configuration object to add the config to
 * \param name The name of the configuration (not a file name, just a name, like "machine")
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_add_system_config(struct config *cfg, const char *name);

/**
 * Parse the given array of configuration files and add them to the configuration
 * structure (e.g. for use with argv+1, argc-1)
 * \param cfg The configuration object to add the config to
 * \param filenames The array of file names
 * \param nr_filenames The number of file names
 * \return A_OK on success, an error code otherwise
 */
aresult_t config_add_array(struct config *cfg, const char **filenames, size_t nr_filenames);

/**
 * Given a JSON string, parse and merge the JSON string into the given
 * configuration atom.
 *
 * \param cfg A nested config atom that can accept new keys
 * \param json_string The string of JSON to be parsed and added.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t config_add_string(struct config *cfg, const char *json_string);

/**
 * Serialize the current configuration and return it as a string on the
 * heap.
 * \param cfg The configuration to serialize
 * \param config The configuration returned as a serialized JSON object
 * \return A_OK on success, an error code otherwise
 * \note It is the caller's responsibility to free the string. Free it
 *       using standard free(3)
 */
aresult_t config_serialize(struct config *cfg, char **config);

/**
 * Destroy the given configuration object
 */
aresult_t config_delete(struct config **pcfg);
