#include <app/app.h>
#include <app/cpufeatures.h>

#include <config/engine.h>

#include <tsl/alloc.h>
#include <tsl/cpumask.h>
#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/panic.h>
#include <tsl/time.h>
#include <tsl/version.h>

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_STATE_RUNNING               0
#define APP_STATE_SHUTDOWN_REQUESTED    1
#define APP_STATE_SHUTDOWN_FORCED       2

#define CONFIG_ALLOC_NR_HUGE_SLABS          "nrHugeSlabs"
#define CONFIG_ALLOC_NR_SLABS               "nrSlabs"
#define CONFIG_ALLOC_HUGE_PAGE_SIZE         "hugePageSize"

#ifndef NR_HUGE_SLABS
#define NR_HUGE_SLABS 32
#endif

#ifndef NR_NORMAL_SLABS
#define NR_NORMAL_SLABS 512
#endif

#define APP_MSG(sev, sys, msg, ...) MESSAGE("APP", sev, sys, msg, ##__VA_ARGS__)

extern int slab_manager_initialized;

static volatile
int __app_state = APP_STATE_RUNNING;

static
const char *app_name_str = NULL;

static
app_sigint_handler_t __app_sigint_handler = NULL;

static
LIST_HEAD(__app_sigusr2_handlers);

static
void buserr_handler(int signal, siginfo_t *info, void *context)
{
    void *symbols[20];
    size_t len = 0;
    len = backtrace(symbols, BL_ARRAY_ENTRIES(symbols));
    printf("\nbus error - backtracing %zu frames.\n", len);
    printf("Faulting address: %p cause: ", (void *)info->si_addr);
    switch (info->si_code) {
    case BUS_ADRALN:
        printf("incorrect memory alignment\n");
        break;
    case BUS_ADRERR:
        printf("nonexistent physical address\n");
        break;
    case BUS_OBJERR:
        printf("object error (hardware)\n");
        break;
    default:
        printf("unknown (si_code = %d)\n", info->si_code);
    }

    backtrace_symbols_fd(symbols, len, STDERR_FILENO);
    printf("aborting.\n");
    abort();

}

/**
 * Dump the backtrace for diagnostic purposes.
 */
static
void segv_handler(int signal, siginfo_t *info, void *context)
{
    void *symbols[20];
    size_t len = 0;
    len = backtrace(symbols, BL_ARRAY_ENTRIES(symbols));
    printf("\nsegmentation fault - backtracing %zu frames.\n", len);
    printf("Faulting address: %p cause: ", (void *)info->si_addr);
    printf("%s (%d)\n",
            info->si_code == SEGV_MAPERR ? "Map Error" :
            info->si_code == SEGV_ACCERR ? "Access Permissions Error" :
                "Unknown/Unspecified", info->si_code);

    backtrace_symbols_fd(symbols, len, STDERR_FILENO);
    printf("aborting.\n");
    abort();
}

static
void app_sigint_handler(int signal)
{
    __app_state++;

    DIAG("Interrupt signal received.");

    if (__app_state >= APP_STATE_SHUTDOWN_FORCED) {
        PANIC("User insisted that application terminate. Aborting.");
    }

    if (NULL != __app_sigint_handler) {
        __app_sigint_handler();
    }
}

static
void app_sigusr2_handler(int signal)
{
    DIAG("SIGUSR2 received.");

    struct app_sigusr2_state_t *iterator;
    list_for_each_type(iterator, &__app_sigusr2_handlers, handlers) {
        iterator->handler();
    }
}

