#pragma once

#include <multifm/receiver.h>

#include <tsl/result.h>

struct sample_buf;
struct file_worker_thread;

typedef aresult_t (*file_read_convert_call_func_t)(struct file_worker_thread *thr, struct sample_buf *sbuf);

enum file_worker_sample_format {
    FILE_WORKER_SAMPLE_FORMAT_UNKNOWN = 0,
    FILE_WORKER_SAMPLE_FORMAT_S8,   /* Signed 8-bit integer input */
    FILE_WORKER_SAMPLE_FORMAT_U8,   /* Unsigned 8-bit integer input */
    FILE_WORKER_SAMPLE_FORMAT_S16,  /* Signed 16-bit integer input */
};

struct file_worker_thread {
    struct receiver rcvr;

    int fd;

    long samples_per_sec;
    uint64_t time_per_buf_ns;
    enum file_worker_sample_format sample_format;

    file_read_convert_call_func_t read_call;
    void *bounce_buf;
    size_t bounce_buf_bytes;
};

#define FL_MSG(sev, sys, msg, ...)      MESSAGE("FILEIF", sev, sys, msg, ##__VA_ARGS__)

