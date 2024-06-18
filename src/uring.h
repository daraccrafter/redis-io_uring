
#ifndef URING_H // Check if URING_H has been defined
#define URING_H
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "liburing.h"
#pragma GCC diagnostic pop
#include "sds.h"
#include "sdsalloc.h"
#include <pthread.h>

#define QUEUE_DEPTH_XS 1024
#define QUEUE_DEPTH_S 2048
#define QUEUE_DEPTH_M 4096
#define QUEUE_DEPTH_L 8192
#define QUEUE_DEPTH_XL 16384
#define QUEUE_DEPTH_XXL 32768

#define MAX_RETRY_XS 3
#define MAX_RETRY_S 10
#define MAX_RETRY_M 50
#define MAX_RETRY_L 100
#define MAX_RETRY_XL 500
#define MAX_RETRY_XXL 1000

#define CQE_BATCH_SIZE(queue_depth) ((queue_depth) / 10)

#define OP_SHIFT 56
#define LEN_SHIFT 32
#define PTR_SHIFT 0
#define OP_MASK 0xFF
#define LEN_MASK 0xFFFFFF
#define PTR_MASK 0xFFFFFFFFFF

enum Operation
{
    WRITE_URING,
    FSYNC_URING,
};
typedef struct
{
    uint8_t op;
    size_t len;
    void *buf_ptr;
    size_t write_offset;
    long long aof_file_incr;

} OperationData;

typedef struct
{
    struct io_uring *ring;
    int cqe_batch_size;
    int *fd;
    int *fd_noappend;
    long long *aof_increment;
} CompletionThreadArgs;

typedef struct
{
    struct io_uring *ring;
    int fd;
    int MAX_RETRY;
    off_t write_offset;
    long long aof_file_incr;
} UringArgs;
struct io_uring setup_aof_io_uring(int QUEUE_DEPTH);
void process_completions(void *args);
int aofFsyncUring(int fd, struct io_uring *ring, int MAX_RETRY);
ssize_t aofWriteUring(int fd, const char *buf, size_t len, UringArgs args);
#endif // End of the include guard