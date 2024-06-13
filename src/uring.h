
#ifndef URING_H // Check if URING_H has been defined
#define URING_H
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "liburing.h"
#pragma GCC diagnostic pop

#define QUEUE_DEPTH 20000
#define MAX_RETRY 100
enum Operation
{
    WRITE_URING,
    FSYNC_URING,
};
typedef struct
{
    enum Operation op;
    uintptr_t buf_ptr;
    ssize_t len;
 
} OperationData;
struct io_uring setup_io_uring(void);
void process_completions(struct io_uring *ring);
int aofFsyncUring(int fd, struct io_uring *ring);
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring);
#endif // End of the include guard