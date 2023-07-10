#include "sump.h"



/*************************************************************************/
/***************************io channel begin*****************************/
/*************************************************************************/

/********************************************************
* Function name:    ump_bdev_io_completion_cb
* Description:      I/O完成时的回调函数，I/O请求成功时，执行真正的回调函数；失败时进行故障处理，如故障切换等等
* Parameter:
*   @bdev_io        spdk_bdev_io结构体指针，包含本次I/O的信息
*   @success        是否成功的标识
*   @cb_arg         从I/O请求处传递过来的参数
* Return:           无        
**********************************************************/
void ump_bdev_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
    struct ump_bdev_io_completion_ctx *completion_ctx = cb_arg;
    if (success)
    {
        sump_printf("io complete success.\n");
        completion_ctx->real_completion_cb(bdev_io, success, completion_ctx->real_caller_ctx);
        free(completion_ctx);
    }
    else
    {
        /* todo 失败io处理 */
        // sd
        sump_printf("io complete failed.\n");
        struct spdk_io_channel *ch = completion_ctx->ch;
        struct ump_bdev_iopath *iopath = completion_ctx->iopath;
        struct ump_bdev_channel *ump_channel = spdk_io_channel_get_ctx(ch);

        memcpy(ump_channel, &completion_ctx->ump_channel, sizeof(struct ump_bdev_channel));


        TAILQ_REMOVE(&ump_channel->iopath_list, iopath, tailq);
        spdk_put_io_channel(iopath->io_channel);
        free(iopath);

        // sd
        // ljx:重新请求
        bdev_io->internal.cb = completion_ctx->real_completion_cb;
        bdev_io->internal.caller_ctx = completion_ctx->real_caller_ctx;
        free(completion_ctx);
        ump_bdev_submit_request(ch, bdev_io);
    }
    // free(completion_ctx);
    return;
err:
    free(completion_ctx);
    return;
}

/********************************************************
* Function name:    ump_bdev_find_iopath
* Description:      从ump_channel中寻找一条I/O路径（支持负载均衡）
* Parameter:
*   @ump_channel    ump_bedv_channel结构体指针，保存了I/O路径队列
* Return:           若找到路径，则返回ump_bdev_iopath结构体指针，否则返回空指针        
**********************************************************/
struct ump_bdev_iopath *ump_bdev_find_iopath(struct ump_bdev_channel *ump_channel)
{
    // sd
    static int turn = 0;
    struct ump_bdev_iopath *iopath;

    if (TAILQ_EMPTY(&ump_channel->iopath_list))
    {
        sump_printf("TAILQ_EMPTY\n");
        return NULL;
    }

    int idx = 0;
    TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
    {
        if (idx == turn)
        {
            turn++;
            sump_printf("%s's IO channel is chosen\n", iopath->bdev->name);
            return iopath;
        }
        else
        {
            idx++;
        }
    }
    turn = 1;
    iopath = TAILQ_FIRST(&ump_channel->iopath_list);
    sump_printf("%s's IO channel is chosen\n", iopath->bdev->name);
    return iopath;
    // sd
}

/********************************************************
* Function name:    ump_bdev_channel_clear_all_iopath
* Description:      释放所有I/O channel
* Parameter:
*   @ump_channel    ump_bedv_channel结构体指针，保存了I/O路径队列
* Return:           无        
**********************************************************/
void ump_bdev_channel_clear_all_iopath(struct ump_bdev_channel *ump_channel)
{
    struct ump_bdev_iopath *iopath = NULL;

    while (!TAILQ_EMPTY(&ump_channel->iopath_list))
    {
        iopath = TAILQ_FIRST(&ump_channel->iopath_list);
        spdk_put_io_channel(iopath->io_channel);
        TAILQ_REMOVE(&ump_channel->iopath_list, iopath, tailq);
        sump_printf("%s's io channel is removed\n", iopath->bdev->name);
        free(iopath);
    }
}

