#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

#include <tsl/list.h>
#include <tsl/result.h>
#include <tsl/sections.h>

struct config;

/**
 * Function pointer for function to be called to initialize an app subsystem.
 */
typedef aresult_t (*app_subsystem_init_func_t)(struct config *cfg);

/**
 * Function pointer for a function to be called to shut down an app subsystem.
 */
typedef aresult_t (*app_subsystem_shutdown_func_t)(void);

/**
 * Declaration of a dynamic compilation application subsystem.
 */
struct app_subsystem {
    const char *name;
    app_subsystem_init_func_t init;
    app_subsystem_shutdown_func_t shutdown;
};

/**
 * Declare a new app subsystem and make it available at run-time
 */
#define APP_SUBSYSTEM(__name, __init_func, __shutdown_func) \
    static struct app_subsystem __name ## _subsystem_decl = {           \
        .name = #__name,                                                \
        .init = (__init_func),                                          \
        .shutdown = (__shutdown_func)                                   \
    };                                                                  \
    CR_LOADABLE(__dynamic_subsystems, __name ## _subsystem_decl);

/**
 * Initialize all dynamic subsystems of an application
 */
aresult_t app_init(const char *app_name, struct config *cfg);

typedef aresult_t (*app_sigint_handler_t)(void);

/**
 * Attach a SIGINT handler so shutdown can be done gracefully
 * \param delegate Function that is delegated to on SIGINT's arrival
 * \return A_OK on success, an error code otherwise
 * \note Use app_running() predicate function to check if a halt has been signaled.
 */
aresult_t app_sigint_catch(app_sigint_handler_t hdlr);

typedef aresult_t (*app_sigusr2_handler_t)(void);
struct app_sigusr2_state_t {
    struct list_entry handlers;
    app_sigusr2_handler_t handler;
};

/**
 * Attach an additional SIGUSR2 handler, for generic app signaling. Note that
 * unlike SIGINT, this allows for multiple handlers.
 * \param delegate Function that is delegated to on SIGUSR2's arrival
 * \return A_OK on success, an error code otherwise
 * \note Use app_running() predicate function to check if a halt has been signaled.
 */
aresult_t app_sigusr2_catch(struct app_sigusr2_state_t *handler_state);

/**
 * Predicate to check if a shutdown has been requested
 * \return 1 on app is to be running, 0 when app is to shutdown
 */
int app_running(void);

/**
 * Get the name (string) of the running application
 */
aresult_t app_get_name(const char **app_name);

aresult_t app_daemonize(void);
aresult_t app_set_diag_output(const char *file_name);
aresult_t app_bind_cpu_core(int core_id);

/**
 * Initialize the allocator subsystem
 */
aresult_t app_allocator_init(struct config* cfg);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
