#include "sump.h"

// 添加两个变量，用于统计每条路径的io次数
uint64_t count[4];
struct spdk_poller * io_count_poller;

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
    struct ump_bdev_iopath *iopath = completion_ctx->iopath;
    if (success)
    {
        // sump_printf("io complete success.\n");   
        update_io_time(iopath, bdev_io);                                           // 负载均衡算法：server-time
        iopath->io_incomplete--;                                                // 负载均衡算法：queue-length
          
        completion_ctx->real_completion_cb(bdev_io, success, completion_ctx->real_caller_ctx);
        free(completion_ctx);
    }
    else
    {
        /* todo 失败io处理 */
        // sd
        // printf("\n\n\n\n\n\n\n");
        sump_printf("io complete failed.\n");
        struct spdk_io_channel *ch = completion_ctx->ch;
        struct ump_bdev_channel *ump_channel = spdk_io_channel_get_ctx(ch);

        iopath->available = false;

        // sd
        // ljx:重新请求
        bdev_io->internal.cb = completion_ctx->real_completion_cb;
        bdev_io->internal.caller_ctx = completion_ctx->real_caller_ctx;
        bdev_io->internal.submit_tsc = spdk_get_ticks();        // 重置初始时间戳
        free(completion_ctx);
        ump_bdev_submit_request(ch, bdev_io);
    }
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
struct ump_bdev_iopath *ump_bdev_find_iopath(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io)
{
    struct ump_bdev_iopath *iopath;
    // iopath = ump_find_iopath_service_time(ump_channel, bdev_io);
    // iopath = ump_find_iopath_round_robin(ump_channel);
    // iopath = ump_find_iopath_queue_length(ump_channel);
    // iopath = ump_find_iopath_random(ump_channel);
    // iopath = ump_find_iopath_random_weight_static(ump_channel);
    iopath = ump_find_iopath_hash(ump_channel, bdev_io);

    // sump_printf("%s's IO channel is chosen\n", iopath->bdev->name);
    return iopath;
}

struct ump_bdev_iopath *ump_find_iopath_round_robin(struct ump_bdev_channel *ump_channel)
{
    // sd
    static int turn = 0;
    struct ump_bdev_iopath *iopath, *iopath1, *iopath2;

    if (TAILQ_EMPTY(&ump_channel->iopath_list))
    {
        sump_printf("TAILQ_EMPTY\n");
        return NULL;
    }

    int idx = 0;
    TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
    {
        if (iopath->available && idx == turn)
        {
            turn++;
            // sump_printf("%s's IO channel is chosen\n", iopath->bdev->name);
            return iopath;
        }
        else
        {
            idx++;
        }
    }
    turn = 1;
    // iopath = TAILQ_FIRST(&ump_channel->iopath_list);
    TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
    {
        if(!iopath->available)
            turn++;
        else
        {
            // sump_printf("%s's IO channel is chosen\n", iopath->bdev->name);
            return iopath;
        }
    }
}

struct ump_bdev_iopath *ump_find_iopath_service_time(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io)
{
    
    uint64_t min_time = UINT64_MAX;             // 初始化为最大
    struct ump_bdev_iopath *iopath, *iopath_chosen;
    if (bdev_io->type == SPDK_BDEV_IO_TYPE_READ)
    {
        TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
        {
            // sump_printf("%s's delay %ld\n", iopath->bdev->name, iopath->io_read_time.io_time_avg);
            if (iopath->available && iopath->io_read_time.io_time_avg < min_time)
            {
                min_time = iopath->io_read_time.io_time_avg;
                iopath_chosen = iopath;
            }
        }
    }
    else
    {
        TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
        {
            if (iopath->available && iopath->io_write_time.io_time_avg < min_time)
            {
                min_time = iopath->io_write_time.io_time_avg;
                iopath_chosen = iopath;
            }
        }
    }
    