/********************************************************
* Function name:    ump_bdev_channel_create_cb
* Description:      创建完channel的回调函数（在spdk_io_device_register时指定，get_io_channel时若没有channel会调用） 通过回调函数来分配新I/O通道所需的任何资源 （这里主要是更新iopath）
* Parameter:
*   @io_device      ump_bedv结构体指针
*   @ctx_buf        ump_bdev_channel结构体指针
* Return:           0表示成功，-1表示失败        
**********************************************************/
int ump_bdev_channel_create_cb(void *io_device, void *ctx_buf)
{
    printf("ump_bdev_channel_create_cb\n");
    struct ump_bdev *mbdev = io_device; // io_device和mbdev的第一个参数为bdev
    struct ump_bdev_channel *ump_channel = ctx_buf;
    struct spdk_list_bdev *list_bdev = NULL;
    struct spdk_bdev *bdev = NULL;
    struct spdk_io_channel *io_channel = NULL;
    struct ump_bdev_iopath *iopath = NULL;
    // ljx: 添加if(TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list))
    // if (TAILQ_EMPTY(&ump_channel->iopath_list))
    TAILQ_INIT(&ump_channel->iopath_list); // 当前mbdev在当前线程的各条路径
    // 遍历mbdev的所有bdev
    TAILQ_FOREACH(list_bdev, &mbdev->spdk_bdev_list, tailq)
    {
        bdev = list_bdev->bdev;
        io_channel = bdev->fn_table->get_io_channel(bdev->ctxt); // 这里的bdev->ctxt为mbdev
        if (io_channel == NULL)
        {
            fprintf(stderr, "ump bdev channel create get iopath channel failed.\n");
            // goto err;   // ljx
            continue;
        }

        iopath = calloc(1, sizeof(struct ump_bdev_iopath));
        if (iopath == NULL)
        {
            fprintf(stderr, "calloc for iopath failed\n");
            spdk_put_io_channel(io_channel);
            goto err;
        }

        iopath->io_channel = io_channel;
        iopath->bdev = bdev;
        TAILQ_INSERT_TAIL(&ump_channel->iopath_list, iopath, tailq);
        sump_printf("%s's io channel is added\n", bdev->name);
    }

    return 0;

err:
    ump_bdev_channel_clear_all_iopath(ump_channel);
    return -1;
}

/********************************************************
* Function name:    ump_bdev_channel_destroy_cb
* Description:      释放 I/O channel 时的回调函数，在设备注册（spdk_io_device_register）时指定
* Parameter:
*   @io_device      指向I/O设备的指针
*   @ctx_buf        传递过来的参数（一个ump_bdev_channel指针）
* Return:           无        
**********************************************************/
void ump_bdev_channel_destroy_cb(void *io_device, void *ctx_buf)
{
    struct ump_bdev_channel *ump_channel = ctx_buf;
    printf("ump_bdev_channel_destroy_cb\n");

    ump_bdev_channel_clear_all_iopath(ump_channel);
}

/*************************************************************************/
/******************************io channel end*****************************/
/*************************************************************************/

int ump_bdev_destruct(void *ctx)
{
    // TODO
    sump_printf("ump_bdev_destruct\n");
    return 0;
}

/* 判断是否支持某种I/O类型 */
bool ump_bdev_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type)
{
    struct ump_bdev *mbdev = ctx;
    struct spdk_list_bdev *list_bdev = TAILQ_FIRST(&mbdev->spdk_bdev_list);
    struct spdk_bdev *bdev = list_bdev->bdev;

    return bdev->fn_table->io_type_supported(bdev->ctxt, io_type);
}

int ump_bdev_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
    struct ump_bdev *mbdev = ctx;
    struct spdk_list_bdev *list_bdev = TAILQ_FIRST(&mbdev->spdk_bdev_list);
    struct spdk_bdev *bdev = list_bdev->bdev;

    return bdev->fn_table->dump_info_json(bdev->ctxt, w);
}

void ump_bdev_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w)
{
    sump_printf("ump_bdev_write_config_json\n");
}

uint64_t ump_bdev_get_spin_time(struct spdk_io_channel *ch)
{
    sump_printf("ump_bdev_get_spin_time\n");
    return 0;
}