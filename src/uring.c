
#include "uring.h"


uint64_t pack_data(uint8_t op, uintptr_t buf_ptr, size_t len)
{
    return ((uint64_t)op << OP_SHIFT) |
           ((uint64_t)len << LEN_SHIFT) |
           ((uint64_t)buf_ptr << PTR_SHIFT);
}

OperationData unpack_data(uint64_t packed_data)
{
    OperationData op_data;
    op_data.op = (packed_data >> OP_SHIFT) & OP_MASK;
    op_data.len = (packed_data >> LEN_SHIFT) & LEN_MASK;
    op_data.buf_ptr = (packed_data >> PTR_SHIFT) & PTR_MASK;
    return op_data;
}

struct io_uring setup_io_uring(void)
{
    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, IORING_SETUP_SQPOLL);
    if (ret < 0)
    {
        exit(1);
    }

    return ring;
}

void process_completions(struct io_uring *ring)
{
    while (1)
    {
        struct io_uring_cqe *cqe;
        int ret = io_uring_peek_cqe(ring, &cqe);
        if (ret < 0)
        {
            continue;
        }

        if (cqe->res < 0)
        {
            fprintf(stderr, "Error in completion: %s\n", strerror(-cqe->res));
        }
        else if (cqe->user_data != NULL)
        {
            OperationData op_data = unpack_data(cqe->user_data);
            switch (op_data.op)
            {
            case WRITE_URING:
                sdsfree((sds)op_data.buf_ptr);
                break;
            case FSYNC_URING:
                break;
            }
        }
        io_uring_cqe_seen(ring, cqe);
    }
}

int aofFsyncUring(int fd, struct io_uring *ring)
{
    struct io_uring_sqe *sqe;

    int retries = 0;
    while ((sqe = io_uring_get_sqe(ring)) == NULL && retries < MAX_RETRY)
    {
        retries++;
        usleep(1);
    }
    if (!sqe)
    {
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }

    io_uring_prep_fsync(sqe, 0, IORING_FSYNC_DATASYNC);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_sqe_set_data(sqe, pack_data(FSYNC_URING, 0, 0));
    return 0;
}
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring)
{
    sds sds_buf = sdsnewlen(buf, len);
    struct io_uring_sqe *sqe;
    int retries = 0;

    while ((sqe = io_uring_get_sqe(ring)) == NULL && retries < MAX_RETRY)
    {
        retries++;
        usleep(1);
    }
    if (!sqe)
    {
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }

    io_uring_prep_write(sqe, 0, sds_buf, len, 0);
    sqe->flags |= IOSQE_IO_LINK;
    sqe->flags |= IOSQE_FIXED_FILE;
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    io_uring_sqe_set_data(sqe, pack_data(WRITE_URING, (uintptr_t)sds_buf, len));
    return len;
}