    // sump_printf("%s's IO channel is chosen\n", iopath_chosen->bdev->name);
    return iopath_chosen;
}
void update_io_time(struct ump_bdev_iopath *iopath, struct spdk_bdev_io *bdev_io)
{
    struct time_queue_ele *time_ele = calloc(1, sizeof(struct time_queue_ele));
    if (time_ele == NULL)
    {
        fprintf(stderr, "calloc for time_queue_ele failed\n");
        return;
    }
    time_ele->io_time = spdk_get_ticks() - bdev_io->internal.submit_tsc;

    if (bdev_io->type == SPDK_BDEV_IO_TYPE_READ)
    {
        update_io_time_detail(time_ele, &iopath->io_read_time);
    }
    else
    {
        update_io_time_detail(time_ele, &iopath->io_write_time);
    }
}
void update_io_time_detail(struct time_queue_ele *time_ele, struct time_queue *t_queue)
{
    TAILQ_INSERT_TAIL(&t_queue->time_list, time_ele, tailq);
    t_queue->io_time_all += time_ele->io_time;

    if (t_queue->len < 20) // 记录20次io的时延
    {
        t_queue->len++;
        t_queue->io_time_avg = t_queue->io_time_all / t_queue->len;
        return;
    }
    struct time_queue_ele *tqh_first = t_queue->time_list.tqh_first;
    TAILQ_REMOVE(&t_queue->time_list, tqh_first, tailq);
    t_queue->io_time_all -= tqh_first->io_time;
    free(tqh_first);
    t_queue->io_time_avg = t_queue->io_time_all / t_queue->len;
}

struct ump_bdev_iopath *ump_find_iopath_queue_length(struct ump_bdev_channel *ump_channel)
{
    uint64_t min_io_count = UINT64_MAX;             // 最小未完成请求数 初始化为最大
    struct ump_bdev_iopath *iopath, *iopath_chosen;
    TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
    {
        if (iopath->available && iopath->io_incomplete < min_io_count)
        {
            min_io_count = iopath->io_incomplete;
            iopath_chosen = iopath;
        }
    }
    // sump_printf("%s's IO channel is chosen, incomplete task:%ld\n", iopath_chosen->bdev->name, iopath_chosen->io_incomplete);
    iopath_chosen->io_incomplete++;
    return iopath_chosen;
}

struct ump_bdev_iopath *ump_find_iopath_random(struct ump_bdev_channel *ump_channel)
{
    struct ump_bdev_iopath *iopath;
    int turn, count;
    while (1)
    {
        // 随机选择一个
        struct timeval start;
        gettimeofday( &start, NULL );
        srand((unsigned)(1000000*start.tv_sec + start.tv_usec));    // 精确到毫秒的随机种子
        turn = rand() % ump_channel->max_id;

        count = 0;

        TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
        {
            if (!iopath->available)
                count++;
            if (iopath->id == turn)
            {
                if (iopath->available)
                    return iopath;
                else
                {
                    if (count == ump_channel->max_id)
                        return NULL;
                    break;
                }
            }

        }
    }

}

struct ump_bdev_iopath *ump_find_iopath_random_weight_static(struct ump_bdev_channel *ump_channel)
{
    struct ump_bdev_iopath *iopath;
    int factor1, chose_id;
    float factor2;
    
    // 权重，暂时设定为4条路径
    float w[4] = {0.5, 0.2, 0.2, 0.1};
    // float w[4] = {1.0, 0, 0, 0};

    // 累积和
    float s[4];
    s[0] = w[0];
    for (int i = 1; i < 4; i++)
        s[i] = s[i - 1] + w[i];
    // printf("%f,%f\n", s[3],s[3]-1.0);
    // if (s[3] < 1.0+1e-6 && s[3] > 1.0-1e-6)
    // {
    //     fprintf(stderr, "the sum of weights must be one.\n");
    //     return NULL;
    // }

