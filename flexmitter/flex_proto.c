#include <flexmitter/flexmitter.h>
#include <flexmitter/flex_proto.h>

#include <app/app.h>

#include <config/engine.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/frame_alloc.h>

#include <jansson.h>

aresult_t flex_proto_init(struct flex_proto *proto, struct config *cfg)
{
    aresult_t ret = A_OK;

    const char *msg_src = NULL;

    TSL_ASSERT_ARG(NULL != proto);
    TSL_ASSERT_ARG(NULL != cfg);

    TSL_BUG_ON(NULL != proto->fa);

    if (FAILED(ret = config_get_string(cfg, &msg_src, "messageSource"))) {
        FLX_MSG(SEV_FATAL, "FLEX-PROTO", "Missing messageSource from configuration.");
        goto done;
    }

    if (FAILED(ret = frame_alloc_new(&proto->fa, 8192, 512))) {
        goto done;
    }

done:
    return ret;
}

aresult_t flex_proto_cleanup(struct flex_proto *proto)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != proto);

    TSL_BUG_IF_FAILED(work_queue_release(&proto->wq));
    TSL_BUG_IF_FAILED(frame_alloc_delete(&proto->fa));

    return ret;
}

aresult_t flex_proto_start(struct flex_proto *proto)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != proto);

    do {
        /* Read a line from stdin */

        /* Parse it as JSON */

        /* Encode the message as raw bits */

    } while (app_running());

    return ret;
}

