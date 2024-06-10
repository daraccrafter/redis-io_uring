#include "uring.h"
#include <jemalloc/jemalloc.h>

struct io_uring setup_io_uring(void)
{
    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, IORING_SETUP_SQPOLL);
    if (ret < 0)
    {
        fprintf(stderr, "io_uring_queue_init_params: %s\n", strerror(-ret));
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
            char *completed_buf = (char *)cqe->user_data;
            printf("Freeing buffer at %p\n", completed_buf);
            je_free(completed_buf);
        }

        io_uring_cqe_seen(ring, cqe);
    }
}

int aofFsyncUring(int fd, struct io_uring *ring)
{
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);
    if (!sqe)
    {
        perror("Unable to get SQE for fsync");
        return -1;
    }
    io_uring_prep_fsync(sqe, 0, IORING_FSYNC_DATASYNC);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    sqe->flags |= IOSQE_FIXED_FILE;
    sqe->user_data = NULL;

    return 0;
}
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring)
{
    char *temp_buf = (char *)je_malloc(len);
    if (!temp_buf)
    {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }
    memcpy(temp_buf, buf, len);
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_write(sqe, 0, temp_buf, len, 0);
    sqe->flags |= IOSQE_IO_LINK;
    sqe->flags |= IOSQE_FIXED_FILE;

    sqe->user_data = (uintptr_t)temp_buf;

    return len;
}