    // 先不考虑无路径可用的情况
    while (1)
    {
        // 随机选择一个
        struct timeval start;
        gettimeofday( &start, NULL );
        srand((unsigned)(1000000*start.tv_sec + start.tv_usec));    // 精确到毫秒的随机种子
        factor1 = (4 * ump_channel->max_id);
        factor2 = (float)(rand() % factor1);

        factor2 = factor2 / factor1;
        if (factor2 < s[0])
            chose_id = 0;
        else if (factor2 < s[1])
            chose_id = 1;
        else if (factor2 < s[2])
            chose_id = 2;
        else if (factor2 < s[3])
            chose_id = 3;

        TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
        {
            if (iopath->available && chose_id == iopath->id)
                return iopath;
        }
    }
}

struct ump_bdev_iopath *ump_find_iopath_hash(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io)
{
    struct ump_bdev_iopath *iopath;

    // hash
    unsigned int chose_id = (bdev_io->u.bdev.offset_blocks / bdev_io->u.bdev.num_blocks) % ump_channel->max_id;
    // printf("chose id:%u\n", chose_id);
    TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq)
    {
        if (iopath->available && chose_id == iopath->id)
            return iopath;
    }
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
    TAILQ_INSERT_TAIL(&g_ump_bdev_channels, ump_channel, tailq);    // ljx
    ump_channel->max_id = 0;

    // 创建poller用于统计
    io_count_poller = spdk_poller_register(ump_io_count_fn, NULL, 300000);    // 轮询的时间单位是微秒

    printf("add ump_channel\n");
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
        iopath->available = true;
        // iopath->io_time_read = 0;       // io时间初始化为最小，确保一开始每一条路径都会被加进去
        // iopath->io_time_write = 0;
        iopath->io_time = 0;
        iopath->id = ump_channel->max_id++;
        iopath->io_incomplete = 0;
        ump_time_queue_init(&iopath->io_read_time);
        ump_time_queue_init(&iopath->io_write_time);
        count[iopath->id] = 0;
        TAILQ_INSERT_TAIL(&ump_channel->iopath_list, iopath, tailq);
        sump_printf("%s's io channel is added\n", bdev->name);
    }

    return 0;

