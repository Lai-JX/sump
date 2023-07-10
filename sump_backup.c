
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <spdk/bdev.h>
#include <spdk/bdev_module.h>
#include <spdk/string.h>
#include <stdlib.h>

// TAILQ_HEAD 定义队列头
// TAILQ_ENTRY 队列实体定义
// TAILQ_INIT 初始化队列
// TAILQ_FOREACH 对队列进行遍历操作
// TAILQ_INSERT_BEFORE 在指定元素之前插入元素
// TAILQ_INSERT_TAIL 在队列尾部插入元素
// TAILQ_EMPTY 检查队列是否为空
// TAILQ_REMOVE 从队列中移除元素


int split_io_request(struct spdk_bdev_io *bdev_io, struct spdk_bdev_io *bdev_io1, struct spdk_bdev_io *bdev_io2)
{
    // 以下先只考虑一个iovs的情况
    

    // 原本的块数，和块偏移
    uint64_t num_blocks = bdev_io->u.bdev.num_blocks;
    uint64_t offset_blocks = bdev_io->u.bdev.offset_blocks;
    uint32_t blocklen = bdev_io->bdev->blocklen;

    printf("type: %d\n", bdev_io->type);
    printf("bdev name: %s\n", bdev_io->bdev->name);
    printf("Number of IO submission retries: %d\n", bdev_io->num_retries);
    printf("num_blocks: %ld\n", num_blocks);
    printf("offset_blocks: %ld\n", offset_blocks);
    printf("iovcnt: %d\n", bdev_io->u.bdev.iovcnt);
    printf("Number of iovecs in fused_iovs: %d\n", bdev_io->u.bdev.fused_iovcnt);
    printf("metadata buf addr: %p\n", bdev_io->u.bdev.md_buf);

    if (num_blocks < 2)
    {
        return -1;
    }

    uint64_t num_blocks1 = num_blocks / 2;
    uint64_t num_blocks2 = num_blocks - num_blocks1;

    memcpy(bdev_io1, bdev_io, sizeof(struct spdk_bdev_io));
    memcpy(bdev_io2, bdev_io, sizeof(struct spdk_bdev_io));

    // 第一个
    bdev_io1->u.bdev.iovs[0].iov_len = num_blocks1 * blocklen;
    bdev_io1->u.bdev.num_blocks = num_blocks1;
    

    // 第二个
    struct iovec *iov = calloc(1, sizeof(struct iovec));
    iov->iov_base = (bdev_io->u.bdev.iovs[0].iov_base) + (bdev_io1->u.bdev.iovs[0].iov_len);
    iov->iov_len = num_blocks2 * blocklen;
    bdev_io2->u.bdev.iovs = iov;
    bdev_io2->u.bdev.offset_blocks = offset_blocks + num_blocks1;
    bdev_io2->u.bdev.num_blocks = num_blocks2;

    return 0;
}


// 总的 ump_bdev 队列
struct ump_bdev_manage {
    TAILQ_HEAD(, ump_bdev) ump_bdev_list;
};
struct ump_bdev_manage ump_bdev_manage;

/* 多路径聚合后的bdev */
struct ump_bdev {
    struct spdk_bdev bdev;                          // 原本的bdev属性必须放前面，这样之后调用属性时才可以按照原来的调用方式
    TAILQ_HEAD(, spdk_list_bdev) spdk_bdev_list;    // 每个ump_bdev里有一个bdev队列（指向uuid相同）
    TAILQ_ENTRY(ump_bdev) tailq;
};

/* spdk_bdev缺少TAILQ成员，无法使用链表，对spdk_bdev封装一层，方便使用链表 */
struct spdk_list_bdev {
    struct spdk_bdev *bdev;
    TAILQ_ENTRY(spdk_list_bdev) tailq;
};

// channel队列
struct ump_bdev_channel {
    TAILQ_HEAD(, ump_bdev_iopath) iopath_list;
};

/* ump_bdev逻辑路径结构 */
struct ump_bdev_iopath {
    struct spdk_io_channel *io_channel;
    struct spdk_bdev *bdev;
    TAILQ_ENTRY(ump_bdev_iopath) tailq;
};

int (*real_spdk_bdev_register)(struct spdk_bdev *bdev);

static int ump_bdev_destruct(void *ctx)
{
    printf("===>ump_bdev_destruct\n");
    return 0;
}


