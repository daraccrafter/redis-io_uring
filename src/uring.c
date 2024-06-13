
#include "uring.h"
#include <jemalloc/jemalloc.h>

struct io_uring setup_io_uring(void)
{
    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, IORING_SETUP_SQPOLL);
    if (ret < 0)
    {
       // fprintf(stderr, "io_uring_queue_init_params: %s\n", strerror(-ret));
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
        // else if (cqe->user_data != NULL)
        // {
        //     // OperationData *op_data = (OperationData *)cqe->user_data;
        //     // switch (op_data->op)
        //     // {
        //     // case WRITE_URING:
        //     //     // printf("SUPPOSED TO WRITE: %d\n", op_data->len);
        //     //     // printf("WROTE: %d\n", cqe->res);
        //     //     break;
        //     // case FSYNC_URING:
        //     //     break;
        //     // }
        //     // je_free(op_data);
        // }
        io_uring_cqe_seen(ring, cqe);
    }
}

int aofFsyncUring(int fd, struct io_uring *ring)
{
    struct io_uring_sqe *sqe;

    int retries = 0;
   // printf("Trying to get SQE\n");
    while ((sqe = io_uring_get_sqe(ring)) == NULL && retries < MAX_RETRY)
    {
        retries++;
        usleep(1);
    }
    if (!sqe)
    {
        // je_free(temp_buf);
        fprintf(stderr, "Failed to get SQE after %d retries\n", retries);
        return -1;
    }

    io_uring_prep_fsync(sqe, 0, IORING_FSYNC_DATASYNC);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    sqe->flags |= IOSQE_FIXED_FILE;
    sqe->user_data = NULL;
    //io_uring_submit(ring);
    return 0;
}
ssize_t aofWriteUring(int fd, const char *buf, size_t len, struct io_uring *ring)
{
    // char *temp_buf = (char *)je_malloc(len);
    // if (!temp_buf)
    // {
    //     fprintf(stderr, "Failed to allocate memory\n");
    //     return -1;
    // }
    // memcpy(temp_buf, buf, len);
    struct io_uring_sqe *sqe;
    int retries = 0;
    // OperationData *op_data = (OperationData *)je_malloc(sizeof(OperationData));
    // op_data->op = WRITE_URING;
    // op_data->buf_ptr = (uintptr_t)buf;
    // op_data->len = len;
    sqe=io_uring_get_sqe(ring);

    io_uring_prep_write(sqe, 0, buf, len, 0); // Ensure fd is valid
    sqe->flags |= IOSQE_IO_LINK;
    //sqe->user_data = (uintptr_t)op_data;
    sqe->flags |= IOSQE_FIXED_FILE;
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    //struct io_uring_cqe *cqe;
    //io_uring_submit(ring);
    usleep(10);

    return len;
}