
#ifndef URING_H // Check if URING_H has been defined
#define URING_H
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "liburing.h"
#pragma GCC diagnostic pop
#include "sds.h"
#include "sdsalloc.h"

#define QUEUE_DEPTH 4096
#define MAX_RETRY 100

#define OP_MASK 0xFF
#define LEN_MASK 0xFFFFFFFFF
#define PTR_MASK 0xFFFFFFFFF

#define OP_SHIFT 56
#define LEN_SHIFT 28
#define PTR_SHIFT 0

enum Operation
{
    WRITE_URING,
    FSYNC_URING,
};
typedef struct __attribute__((__packed__))
{
    uint8_t op;
    uintptr_t buf_ptr;
    size_t len;
} OperationData;
struct io_uring setup_io_uring(void);
void process_completions(struct io_uring *ring);
int aofFsyncUring(int fd, struct io_uring *ring);
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring);
#endif // End of the include guard