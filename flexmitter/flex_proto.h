#pragma once

#include <tsl/result.h>
#include <tsl/work_queue.h>

struct config;

struct flex_proto {
    struct work_queue wq;
    struct frame_alloc *fa;
    int fd;
};

aresult_t flex_proto_init(struct flex_proto *proto, struct config *cfg);
aresult_t flex_proto_cleanup(struct flex_proto *proto);

aresult_t flex_proto_start(struct flex_proto *proto);

