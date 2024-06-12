
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "liburing.h"
#pragma GCC diagnostic pop

#define QUEUE_DEPTH 8096
#define MAX_RETRY 100

struct io_uring setup_io_uring(void);
void process_completions(struct io_uring *ring);
int aofFsyncUring(int fd, struct io_uring *ring);
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring);