err:
    ump_bdev_channel_clear_all_iopath(ump_channel);
    return -1;
}
int ump_io_count_fn()
{
    for (int i = 0; i < 4; i++)
    {
        printf("path %d %ld KB\n", i, count[i]/1024);
    }

    // 定时清理io时间，防止某些路径一直不被使用
    struct ump_bdev_iopath *iopath;
    struct ump_bdev_channel *ch;
    TAILQ_FOREACH(ch, &g_ump_bdev_channels, tailq)
    {
        TAILQ_FOREACH(iopath, &ch->iopath_list, tailq)
        {
            ump_time_queue_init(&iopath->io_read_time);
            ump_time_queue_init(&iopath->io_write_time);
        }
    }
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
    TAILQ_REMOVE(&g_ump_bdev_channels, ump_channel, tailq);
    printf("remove ump_channel\n");

    spdk_poller_unregister(&io_count_poller);
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

/*************************************************************************/
/******************************failback begin*****************************/
/*************************************************************************/

// void ump_failback(struct ump_bdev_iopath *iopath, struct spdk_bdev_io *bdev_io, struct ump_bdev_channel *ump_channel)
// {
//     sump_printf("ump_failback! bdev name:%s\n", iopath->bdev->name);
//     // 1. 创建线程
//     char thread_name[128];
//     sprintf(thread_name, "failback_%s", iopath->bdev->name);
//     struct spdk_cpuset tmp_cpumask = {};
//     spdk_cpuset_zero(&tmp_cpumask);
//     spdk_cpuset_set_cpu(&tmp_cpumask, 2, true);
//     struct spdk_thread *thread = spdk_thread_create(thread_name, &tmp_cpumask);

//     // 2. 启动线程
//     struct ump_failback_ctx *ump_failback_ctx = malloc(sizeof(struct ump_failback_ctx));
//     // ump_failback_ctx->bdev_io = malloc(sizeof(struct spdk_bdev_io));
//     // memcpy(ump_failback_ctx->bdev_io, bdev_io, sizeof(struct spdk_bdev_io));
//     ump_failback_ctx->bdev_io = bdev_io;
//     ump_failback_ctx->iopath = iopath;
//     ump_failback_ctx->thread = thread;
//     ump_failback_ctx->ump_channel = ump_channel;
//     ump_failback_ctx->tqh_first = ump_channel->iopath_list.tqh_first;
//     // ump_failback_ctx->addr = &ump_failback_ctx;
//     // spdk_spin_init(&ump_failback_ctx->lock);    // 初始化自旋锁
//     ump_failback_ctx->poller = spdk_poller_register(ump_failback_io_fn, (void *)ump_failback_ctx, 1000); // 每1000微秒试一下是否已经重连
//     // spdk_thread_send_msg(thread, ump_failback_io_fn, (void *)ump_failback_ctx);

//     return;
// err:
//     /* todo io complete */
//     return;
// }

// int ump_failback_io_fn(void *arg1)
// {
//     struct ump_failback_ctx *ump_failback_ctx = arg1;
//     struct ump_bdev_iopath *iopath = ump_failback_ctx->iopath;
//     struct spdk_bdev_io *bdev_io = ump_failback_ctx->bdev_io;
//     // printf("iopath:%p\n", iopath);
//     // printf("iopath->bdev:%p\n", iopath->bdev);
//     // printf("iopath->bdev->name:%p\n", iopath->bdev->name);
//     // sump_printf("ump_failback_io_fn! bdev name:%s\n", iopath->bdev->name);
//     bdev_io->internal.cb = ump_failback_io_completion_cb;
//     bdev_io->internal.caller_ctx = ump_failback_ctx;

//     /* 提交I/O请求 */
//     // bdev_io->internal.ch->channel = iopath->io_channel;
//     iopath->bdev->fn_table->submit_request(iopath->io_channel, bdev_io);

//     return 0;
// }

// void ump_failback_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
// {
//     struct ump_failback_ctx *ump_failback_ctx = cb_arg;
//     static bool flag = true;
//     if (flag && success)
//     {
//         printf("addr1!!!:%p\n", (bdev_io->internal.ch));
//         // printf("addr2!!!:%p\n", (bdev_io->internal.ch)->channel);
//         sump_printf("io reconnect success.\n");
//         // printf("ump_failback_ctx addr:%p\n", ump_failback_ctx);
//         // printf("ump_failback_ctx->thread addr:%p\n", ump_failback_ctx->thread);
        
//         // spdk_spin_lock(&ump_failback_ctx->lock);
//         if (ump_failback_ctx)                           // 这里需要加锁，不然会double free （虽然不知道为什么）
//         {
//             ump_failback_ctx->iopath->available = true;
//             // struct ump_failback_ctx **addr_tmp = ump_failback_ctx->addr;
//             spdk_poller_pause(ump_failback_ctx->poller);
//             // spdk_poller_unregister(ump_failback_ctx->poller);
//             spdk_thread_exit(ump_failback_ctx->thread); // 退出当前线程
//             // spdk_spin_lock(&ump_failback_ctx->lock);
//             // spdk_spin_destroy(&ump_failback_ctx->lock);
//             // free(ump_failback_ctx->bdev_io);
//             free(ump_failback_ctx);
//             // *addr_tmp = NULL;
//         }
//         flag = false;
//     }
//     if (!success)
//     {
//         ump_failback_ctx->ump_channel->iopath_list.tqh_first = ump_failback_ctx->tqh_first;
//     }
//     // else
//     // {
//     //     sump_printf("io reconnect fail.\n");
//     // }
//     return;
// }

/*************************************************************************/
/******************************failback end*****************************/
/*************************************************************************/