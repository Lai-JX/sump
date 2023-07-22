#include "sump.h"

/* 全局变量，组织所有ump_bdev设备 */
struct ump_bdev_manage ump_bdev_manage;
/* 全局变量，保存真正处理设备注册的函数指针 */
int (*real_spdk_bdev_register)(struct spdk_bdev *bdev);
/* 全局变量，劫持_bdev_nvme_reset_complete函数，用于实现failback */
int (*real_spdk_nvme_ctrlr_reconnect_poll_async)(struct spdk_nvme_ctrlr *ctrlr);

/***********************************************************************
 *******************************spdk函数劫持****************************
 ***********************************************************************/

/* 最先执行的函数，先获取到真正注册函数的函数指针 */
void __attribute__((constructor)) ump_init(void)
{
    sump_printf("ump init start...\n");
    real_spdk_bdev_register = dlsym(RTLD_NEXT, "spdk_bdev_register");
    real_spdk_nvme_ctrlr_reconnect_poll_async = dlsym(RTLD_NEXT, "spdk_nvme_ctrlr_reconnect_poll_async");
    sump_printf("real_spdk_bdev_register = %p.\n", real_spdk_bdev_register);
    sump_printf("real_spdk_nvme_ctrlr_reconnect_poll_async = %p.\n", real_spdk_nvme_ctrlr_reconnect_poll_async);
    
    TAILQ_INIT(&g_ump_bdev_channels); // ljx
    printf("g_ump_bdev_iopaths:%p\n", &g_ump_bdev_channels);
    // printf("%s\n",dlerror());
}

int
spdk_nvme_ctrlr_reconnect_poll_async(struct spdk_nvme_ctrlr *ctrlr)
{
    struct nvme_ctrlr *nvme_ctrlr = (struct nvme_ctrlr *)ctrlr;
    if(nvme_ctrlr->size == sizeof(struct nvme_ctrlr))
    {
        int rc = real_spdk_nvme_ctrlr_reconnect_poll_async(nvme_ctrlr->ctrlr);
        if(rc == 0)
        {
            struct ump_bdev_iopath *iopath;
            struct ump_bdev_channel *ch;
            TAILQ_FOREACH(ch, &g_ump_bdev_channels, tailq)
            {
                TAILQ_FOREACH(iopath, &ch->iopath_list, tailq)
                {
                    printf("iopath->bdev->name:%s, nvme_ctrlr->nbdev_ctrlr->name:%s\n",iopath->bdev->name, nvme_ctrlr->nbdev_ctrlr->name);
                    if (!iopath->available && strstr(iopath->bdev->name, nvme_ctrlr->nbdev_ctrlr->name))
                    {
                        printf("iopath %s reconnect successful!\n",iopath->bdev->name);
                        iopath->available = true;
                    }
                }
            }
            printf("\nspdk_nvme_ctrlr_reconnect_poll_async:reset successfully\n\n");
        }
        return rc;
    }
    else
    {
        return real_spdk_nvme_ctrlr_reconnect_poll_async(ctrlr);
    }
}

/********************************************************
* Function name:    spdk_bdev_register
* Description:      组织用户注册的设备，其中uuid相同的设备会被聚合在一起
* Parameter:
*   @bdev           spdk_bdev结构体指针，即要进行注册的设备
* Return:           0表示成功，其它表示失败   
**********************************************************/
int spdk_bdev_register(struct spdk_bdev *bdev)
{
    sump_printf("successfully hijack function <spdk_bdev_register> of shared objects\n");
    int rc;
    struct ump_bdev *mbdev = NULL;

    if (real_spdk_bdev_register == NULL)
    {
        ump_init();
    }

    if (real_spdk_bdev_register == NULL)
    {
        fprintf(stderr, "ump init failed\n");
        return -1;
    }

    /* 通过uuid查找ump_bdev设备mbdev */
    mbdev = get_ump_bdev_by_uuid(&bdev->uuid); // uuid是全局唯一的

    char uuid_str[SPDK_UUID_STRING_LEN] = {0};
    spdk_uuid_fmt_lower(uuid_str, sizeof(uuid_str), &bdev->uuid);
    sump_printf("uuid of the new bdev: %s\n", uuid_str);
    sump_printf("name of the new bdev: %s\n", bdev->name);

    if (mbdev == NULL)                  // 没有mbdev，则进行构造
    {
        rc = ump_bdev_construct(bdev);
    }
    else                                // 有mbdev，则进行将当前设备（路径）添加到mbdev
    {
        rc = ump_bdev_add_bdev(mbdev, bdev);
    }

    if (rc != 0)
    {
        fprintf(stderr, "ump bdev construct failed.\n");
        return rc;
    }

    return 0;
}

/*************************************************************************/
/*********************construct mbdev begin*******************************/
/*************************************************************************/

