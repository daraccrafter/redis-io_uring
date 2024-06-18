
#include "uring.h"

struct io_uring setup_aof_io_uring(int QUEUE_DEPTH)
{
    struct io_uring ring;
    printf("Setting up io_uring with queue depth: %d\n", QUEUE_DEPTH);
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret)
    {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        exit(1);
    }
    return ring;
}
ssize_t aofWriteUringg(int fd, const char *buf, size_t len, UringArgs args)
{
    struct io_uring_sqe *sqe;
    int retries = 0;
    sds sdsbuf = sdsnewlen(buf, len);
    OperationData *op_data = (OperationData *)zmalloc(sizeof(OperationData));
    op_data->op = WRITE_URING;
    op_data->len = len;
    op_data->buf_ptr = (void *)sdsbuf;
    op_data->write_offset = args.write_offset;
    printf("HERE\n");
    while ((sqe = io_uring_get_sqe(args.ring)) == NULL && retries++ < args.MAX_RETRY)
    {
        retries++;
    }
    if (!sqe)
    {
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }
    io_uring_prep_write(sqe, fd, sdsbuf, len, args.write_offset);
    sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data(sqe, op_data);
    return len;
}
void process_completions(void *args)
{

    CompletionThreadArgs *arg = (CompletionThreadArgs *)args;
    struct io_uring *ring = arg->ring;
    int cqe_batch_size = arg->cqe_batch_size;
    int *fd = arg->fd;
    int *fd_noappend = arg->fd_noappend;
    long long *aof_increment = arg->aof_increment;
    struct io_uring_cqe *cqe;
    struct io_uring_cqe *cqes[cqe_batch_size];

    while (1)
    {
        int count = io_uring_peek_batch_cqe(ring, cqes, cqe_batch_size);
        for (int i = 0; i < count; i++)
        {

            cqe = cqes[i];
            if (cqe->user_data != NULL)
            {
                OperationData *op_data = (OperationData *)io_uring_cqe_get_data(cqe);
                switch (op_data->op)
                {
                case WRITE_URING:

                    if (cqe->res < 0)
                    {
                        UringArgs args = {
                            .ring = ring,
                            .fd = *fd,
                            .MAX_RETRY = MAX_RETRY_S,
                            .write_offset = 0};
                        aofWriteUring(*fd, (sds)op_data->buf_ptr, op_data->len, args);
                    }
                    else if (cqe->res != op_data->len)
                    {
                        UringArgs args = {
                            .ring = ring,
                            .fd = *fd,
                            .MAX_RETRY = MAX_RETRY_S,
                            .write_offset = aof_increment != op_data->aof_file_incr ? 0 : op_data->write_offset + cqe->res};
                        if (aof_increment != op_data->aof_file_incr)
                        {
                            aofWriteUring(*fd, (sds)op_data->buf_ptr, op_data->len, args); // Case new file: where new file is created alredy and rewrite happened (meaning we lost this data), so just append it to new file
                        }
                        else
                        {
                            aofWriteUring(*fd_noappend, (sds)(op_data->buf_ptr + cqe->res), op_data->len - cqe->res, args); // Case same file: where we deal with partial write using fd_noappend (because we don't want to append to the file, we want to write to exact offset)
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
                        aofFsyncUring(*fd, ring, 100);
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
}

int aofFsyncUring(int fd, struct io_uring *ring, int MAX_RETRY)
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
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }
    io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
    io_uring_sqe_set_data(sqe, op_data);

    return 0;
}
ssize_t aofWriteUring(int fd, const char *buf, size_t len, UringArgs args)
{
    struct io_uring_sqe *sqe;
    int retries = 0;
    sds sdsbuf = sdsnewlen(buf, len);
    OperationData *op_data = (OperationData *)zmalloc(sizeof(OperationData));
    op_data->op = WRITE_URING;
    op_data->len = len;
    op_data->buf_ptr = (void *)sdsbuf;
    op_data->write_offset = args.write_offset;
    op_data->aof_file_incr = args.aof_file_incr;
    while ((sqe = io_uring_get_sqe(args.ring)) == NULL && retries++ < args.MAX_RETRY)
    {
        retries++;
    }
    if (!sqe)
    {
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }
    io_uring_prep_write(sqe, fd, sdsbuf, len, args.write_offset);
    sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data(sqe, op_data);
    return len;
}
