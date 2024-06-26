
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
    void (*serverLog)(int level, const char *fmt, ...) = arg->serverLog;

    struct io_uring_cqe *cqe;
    struct io_uring_cqe *cqes[cqe_batch_size];

    int no_completion_count = 0;

    while (1)
    {
        int count = io_uring_peek_batch_cqe(ring, cqes, cqe_batch_size);
        if (count == 0)
        {
            no_completion_count++;
            if (no_completion_count > 1000)
            {
                *running = false;
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
                    if (cqe->res < 0)
                    {
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
                        serverLog(LL_WARNING, "Partial write to AOF file: %d/%d, Retrying...", cqe->res, op_data->len);
                        WriteUringArgs args = {
                            .ring = ring,
                            .fd = *fd,
                            .MAX_RETRY = MAX_RETRY_S,
                            .write_offset = *aof_increment != op_data->aof_file_incr ? 0 : op_data->write_offset + cqe->res,
                            .sqpoll = false,
                            .fsync_always = false};
                        if (*aof_increment != op_data->aof_file_incr)
                        {
                            aofWriteUring(*fd, (sds)op_data->buf_ptr, op_data->len, args); // Case new file: where new file is created alredy and rewrite happened (meaning we lost this data), so just append it to new file
                        }
                        else
                        {
                            aofWriteUring(*fd_noappend, (sds)((char *)op_data->buf_ptr + cqe->res), op_data->len - cqe->res, args); // Case same file: where we deal with partial write using fd_noappend (because we don't want to append to the file, we want to write to exact offset)
                        }
                    }
                    else
                    {
                        sdsfree((sds)op_data->buf_ptr);
                    }
                    break;
                case FSYNC_URING:
                    if (cqe->res < 0)
                    {
                        aofFsyncUring(*fd, ring, 100, false);
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
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    io_uring_sqe_set_data(sqe, op_data);

    return 0;
}
/* returns len on successful sqe, or -1 on failed to get sqe*/
ssize_t aofWriteUring(int fd, const char *buf, size_t len, WriteUringArgs args)
{
    struct io_uring_sqe *sqe;
    int retries = 0;
    sds sdsbuf = sdsnewlen(buf, len);
    OperationData *op_data = (OperationData *)zmalloc(sizeof(OperationData));
    op_data->op = WRITE_URING;
    op_data->len = len;
    op_data->buf_ptr = (void *)sdsbuf;
    op_data->write_offset = args.write_offset;
    while ((sqe = io_uring_get_sqe(args.ring)) == NULL && retries++ < args.MAX_RETRY)
    {
        retries++;
    }
    if (!sqe)
    {
        return -1;
    }
    io_uring_prep_write(sqe, args.sqpoll ? 0 : fd, sdsbuf, len, args.write_offset);
    if (args.fsync_always)
    {
        sqe->flags |= IOSQE_IO_LINK;
    }
    if (args.sqpoll)
    {
        sqe->flags |= IOSQE_FIXED_FILE;
    }
    io_uring_sqe_set_data(sqe, op_data);
    return len;
}

CompletionThreadArgs getCompletionThreadArgs(struct io_uring *ring, int cqe_batch_size, int *fd, int *fd_noappend, long long *aof_increment, bool *running, void (*serverLog)(int level, const char *fmt, ...))
{
    CompletionThreadArgs args = {
        .ring = ring,
        .cqe_batch_size = cqe_batch_size,
        .fd = fd,
        .fd_noappend = fd_noappend,
        .aof_increment = aof_increment,
        .running = running,
        .serverLog = serverLog};
    return args;
}