static struct ump_bdev_iopath *ump_bdev_find_iopath(struct ump_bdev_channel *ump_channel)
{
    // if (TAILQ_EMPTY(&ump_channel->iopath_list)) {
    //     return NULL;
    // }
    // struct ump_bdev_iopath *iopath;
    // TAILQ_FOREACH(iopath, &ump_channel->iopath_list, tailq) {
    //     if(!iopath->using) {
    //         iopath->using = 1;
    //         return iopath;
    //     }
    // }

    // return NULL;
    if (TAILQ_EMPTY(&ump_channel->iopath_list)) {
        return NULL;
    }
    return TAILQ_FIRST(&ump_channel->iopath_list);

}

struct ump_bdev_io_completion_ctx {
    struct spdk_io_channel *ch;
    struct spdk_bdev_io *bdev_io;
    void *real_caller_ctx;
    spdk_bdev_io_completion_cb real_completion_cb;
    uint32_t split_num;
    uint32_t complete_num;
};

static void ump_bdev_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
    struct ump_bdev_io_completion_ctx *completion_ctx = cb_arg;
    if (success) {
        printf("io complete success.\n");
        completion_ctx->complete_num++;
        if (completion_ctx->complete_num == completion_ctx->split_num)
        {
            completion_ctx->real_completion_cb(completion_ctx->bdev_io, success, completion_ctx->real_caller_ctx);
            free(completion_ctx);
        }
    } else {
        /* todo 失败io处理 */
        fprintf(stderr, "io complete failed.\n");
        free(completion_ctx);
    }
}

static void ump_bdev_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
    struct ump_bdev_channel *ump_channel = NULL;
    struct ump_bdev_iopath *iopath= NULL;
    struct spdk_bdev *bdev = NULL;
    struct ump_bdev *mbdev = NULL;
    struct ump_bdev_io_completion_ctx *ump_completion_ctx = NULL;

    printf("==========> ump_bdev_submit_request\n");
    /* 替换io完成的回调，方便对返回的io处理 */
    ump_completion_ctx = calloc(1, sizeof(struct ump_bdev_io_completion_ctx));
    if (ump_completion_ctx == NULL) {
        fprintf(stderr, "calloc for ump_completion_ctx failed.\n");
        goto err;
    }
    ump_completion_ctx->real_caller_ctx = bdev_io->internal.caller_ctx;
    ump_completion_ctx->real_completion_cb = bdev_io->internal.cb;
    ump_completion_ctx->ch = ch;
    bdev_io->internal.cb = ump_bdev_io_completion_cb;
    bdev_io->internal.caller_ctx = ump_completion_ctx;

    ump_channel = spdk_io_channel_get_ctx(ch);
    mbdev = spdk_io_channel_get_io_device(ch);

    /************************************************************************************/
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

    iopath = ump_bdev_find_iopath(ump_channel);
    if (iopath == NULL) {
        fprintf(stderr, "mbdev(%s) dont has any iopath.\n", mbdev->bdev.name);
        goto err;
    }

    bdev = iopath->bdev;
    bdev->fn_table->submit_request(iopath->io_channel, bdev_io);

err:
    /* todo io complete */
    return;
}

static bool ump_bdev_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type)
{
    struct ump_bdev *mbdev = ctx;
    struct spdk_list_bdev *list_bdev = TAILQ_FIRST(&mbdev->spdk_bdev_list);
    struct spdk_bdev *bdev = list_bdev->bdev;

	return bdev->fn_table->io_type_supported(bdev->ctxt, io_type);
}

static struct spdk_io_channel *ump_bdev_get_io_channel(void *ctx)
{
	return spdk_get_io_channel(ctx);
}

static int ump_bdev_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
    struct ump_bdev *mbdev = ctx;
    struct spdk_list_bdev *list_bdev = TAILQ_FIRST(&mbdev->spdk_bdev_list);
    struct spdk_bdev *bdev = list_bdev->bdev;

    return bdev->fn_table->dump_info_json(bdev->ctxt, w);
}

static void ump_bdev_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w)
{
    printf("===>ump_bdev_write_config_json\n");
}

static uint64_t ump_bdev_get_spin_time(struct spdk_io_channel *ch)
{
    printf("===>ump_bdev_get_spin_time\n");
	return 0;
}

static const struct spdk_bdev_fn_table umplib_fn_table = {
    .destruct           = ump_bdev_destruct,
    .submit_request     = ump_bdev_submit_request,
    .io_type_supported  = ump_bdev_io_type_supported,
    .get_io_channel     = ump_bdev_get_io_channel,
    .dump_info_json     = ump_bdev_dump_info_json,
    .write_config_json  = ump_bdev_write_config_json,
    .get_spin_time      = ump_bdev_get_spin_time,
};

