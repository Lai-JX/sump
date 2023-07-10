#include "split.h"

/******************************仅作备份******************************************************/
    /* 拆分 */
    
    // ump_completion_ctx->complete_num = 0;
    // ump_completion_ctx->bdev_io = bdev_io;

    // struct spdk_bdev_io *bdev_io1 = calloc(1, sizeof(struct spdk_bdev_io));
    // struct spdk_bdev_io *bdev_io2 = calloc(1, sizeof(struct spdk_bdev_io)); // 这俩应该在complete回调时free，暂未实现

    // if (bdev_io1 == NULL || bdev_io2 == NULL) {
    //     fprintf(stderr, "calloc for bdev_io1/2 failed.\n");
    //     goto err;
    // }

    // split_io_request(bdev_io, bdev_io1, bdev_io2);

    // iopath = ump_bdev_find_iopath(ump_channel);
    // printf("iopath(struct) 1 addr: %p\n", iopath);
    // if (iopath == NULL) {
    //     fprintf(stderr, "mbdev(%s) dont has any iopath.\n", mbdev->bdev.name);
    //     goto err;
    // }
    // struct ump_bdev_iopath *iopath_ = TAILQ_NEXT(iopath, tailq);
    // printf("iopath(struct) 2 addr: %p\n", iopath_);

    

    
    // if (iopath_ == NULL || bdev_io->u.bdev.iovcnt!=1) {  // 说明只有一条路
    //     ump_completion_ctx->split_num = 1;
    //     bdev = iopath->bdev;
    //     bdev->fn_table->submit_request(iopath->io_channel, bdev_io);
    //     return;
    // }
    // // 两条路
    // ump_completion_ctx->split_num = 2;
    // bdev = iopath->bdev;
    // bdev->fn_table->submit_request(iopath->io_channel, bdev_io1);

    // bdev = iopath_->bdev;
    // bdev->fn_table->submit_request(iopath_->io_channel, bdev_io2);
    // printf("submit finish!\n");

    /**************************************************************************************/