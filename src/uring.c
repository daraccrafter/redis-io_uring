
#include "uring.h"

int setup_aof_io_uring(int QUEUE_DEPTH, struct io_uring *ring)
{
    int ret = io_uring_queue_init(QUEUE_DEPTH, ring, 0);
    if (ret)
    {
        return -1;
    }
    return 0;
}
int setup_aof_io_uring_sq_poll(int QUEUE_DEPTH, struct io_uring *ring)
{
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;

    int ret = io_uring_queue_init_params(QUEUE_DEPTH, ring, &params);
    if (ret)
    {
        return -1;
    }
    return 0;
}

void *process_completions(void *args)
{

    CompletionThreadArgs *arg = (CompletionThreadArgs *)args;
    struct io_uring *ring = arg->ring;
    int cqe_batch_size = arg->cqe_batch_size;
    int *fd = arg->fd;
    int *fd_noappend = arg->fd_noappend;
    long long *aof_increment = arg->aof_increment;
    bool *running = arg->running;
    bool *correct_test = arg->correct_test_on;
    int *correct_test_reqnum = arg->correct_test_reqnum;
    void (*serverLog)(int level, const char *fmt, ...) = arg->serverLog;
    pthread_mutex_t *lock = arg->lock;
    struct io_uring_cqe *cqe;
    struct io_uring_cqe *cqes[cqe_batch_size];

    static int write_count = 1;       // used for correctness testing
    static int fsync_count_compl = 1; // used for correctness testing
    int no_completion_count = 0;
    int start_time;
    int end_time;
    while (1)
    {
        int count = io_uring_peek_batch_cqe(ring, cqes, cqe_batch_size);
        if (count == 0)
        {
            no_completion_count++;
            if (no_completion_count > 10000)
            {
                pthread_mutex_lock(lock);
                *running = false;
                pthread_mutex_unlock(lock);
                break;
            }
            usleep(1000);
            continue;
        }
        else
        {
            no_completion_count = 0;
        }

        for (int i = 0; i < count; i++)
        {

            cqe = cqes[i];
            if (cqe->user_data != 0)
            {
                OperationData *op_data = (OperationData *)io_uring_cqe_get_data(cqe);
                switch (op_data->op)
                {
                case WRITE_URING:
                    // serverLog(LL_NOTICE, "Write completion: %d", write_count++);
                    if (cqe->res < 0)
                    {
                        printf("Error writing to AOF file, Retrying...");
                        serverLog(LL_WARNING, "Error writing to AOF file, Retrying...");
                        WriteUringArgs args = {
                            .ring = ring,
                            .fd = *fd,
                            .MAX_RETRY = MAX_RETRY_S,
                            .write_offset = 0,
                            .sqpoll = false,
                            .fsync_always = false};
                        aofWriteUring(*fd, (sds)op_data->buf_ptr, op_data->len, args);
                    }
                    else if (cqe->res != (int)op_data->len)
                    {
                        serverLog(LL_WARNING, "Partial write to AOF file: %d/%d, at offset: %d Retrying...", cqe->res, op_data->len, op_data->write_offset);
                        WriteUringArgs args = {
                            .ring = ring,
                            .fd = *fd,
                            .MAX_RETRY = MAX_RETRY_S,
                            .write_offset = op_data->write_offset + cqe->res,
                            .sqpoll = false,
                            .fsync_always = false};

                        aofWriteUring(*fd_noappend, (sds)((char *)op_data->buf_ptr + cqe->res), op_data->len - cqe->res, args);
                        io_uring_submit(ring);
                    }
                    else
                    {
                    }
                    break;
                case FSYNC_URING:
                    if (cqe->res < 0)
                    {

                        serverLog(LL_WARNING, "Error fsyncing AOF file, Retrying...");
                        aofFsyncUring(*fd, ring, 100, false);
                        io_uring_submit(ring);
                    }
                    if (cqe->res == 0)
                    {
                        if (*correct_test && fsync_count_compl == *correct_test_reqnum)
                        {
                            serverLog(LL_NOTICE, "Final fsync completed\n");
                        }
                        fsync_count_compl++;
                        // serverLog(LL_NOTICE, "Fsync completion: %d", fsync_count_compl);
                    }
                    break;
                default:
                    break;
                }
                zfree(op_data);
            }
            io_uring_cqe_seen(ring, cqe);
        }
    }
    serverLog(LL_NOTICE, "Completion thread exiting...");
    return NULL;
}
/*returns 0 on successful sqe, or -1 on failed to get sqe*/
int aofFsyncUring(int fd, struct io_uring *ring, int MAX_RETRY, bool sqpoll)
{
    struct io_uring_sqe *sqe;
    OperationData *op_data = (OperationData *)zmalloc(sizeof(OperationData));
    op_data->op = FSYNC_URING;
    op_data->len = 0;
    op_data->buf_ptr = NULL;
    int retries = 0;

    while ((sqe = io_uring_get_sqe(ring)) == NULL && retries++ < MAX_RETRY)
    {
        retries++;
    }
    if (!sqe)
    {
        return -1;
    }
    io_uring_prep_fsync(sqe, sqpoll ? 0 : fd, IORING_FSYNC_DATASYNC);

    if (sqpoll)
    {
        sqe->flags |= IOSQE_FIXED_FILE;
    }
    sqe->flags |= IOSQE_IO_DRAIN;

    io_uring_sqe_set_data(sqe, op_data);

    return 0;
}
/* returns len on successful sqe, or -1 on failed to get sqe*/
#include <stdlib.h> // For rand() and srand()
#include <time.h>   // For time()

ssize_t aofWriteUring(int fd, const char *buf, size_t len, WriteUringArgs args)
{
    struct io_uring_sqe *sqe;
    int retries = 0;
    sds newbuf = sdsnewlen(buf, len);
    OperationData *op_data = (OperationData *)zmalloc(sizeof(OperationData));
    op_data->op = WRITE_URING;
    op_data->len = len;
    op_data->buf_ptr = (void *)newbuf;
    op_data->write_offset = args.write_offset;

    while ((sqe = io_uring_get_sqe(args.ring)) == NULL && retries++ < args.MAX_RETRY)
    {
        retries++;
    }
    if (!sqe)
    {
        return -1;
    }
    io_uring_prep_write(sqe, args.sqpoll ? 0 : fd, newbuf, len, args.write_offset);

    if (args.sqpoll)
    {
        sqe->flags |= IOSQE_FIXED_FILE;
    }
    sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data(sqe, op_data);
    return len;
}
CompletionThreadArgs getCompletionThreadArgs(struct io_uring *ring, int cqe_batch_size, int *fd, int *fd_noappend, long long *aof_increment, bool *running, pthread_mutex_t *lock, bool *correct_test, int *correct_test_reqnum, void (*serverLog)(int level, const char *fmt, ...))
{
    CompletionThreadArgs args = {
        .ring = ring,
        .cqe_batch_size = cqe_batch_size,
        .fd = fd,
        .fd_noappend = fd_noappend,
        .aof_increment = aof_increment,
        .running = running,
        .lock = lock,
        .correct_test_on = correct_test,
        .correct_test_reqnum = correct_test_reqnum,
        .serverLog = serverLog,
    };
    return args;
}
