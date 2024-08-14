
#ifndef URING_H
#define URING_H
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "liburing.h"
#pragma GCC diagnostic pop
#include "sds.h"
#include "sdsalloc.h"
#include <unistd.h>
#include <string.h>
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

// Log levels
#define LL_NOTICE 2
#define LL_WARNING 3

#define CQE_BATCH_SIZE(queue_depth) ((queue_depth) / 10)

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
    long long aof_increment;
} OperationData;

typedef struct
{
    struct io_uring *ring;
    int cqe_batch_size;
    int *fd_noappend;
    long long *aof_increment;
    bool *running;
    pthread_mutex_t *lock;
    bool *correct_test_on;
    int *correct_test_reqnum;
    void (*serverLog)(int level, const char *fmt, ...);
} CompletionThreadArgs;

typedef struct
{
    struct io_uring *ring;
    int fd;
    int MAX_RETRY;
    off_t write_offset;
    bool sqpoll;
    bool fsync_always;
    long long aof_increment;
} WriteUringArgs;

int setup_aof_io_uring_sq_poll(int QUEUE_DEPTH, struct io_uring *ring);
int setup_aof_io_uring(int QUEUE_DEPTH, struct io_uring *ring);
void *process_completions(void *args);
int aofFsyncUring(int fd, struct io_uring *ring, int MAX_RETRY, bool sqpoll, long long aof_increment);
ssize_t aofWriteUring(int fd, const char *buf, size_t len, WriteUringArgs args);
CompletionThreadArgs getCompletionThreadArgs(struct io_uring *ring, int cqe_batch_size, int *fd_noappend, long long *aof_increment, bool *running, pthread_mutex_t *lock, bool *correct_test, int *correct_test_reqnum, void (*serverLog)(int level, const char *fmt, ...));
#endif