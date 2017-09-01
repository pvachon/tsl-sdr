#include <flexmitter/flexmitter.h>
#include <flexmitter/tx_thread.h>
#include <flexmitter/flex_proto.h>

#include <app/app.h>

#include <config/engine.h>

#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>

#include <unistd.h>

static
void _usage(const char *name)
{
    fprintf(stderr, "usage: %s [Config File 1]{, Config File 2, ...} | %s -h\n", name, name);
}

int main(int argc, char * const argv[])
{
    int ret = EXIT_FAILURE;

    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    struct tx_thread *thr CAL_CLEANUP(tx_thread_delete) = NULL;
    struct flex_proto proto CAL_CLEANUP(flex_proto_cleanup);

    if (argc < 2) {
        _usage(argv[0]);
        goto done;
    }

    memset(&proto, 0, sizeof(proto));

    /* Parse and load the configurations from the command line */
    TSL_BUG_IF_FAILED(config_new(&cfg));

    for (int i = 1; i < argc; i++) {
        if (FAILED(config_add(cfg, argv[i]))) {
            FLX_MSG(SEV_FATAL, "MALFORMED-CONFIG", "Configuration file [%s] is malformed.", argv[i]);
            goto done;
        }
        DIAG("Added configuration file '%s'", argv[i]);
    }

    /* Application Framework */
    TSL_BUG_IF_FAILED(app_init("flexmitter", NULL));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    /* Create the FLEX protocol handler */
    TSL_BUG_IF_FAILED(flex_proto_init(&proto, cfg));

    /* Create the transmitter thread */
    TSL_BUG_IF_FAILED(tx_thread_new(&thr, cfg));

    /* Kick off the protocol handler */
    TSL_BUG_IF_FAILED(flex_proto_start(&proto));

done:
    return ret;
}

