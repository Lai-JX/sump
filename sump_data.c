#include "sump.h"


/* 获取 I/O channel */
struct spdk_io_channel *ump_bdev_get_io_channel(void *ctx)
{
    printf("ump_bdev_get_io_channel\n");
    return spdk_get_io_channel(ctx);
}

/********************************************************
* Function name:    ump_bdev_submit_request
* Description:      处理I/O请求（是函数表中的函数，由上层调用）
* Parameter:
*   @ch             spdk_io_channel结构体指针，代表访问I/O设备的线程通道
*   @bdev_io        spdk_bdev_io结构体指针，保存块设备I/O信息
* Return:           无        
**********************************************************/
void ump_bdev_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
    struct ump_bdev_channel *ump_channel = NULL;
    struct ump_bdev_iopath *iopath = NULL;
    struct spdk_bdev *bdev = NULL;
    struct ump_bdev *mbdev = NULL;
    struct ump_bdev_io_completion_ctx *ump_completion_ctx = NULL;

    // sump_printf("ump_bdev_submit_request\n");
    
    ump_completion_ctx = calloc(1, sizeof(struct ump_bdev_io_completion_ctx));
    if (ump_completion_ctx == NULL)
    {
        fprintf(stderr, "calloc for ump_completion_ctx failed.\n");
        goto err;
    }

    /* 替换io完成的回调，方便后续实现故障处理等功能 */
    ump_completion_ctx->real_caller_ctx = bdev_io->internal.caller_ctx;
    ump_completion_ctx->real_completion_cb = bdev_io->internal.cb;

    ump_completion_ctx->ch = ch;
    bdev_io->internal.cb = ump_bdev_io_completion_cb;
    bdev_io->internal.caller_ctx = ump_completion_ctx;

    ump_channel = spdk_io_channel_get_ctx(ch);
    mbdev = spdk_io_channel_get_io_device(ch);

    //ljx
    // memcpy(&(ump_completion_ctx->ump_channel), ump_channel, sizeof(struct ump_bdev_channel));

    //     /* 寻找I/O路径 */
    iopath = ump_bdev_find_iopath(ump_channel);
    if (iopath == NULL)
    {
        printf("mbdev(%s) don't has any iopath.\n", mbdev->bdev.name);
        goto err;
    }

    /* 设置路径 */
    ump_completion_ctx->iopath = iopath;
    bdev = iopath->bdev;
    // bdev_io->internal.ch->channel = iopath->io_channel;
    // sump_printf("before bdev->fn_table->submit_request\n");
    /* 提交I/O请求 */
    bdev->fn_table->submit_request(iopath->io_channel, bdev_io);
    // sump_printf("after bdev->fn_table->submit_request\n\n");

err:
    /* todo io complete */
    return;
}