static
aresult_t _segv_handler_install(struct config *cfg)
{
    aresult_t ret = A_OK;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO;

    if (0 > sigaction(SIGSEGV, &sa, NULL)) {
        PDIAG("Failed to install SEGV handler.");
        ret = A_E_INVAL;
        goto done;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = buserr_handler;
    sa.sa_flags = SA_SIGINFO;

    if (0 > sigaction(SIGBUS, &sa, NULL)) {
        PDIAG("Failed to install SEGV handler.");
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

APP_SUBSYSTEM(sigsegv, _segv_handler_install, NULL);

int app_running(void)
{
    return (__app_state == APP_STATE_RUNNING);
}

aresult_t app_sigint_catch(app_sigint_handler_t hdlr)
{
    aresult_t ret = A_OK;
    struct sigaction sa;

    /* XXX: missing TSL_ASSERT_ARG on hdler != NULL? */

    __app_sigint_handler = hdlr;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = app_sigint_handler;

    if (0 > sigaction(SIGINT, &sa, NULL)) {
        PDIAG("Failed to install SIGINT handler.");
        ret = A_E_INVAL;
        goto done;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = app_sigint_handler;

    if (0 > sigaction(SIGTERM, &sa, NULL)) {
        PDIAG("Failed to install SIGTERM handler.");
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

aresult_t app_sigusr2_catch(struct app_sigusr2_state_t *handler_state)
{
    aresult_t ret = A_OK;
    bool empty = list_empty(&__app_sigusr2_handlers);
    struct sigaction sa;

    TSL_ASSERT_ARG(NULL != handler_state);
    TSL_ASSERT_ARG(NULL != handler_state->handler);

    list_append(&__app_sigusr2_handlers, &handler_state->handlers);

    /* Only setup if there were no handlers yet */
    if (empty) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = app_sigusr2_handler;

        if (0 > sigaction(SIGUSR2, &sa, NULL)) {
            PDIAG("Failed to install SIGUSR2 handler.");
            ret = A_E_INVAL;
            goto done;
        }
    }

done:
    return ret;
}

/**
 * Daemonize the current application. Fork the application and terminate the parent
 * thread.
 * \return A_OK on success, an error code otherwise.
 */
aresult_t app_daemonize(void)
{
    aresult_t ret = A_OK;
    pid_t proc_id = 0;
    pid_t session_id = 0;

    proc_id = fork();

    if (proc_id < 0) {
        PDIAG("Unable to fork(2) process from parent.");
        ret = A_E_UNKNOWN;
        goto done;
    }

    if (proc_id > 0) {
        /* Terminate the invoking process */
        exit(EXIT_SUCCESS);
    }

    session_id = setsid();

    if (session_id < 0) {
        PDIAG("Failed to set session ID");
        ret = A_E_INVAL;
        goto done;
    }

    /* Set working directory to the root of the filesystem */
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

done:
    return ret;
}

/**
 * Bind app to a specified CPU core
 * \param core_id the CPU core to bind to
 * \return A_OK on success, an error code otherwise
 */
aresult_t app_bind_cpu_core(int core_id)
{
    aresult_t ret = A_OK;
    struct cpu_mask *msk = NULL;

    TSL_ASSERT_ARG(core_id >= 0); /* XXX: should we just use an unsigned type? */

    if (FAILED(ret = cpu_mask_new(&msk))) {
        goto done;
    }

    if (FAILED(ret = cpu_mask_set(msk, core_id))) {
        goto done;
    }

    if (FAILED(ret = cpu_mask_apply(msk))) {
        goto done;
    }

    if (FAILED(ret = cpu_mask_delete(&msk))) {
        goto done;
    }

done:
    return ret;
}

/**
 * Redirect any diagnostic outputs to a file for later consumption.
 * \param file_name the name of the file to redirect to
 * \return A_OK on success, A_E_INVAL if file could not be opened.
 */
aresult_t app_set_diag_output(const char *file_name)
{
    aresult_t ret = A_OK;
    FILE *reopen = NULL;

    TSL_ASSERT_ARG(NULL != file_name);
    TSL_ASSERT_ARG('\0' != *file_name);

    if (NULL == (reopen = freopen(file_name, "a+", stdout))) {
        PDIAG("Failed to redirect diag output. Aborting.");
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

aresult_t app_init(const char *app_name, struct config *cfg)
{
    struct app_subsystem *subsys = NULL;

    TSL_ASSERT_ARG(NULL != app_name);
    TSL_ASSERT_ARG('\0' != *app_name);

    app_name_str = app_name;

    signal(SIGPIPE, SIG_IGN);

    TSL_BUG_IF_FAILED(app_cpufeatures_check_at_init());

    DIAG("Build version: %s", tsl_get_version());

    CR_FOR_EACH_LOADABLE(subsys, __dynamic_subsystems) {
        if (!subsys->init) {
            continue;
        }

        DIAG("Initializing '%s' subsystem...", subsys->name);
        if (FAILED(subsys->init(cfg))) {
            PANIC("Failed to initialize subsystem '%s'", subsys->name);
        }
    }

    return A_OK;
}

/** \brief Initialize the allocator subsystem
 * Initialize the allocator subsystem to make slabs available to the application
 */
aresult_t app_allocator_init(struct config *cfg)
{
    aresult_t ret = A_OK;
    char *a_nr_pages = NULL;
    char *a_nr_huge_pages = NULL;
    int nr_pages = 0;
    int nr_huge_pages = 0;
    size_t huge_page_size = 2 * 1024 * 1024;
    long page_size = sysconf(_SC_PAGESIZE);

    if (slab_manager_initialized) {
        DIAG("Slab manager was already initialized, skipping.");
        return A_OK;
    }

    if (page_size < 1) {
        PANIC("System page size returned %d, aborting.", page_size);
    }

    /* Legacy configuration using environment variables, if config is not specified */
    if ( (NULL == cfg) ) {
        DIAG("Using legacy allocator configuration mechanism; you should specify a config");
        if ((a_nr_pages = getenv("TSL_NR_SLABS")) != NULL) {
            nr_pages = atoi(a_nr_pages);
        }

        if ((a_nr_huge_pages = getenv("TSL_NR_HUGE_SLABS")) != NULL) {
            nr_huge_pages = atoi(a_nr_huge_pages);
        }
    } else {
        if (FAILED(config_get_integer(cfg, &nr_pages, CONFIG_ALLOC_NR_SLABS))) {
            nr_pages = 0;
        }

        if (FAILED(config_get_integer(cfg, &nr_huge_pages, CONFIG_ALLOC_NR_HUGE_SLABS))) {
            nr_huge_pages = 0;
        }

        if (FAILED(config_get_byte_size(cfg, &huge_page_size, CONFIG_ALLOC_HUGE_PAGE_SIZE))) {
            APP_MSG(SEV_WARNING, "NO-HUGE-PAGE-SIZE-SPECIFIED", "Defaulting huge slab size to 2MB.");
        }
    }

    if ((0 > nr_pages) || (0 > nr_huge_pages) || 0 == huge_page_size) {
        PANIC("Invalid allocator parameters. SlabCount = %d, NrHugeSlabs = %d, HugePageSize = %zu", nr_pages, nr_huge_pages, huge_page_size);
    }

    DIAG("Memory Allocator Subsystem: %d normal slabs, %d, huge page size = %zu", nr_pages, nr_huge_pages, huge_page_size);

    if (0 == nr_pages && 0 == nr_huge_pages) {
        DIAG("Warning: memory allocation subsystem is disabled.");
    }

    if (FAILED(ret = allocator_system_init(nr_pages, page_size, nr_huge_pages, huge_page_size))) {
        PANIC("Unable to initialize the allocator subsystem.");
    }

    return ret;
}
APP_SUBSYSTEM(allocator, app_allocator_init, NULL);

static
aresult_t tsl_time_subsys_init(struct config *cfg)
{
    aresult_t ret = A_OK;

    ret = tsl_time_init();

    return ret;
}
APP_SUBSYSTEM(tsltimer, tsl_time_subsys_init, NULL);