static struct ump_bdev *get_ump_bdev_by_uuid(struct spdk_uuid *uuid)
{
    struct ump_bdev *mbdev = NULL;
    TAILQ_FOREACH(mbdev, &ump_bdev_manage.ump_bdev_list, tailq) {
        if (!spdk_uuid_compare(&mbdev->bdev.uuid, uuid)) {
            return mbdev;
        }
    }
    return NULL;
}

static int ump_bdev_add_bdev(struct ump_bdev *mbdev, struct spdk_bdev *bdev)
{
    // 带链表的bdev
    struct spdk_list_bdev *list_bdev = NULL;

    list_bdev = calloc(1, sizeof(struct spdk_list_bdev));
    if (list_bdev == NULL) {
        fprintf(stderr, "failed calloc for list_bdev.\n");
        return -1;
    }
    list_bdev->bdev = bdev;
    TAILQ_INSERT_TAIL(&mbdev->spdk_bdev_list, list_bdev, tailq);
    printf("mbdev(%s) add new path success.\n", mbdev->bdev.name);

    return 0;
}

static void ump_bdev_channel_clear_all_iopath(struct ump_bdev_channel *ump_channel)
{
    struct ump_bdev_iopath *iopath = NULL;

    while (!TAILQ_EMPTY(&ump_channel->iopath_list)) {
        iopath = TAILQ_FIRST(&ump_channel->iopath_list);
        spdk_put_io_channel(iopath->io_channel);
        TAILQ_REMOVE(&ump_channel->iopath_list, iopath, tailq);
        free(iopath);
    }
}

// 创建完channel的回调函数（get_io_channel时若没有channel会调用） 通过回调函数来分配新I/O通道所需的任何资源 （这里主要是更新iopath）
static int ump_bdev_channel_create_cb(void *io_device, void *ctx_buf)
{
    struct ump_bdev *mbdev = io_device; // io_device和mbdev的第一个参数为bdev
    struct ump_bdev_channel *ump_channel = ctx_buf;
    struct spdk_list_bdev *list_bdev = NULL;
    struct spdk_bdev *bdev = NULL;
    struct spdk_io_channel *io_channel = NULL;
    struct ump_bdev_iopath *iopath = NULL;
    // ljx: 添加if(TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list))
    if(TAILQ_EMPTY(&ump_channel->iopath_list))
        TAILQ_INIT(&ump_channel->iopath_list);  // 当前mbdev在当前线程的各条路径
    // 遍历mbdev的所有bdev
    TAILQ_FOREACH(list_bdev, &mbdev->spdk_bdev_list, tailq) {
        bdev = list_bdev->bdev;
        io_channel = bdev->fn_table->get_io_channel(bdev->ctxt);    // 这里的bdev->ctxt为mbdev
        if (io_channel == NULL) {
            fprintf(stderr, "ump bdev channel create get iopath channel failed.\n");
            goto err;
        }

        iopath = calloc(1, sizeof(struct ump_bdev_iopath));
        if (iopath == NULL) {
            fprintf(stderr, "calloc for iopath failed.\n");
            spdk_put_io_channel(io_channel);
            goto err;
        }

        iopath->io_channel = io_channel;
        iopath->bdev = bdev;
        TAILQ_INSERT_TAIL(&ump_channel->iopath_list, iopath, tailq);
    }

    return 0;

err:
    ump_bdev_channel_clear_all_iopath(ump_channel);
    return -1;
}

static void ump_bdev_channel_destroy_cb(void *io_device, void *ctx_buf)
{
    struct ump_bdev_channel *ump_channel = ctx_buf;

    ump_bdev_channel_clear_all_iopath(ump_channel);
}

