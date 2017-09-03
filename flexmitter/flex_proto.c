#include <flexmitter/flexmitter.h>
#include <flexmitter/flex_proto.h>

#include <app/app.h>

#include <config/engine.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/frame_alloc.h>

#include <jansson.h>

#include <math.h>

aresult_t flex_proto_init(struct flex_proto *proto, struct config *cfg)
{
    aresult_t ret = A_OK;

    const char *msg_src = NULL;
    double sensitivity = 0.0;

    TSL_ASSERT_ARG(NULL != proto);
    TSL_ASSERT_ARG(NULL != cfg);

    TSL_BUG_ON(NULL != proto->fa);

    if (FAILED(ret = config_get_string(cfg, &msg_src, "messageSource"))) {
        FLX_MSG(SEV_FATAL, "FLEX-PROTO", "Missing messageSource from configuration.");
        goto done;
    }

    if (FAILED(ret = config_get_float(cfg, &sensitivity, "sensitivity"))) {
        FLX_MSG(SEV_FATAL, "FLEX-PROTO", "Missing sensitivity, aborting.");
        goto done;
    }

    if (FAILED(ret = frame_alloc_new(&proto->fa, 8192, FLEX_PROTO_NR_SAMPLES * 2 * sizeof(int16_t)))) {
        goto done;
    }

    proto->chunk = 1;
    proto->phase = 0.0;

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

/*
 * Stupid test function to generate a square wave.
 */
static
aresult_t _flex_proto_generate_square(struct flex_proto *proto)
{
    aresult_t ret = A_OK;

    int16_t *sbuf = NULL;

    TSL_ASSERT_ARG(NULL != proto);

    TSL_BUG_IF_FAILED(frame_alloc(proto->fa, (void **)&sbuf));

    for (int i = 0; i < FLEX_PROTO_NR_SAMPLES; i++) {
        int16_t s_real = 0,
                s_imag = 0;
        float f_real = 0.0,
              f_imag = 0.0;

        if (++(proto->ctr) == 10) {
            proto->ctr = 0;
            proto->chunk = -proto->chunk;
        }

        proto->phase += proto->sensitivity * proto->chunk;
        proto->phase = fmodf(proto->phase + (float)M_PI, 2.0f * (float)M_PI);

        sincosf(proto->phase, &f_imag, &f_real);

        s_real = (float)(1 << 14) * f_real;
        s_imag = (float)(1 << 14) * f_imag;

        sbuf[2 * i    ] = s_real;
        sbuf[2 * i + 1] = s_imag;
    }

    TSL_BUG_IF_FAILED(work_queue_push(&proto->wq, sbuf));

    return ret;
}

aresult_t flex_proto_start(struct flex_proto *proto)
{
    aresult_t ret = A_OK;

    unsigned int wq_size = 0;

    TSL_ASSERT_ARG(NULL != proto);
    TSL_BUG_IF_FAILED(work_queue_size(&proto->wq, &wq_size));

    do {
#if 0
        /* Read a line from stdin */

        /* Parse it as JSON */

        /* Encode the message as raw bits */
#endif
        unsigned int nr_entries = 0;
        TSL_BUG_IF_FAILED(work_queue_fill(&proto->wq, &nr_entries));

        if (nr_entries + 1 < wq_size - 1) {
            DIAG("Pushing in some waves, yo.");
            TSL_BUG_IF_FAILED(_flex_proto_generate_square(proto));
        }
    } while (app_running());

    return ret;
}

