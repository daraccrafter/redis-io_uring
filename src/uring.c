
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
void process_completions(void *args)
{
    CompletionThreadArgs *arg = (CompletionThreadArgs *)args;
    struct io_uring *ring = arg->ring;
    int cqe_batch_size = arg->cqe_batch_size;

    struct io_uring_cqe *cqe;
    struct io_uring_cqe *cqes[cqe_batch_size];
    while (1)
    {
        int count = io_uring_peek_batch_cqe(ring, cqes, cqe_batch_size);

        for (int i = 0; i < count; i++)
        {
            cqe = cqes[i];
            if (cqe->res < 0 && cqe!=NULL)
            {
                printf( "Error in completion: %d\n",cqe->res);
            }
            else if (cqe->user_data != NULL)
            {
                sdsfree((sds)cqe->user_data);
            }
            io_uring_cqe_seen(ring, cqe);
        }
    }
}

int aofFsyncUring(int fd, struct io_uring *ring, int MAX_RETRY)
{
    struct io_uring_sqe *sqe;

    int retries = 0;
    sqe = io_uring_get_sqe(ring);

    io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;

    return 0;
}
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring, int MAX_RETRY)
{
    struct io_uring_sqe *sqe;
    int retries = 0;
    sds sdsbuf = sdsnewlen(buf, len);
    sqe = io_uring_get_sqe(ring);
    if (!sqe)
    {
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }

    io_uring_prep_write(sqe, fd, sdsbuf, len, 0);
    sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data(sqe,sdsbuf);
    return len;
}