/********************************************************
* Function name:    ump_bdev_construct
* Description:      根据传入的bdev设备，构建一个ump_bdev设备
* Parameter:
*   @bdev           spdk_bdev结构体指针bdev
* Return:           0表示成功，其它表示失败   
**********************************************************/
int ump_bdev_construct(struct spdk_bdev *bdev)
{
    char uuid_str[SPDK_UUID_STRING_LEN] = {0};
    struct ump_bdev *mbdev = NULL;
    struct spdk_list_bdev *list_bdev = NULL;
    int rc;

    mbdev = calloc(1, sizeof(struct ump_bdev));
    if (mbdev == NULL)
    {
        fprintf(stderr, "calloc for ump_bdev failed.\n");
        return -1;
    }

    /* mbdev copy attr from spdk bdev */
    spdk_uuid_fmt_lower(uuid_str, sizeof(uuid_str), &bdev->uuid); // (小写转换)
    mbdev->bdev.ctxt = mbdev;                                     /* set mbdev->bdev ctxt */
    mbdev->bdev.name = spdk_sprintf_alloc("ump-bdev-%s", uuid_str);
    mbdev->bdev.product_name = strdup(bdev->product_name);
    mbdev->bdev.write_cache = bdev->write_cache;
    mbdev->bdev.blocklen = bdev->blocklen;
    // mbdev->bdev.phys_blocklen = bdev->phys_blocklen; // sd
    mbdev->bdev.blockcnt = bdev->blockcnt;
    // mbdev->bdev.split_on_write_unit = bdev->split_on_write_unit; // sd
    mbdev->bdev.write_unit_size = bdev->write_unit_size;
    mbdev->bdev.acwu = bdev->acwu;
    mbdev->bdev.required_alignment = bdev->required_alignment;
    mbdev->bdev.split_on_optimal_io_boundary = bdev->split_on_optimal_io_boundary;
    mbdev->bdev.optimal_io_boundary = bdev->optimal_io_boundary;
    // mbdev->bdev.max_segment_size = bdev->max_segment_size;     // sd
    // mbdev->bdev.max_num_segments = bdev->max_num_segments;     // sd
    // mbdev->bdev.max_unmap = bdev->max_unmap;                   // sd
    // mbdev->bdev.max_unmap_segments = bdev->max_unmap_segments; // sd
    // mbdev->bdev.max_write_zeroes = bdev->max_write_zeroes;     // sd
    // mbdev->bdev.max_copy = bdev->max_copy;                     // sd
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
    // mbdev->bdev.reset_io_drain_timeout = bdev->reset_io_drain_timeout; // sd
    // mbdev->bdev.module = bdev->module;                                 // sd
    mbdev->bdev.fn_table = &umplib_fn_table; // 这点很重要，将回调函数截取
    // mbdev->bdev.internal = bdev->internal;                             // sd

    // 初始化当前mbdev的bdev队列
    TAILQ_INIT(&mbdev->spdk_bdev_list);
    list_bdev = calloc(1, sizeof(struct spdk_list_bdev));
    if (list_bdev == NULL)
    {
        rc = -1;
        goto err;
    }
    list_bdev->bdev = bdev;
    // bdev加入当前mbdev的bdev队列
    TAILQ_INSERT_TAIL(&mbdev->spdk_bdev_list, list_bdev, tailq);

    /* register mbdev io_device */
    spdk_io_device_register(mbdev, ump_bdev_channel_create_cb, ump_bdev_channel_destroy_cb,
                            sizeof(struct ump_bdev_channel), mbdev->bdev.name);

    // 这里不一定要初始化ump_bdev_manage.ump_bdev_list吧，在总的第一次启动时初始化就好？→ (if(TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list)))
    if (TAILQ_EMPTY(&ump_bdev_manage.ump_bdev_list))
        TAILQ_INIT(&ump_bdev_manage.ump_bdev_list);
    // 将当前mbdev加入mbdev队列
    TAILQ_INSERT_TAIL(&ump_bdev_manage.ump_bdev_list, mbdev, tailq);
    rc = real_spdk_bdev_register(&mbdev->bdev);
    if (rc != 0)
    {
        fprintf(stderr, "register mbdev(%s) failed.\n", mbdev->bdev.name);
        goto err;
    }

    sump_printf("create new mbdev(%s) success.\n", mbdev->bdev.name);

    return 0;

err:
    if (list_bdev)
    {
        free(list_bdev);
    }
    free(mbdev->bdev.name);
    free(mbdev->bdev.product_name);
    free(mbdev);
    return rc;
}

/********************************************************
* Function name:    ump_bdev_add_bdev
* Description:      将传入的spdk_bdev设备封装为spdk_list_bdev结构体，并添加到ump_bdev设备的spdk_bdev_list中
* Parameter:
*   @mbdev          ump_bdev结构体指针
*   @bdev           spdk_bdev结构体指针
* Return:           0表示成功，其它表示失败   
**********************************************************/
int ump_bdev_add_bdev(struct ump_bdev *mbdev, struct spdk_bdev *bdev)
{
    // 带链表的bdev
    struct spdk_list_bdev *list_bdev = NULL;

    list_bdev = calloc(1, sizeof(struct spdk_list_bdev));
    if (list_bdev == NULL)
    {
        fprintf(stderr, "failed calloc for list_bdev.\n");
        return -1;
    }
    list_bdev->bdev = bdev;
    TAILQ_INSERT_TAIL(&mbdev->spdk_bdev_list, list_bdev, tailq);
    
    sump_printf("mbdev(%s) add new bdev success.\n", mbdev->bdev.name);

    // 把路径添加进ump_bdev_channel
    struct ump_bdev_iopath *iopath;
    struct ump_bdev_channel *ch;
    TAILQ_FOREACH(ch, &g_ump_bdev_channels, tailq)
    {
        TAILQ_FOREACH(iopath, &ch->iopath_list, tailq)
        {
            if(!spdk_uuid_compare(&mbdev->bdev.uuid, &iopath->bdev->uuid))   // 相等
            {
                struct ump_bdev_iopath *iopath = calloc(1, sizeof(struct ump_bdev_iopath));
                iopath->io_channel = bdev->fn_table->get_io_channel(bdev->ctxt);
                iopath->bdev = bdev;
                iopath->available = true;
                iopath->io_time = 0;       // io时间初始化为最小，确保一开始每一条路径都会被加进去
                iopath->id = ch->max_id++;
                iopath->io_incomplete = 0;
                TAILQ_INSERT_TAIL(&ch->iopath_list, iopath, tailq);
                return 0;
            }
        }
    }


    return 0;
}

/*************************************************************************/
/*********************construct mbdev end*********************************/
/*************************************************************************/