// 构建一个mbdev设备
static int ump_bdev_construct(struct spdk_bdev *bdev)
{
    char uuid_str[SPDK_UUID_STRING_LEN] = {0};
    struct ump_bdev *mbdev = NULL;
    struct spdk_list_bdev *list_bdev = NULL;
    int rc;

    mbdev = calloc(1, sizeof(struct ump_bdev));
    if (mbdev == NULL) {
        fprintf(stderr, "calloc for ump_bdev failed.\n");
        return -1;
    }

    /* mbdev copy attr from spdk bdev */ 
    spdk_uuid_fmt_lower(uuid_str, sizeof(uuid_str), &bdev->uuid);   // (小写转换)
    mbdev->bdev.name = spdk_sprintf_alloc("ump-bdev-%s", uuid_str);
    mbdev->bdev.product_name = strdup(bdev->product_name);
    mbdev->bdev.write_cache = bdev->write_cache;
    mbdev->bdev.blocklen = bdev->blocklen;
    mbdev->bdev.blockcnt = bdev->blockcnt;
    mbdev->bdev.write_unit_size = bdev->write_unit_size;
    mbdev->bdev.acwu = bdev->acwu;
    mbdev->bdev.required_alignment = bdev->required_alignment;
    mbdev->bdev.split_on_optimal_io_boundary = bdev->split_on_optimal_io_boundary;
    mbdev->bdev.optimal_io_boundary = bdev->optimal_io_boundary;
    mbdev->bdev.uuid = bdev->uuid;
    mbdev->bdev.md_len = bdev->md_len;
    mbdev->bdev.md_interleave = bdev->md_interleave;
    mbdev->bdev.dif_type = bdev->dif_type;
    mbdev->bdev.dif_is_head_of_md = bdev->dif_is_head_of_md;
    mbdev->bdev.dif_check_flags = bdev->dif_check_flags;
    mbdev->bdev.zoned = bdev->zoned;
    mbdev->bdev.zone_size = bdev->zone_size;
    mbdev->bdev.max_open_zones = bdev->max_open_zones;
    mbdev->bdev.optimal_open_zones = bdev->optimal_open_zones;
    mbdev->bdev.media_events = bdev->media_events;
    mbdev->bdev.fn_table = &umplib_fn_table;                    // 这点很重要，将回调函数截取
    
    /* set mbdev->bdev ctxt */
    mbdev->bdev.ctxt = mbdev;

    // 初始化当前mbdev的bdev队列
    TAILQ_INIT(&mbdev->spdk_bdev_list);
    list_bdev = calloc(1, sizeof(struct spdk_list_bdev));
    if (list_bdev == NULL) {
        rc = -1;
        goto err;
    }
    list_bdev->bdev = bdev;
    // bdev加入当前bdev的bdev队列
    TAILQ_INSERT_TAIL(&mbdev->spdk_bdev_list, list_bdev, tailq);
    
    /* register mbdev io_device */
    spdk_io_device_register(mbdev, ump_bdev_channel_create_cb, ump_bdev_channel_destroy_cb,
                            sizeof(struct ump_bdev_channel), mbdev->bdev.name);

    // 这里不一定要初始化ump_bdev_manage.ump_bdev_list吧，在总的第一次启动时初始化就好？→ (if(TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list)))
    if(TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list))
        TAILQ_INIT(&ump_bdev_manage.ump_bdev_list);
    // 将当前mbdev加入mbdev队列
    TAILQ_INSERT_TAIL(&ump_bdev_manage.ump_bdev_list, mbdev, tailq);
    rc = real_spdk_bdev_register(&mbdev->bdev);
    if (rc != 0) {
        fprintf(stderr, "register mbdev(%s) failed.\n", mbdev->bdev.name);
        goto err;
    }
 
    printf("create new mbdev(%s) success.\n", mbdev->bdev.name);

    return 0;

err:
    if (list_bdev) {
        free(list_bdev);
    }
    free(mbdev->bdev.name);
    free(mbdev->bdev.product_name);
    free(mbdev);
    return rc;
}

/***********************************************************************
 *******************************spdk函数替换****************************
 ***********************************************************************/
static void __attribute__((constructor)) ump_init(void)
{
	printf("ump init start...\n");
	real_spdk_bdev_register = dlsym(RTLD_NEXT, "spdk_bdev_register");
    printf("real_spdk_bdev_register = %p.\n", real_spdk_bdev_register);
    // printf("%s\n",dlerror());
}

int spdk_bdev_register(struct spdk_bdev *bdev)
{
    int rc;
    struct ump_bdev *mbdev = NULL;

	if (real_spdk_bdev_register == NULL) {
		ump_init();
	}

    if (real_spdk_bdev_register == NULL) {
        fprintf(stderr, "ump init failed.\n");
        return -1;
    }

    mbdev = get_ump_bdev_by_uuid(&bdev->uuid);  // uuid是全局唯一的
    if (mbdev == NULL) {
        rc = ump_bdev_construct(bdev);
    } else {
        rc = ump_bdev_add_bdev(mbdev, bdev);
    }

    if (rc != 0) {
        fprintf(stderr, "ump bdev construct failed.\n");
        return rc;
    }

	return 0;
}
