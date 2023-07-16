#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <spdk/bdev.h>
#include <spdk/thread.h>
#include <spdk/bdev_module.h>
#include <spdk/string.h>
#include <stdlib.h>
#include "sump_nvme_ctrlr.h"


// #include "spdk_internal/event.h"

// TAILQ_HEAD 定义队列头
// TAILQ_ENTRY 队列实体定义
// TAILQ_INIT 初始化队列
// TAILQ_FOREACH 对队列进行遍历操作
// TAILQ_INSERT_BEFORE 在指定元素之前插入元素
// TAILQ_INSERT_TAIL 在队列尾部插入元素
// TAILQ_EMPTY 检查队列是否为空
// TAILQ_REMOVE 从队列中移除元素

// 由于某些原因这里需要再定义一遍
struct spdk_bdev_channel {
	struct spdk_bdev	*bdev;

	/* The channel for the underlying device */
	struct spdk_io_channel	*channel;

	/* Per io_device per thread data */
	struct spdk_bdev_shared_resource *shared_resource;

	struct spdk_bdev_io_stat *stat;

	/*
	 * Count of I/O submitted to the underlying dev module through this channel
	 * and waiting for completion.
	 */
	uint64_t		io_outstanding;

	/*
	 * List of all submitted I/Os including I/O that are generated via splitting.
	 */
	bdev_io_tailq_t		io_submitted;

	/*
	 * List of spdk_bdev_io that are currently queued because they write to a locked
	 * LBA range.
	 */
	bdev_io_tailq_t		io_locked;

	uint32_t		flags;

	struct spdk_histogram_data *histogram;

#ifdef SPDK_CONFIG_VTUNE
	uint64_t		start_tsc;
	uint64_t		interval_tsc;
	__itt_string_handle	*handle;
	struct spdk_bdev_io_stat *prev_stat;
#endif

	bdev_io_tailq_t		queued_resets;

	lba_range_tailq_t	locked_ranges;
};


/* 总的 ump_bdev 队列 */
struct ump_bdev_manage
{
    TAILQ_HEAD(, ump_bdev)
    ump_bdev_list;
};

/* 多路径聚合后的bdev */
struct ump_bdev
{
    struct spdk_bdev bdev; // 原本的bdev必须放前面，这样之后调用属性时才可以按照原来的调用方式
    TAILQ_HEAD(, spdk_list_bdev)
    spdk_bdev_list; // 每个ump_bdev里有一个bdev队列（指向uuid相同）
    TAILQ_ENTRY(ump_bdev)
    tailq;
};

/* spdk_bdev缺少TAILQ成员，无法使用链表，对spdk_bdev封装一层，方便使用链表 */
struct spdk_list_bdev
{
    struct spdk_bdev *bdev;
    TAILQ_ENTRY(spdk_list_bdev)
    tailq;
};
// ljx
enum bdev_nvme_multipath_policy {
	BDEV_NVME_MP_POLICY_ACTIVE_PASSIVE,
	BDEV_NVME_MP_POLICY_ACTIVE_ACTIVE,
};

enum bdev_nvme_multipath_selector {
	BDEV_NVME_MP_SELECTOR_ROUND_ROBIN = 1,
	BDEV_NVME_MP_SELECTOR_QUEUE_DEPTH,
};
/* iopath队列 */ 
struct ump_bdev_channel
{
    struct nvme_io_path *current_io_path;               // 填充用，可能会有其它问题，先试试
    enum bdev_nvme_multipath_policy		mp_policy;      // 填充用，可能会有其它问题，先试试
	enum bdev_nvme_multipath_selector	mp_selector;    // 填充用，可能会有其它问题，先试试
	uint32_t				rr_min_io;                  // 填充用，可能会有其它问题，先试试
	uint32_t				rr_counter;                 // 填充用，可能会有其它问题，先试试
    TAILQ_HEAD(, ump_bdev_iopath)
    iopath_list;
    TAILQ_ENTRY(ump_bdev_channel) tailq;                // ljx
};

/* ump_bdev逻辑路径结构 */
struct ump_bdev_iopath
{
    TAILQ_ENTRY(ump_bdev_iopath) tailq;
    struct spdk_io_channel *io_channel;
    struct spdk_bdev *bdev;
    // uint64_t reconnect_thread_id;   // 用于重连时的线程id
    bool available;
};

/* 参数上下文，用于保留必要变量并传递给回调函数 */
struct ump_bdev_io_completion_ctx
{
    struct spdk_io_channel *ch;
    struct spdk_bdev_io *bdev_io;
    struct ump_bdev_channel ump_channel;
    void *real_caller_ctx;
    spdk_bdev_io_completion_cb real_completion_cb;
    struct ump_bdev_iopath *iopath; // sd
};


// ljx sump_ctrl.c
struct ump_failback_ctx
{
    struct ump_bdev_iopath *iopath;     
    struct spdk_bdev_io *bdev_io;       // 用于发出io请求
    struct spdk_thread *thread;
    struct ump_bdev_channel *ump_channel;
    struct ump_bdev_iopath *tqh_first;
    struct spdk_poller *poller;
    // struct ump_failback_ctx **addr;                         // 保存指向当前结构体地址的指针
    // struct spdk_spinlock lock;          // 用于同步，避免double free等
};
struct ump_channel_list { struct ump_bdev_channel *tqh_first; struct ump_bdev_channel * *tqh_last;
};
/* 全局变量，组织所有ump_bdev设备 */
extern struct ump_bdev_manage ump_bdev_manage;
/* 全局变量，保存真正处理设备注册的函数指针 */
extern int (*real_spdk_bdev_register)(struct spdk_bdev *bdev);
TAILQ_HEAD(, ump_bdev_channel) g_ump_bdev_channels;

/* sump_data.c */
void __attribute__((constructor)) ump_init(void);
void ump_bdev_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io);
struct spdk_io_channel *ump_bdev_get_io_channel(void *ctx);

/* sump_ctrl.c */
void ump_bdev_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);
struct ump_bdev_iopath *ump_bdev_find_iopath(struct ump_bdev_channel *ump_channel);
void ump_bdev_channel_clear_all_iopath(struct ump_bdev_channel *ump_channel);
int ump_bdev_channel_create_cb(void *io_device, void *ctx_buf);
void ump_bdev_channel_destroy_cb(void *io_device, void *ctx_buf);

int ump_bdev_destruct(void *ctx);
bool ump_bdev_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type);
int ump_bdev_dump_info_json(void *ctx, struct spdk_json_write_ctx *w);
void ump_bdev_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w);
uint64_t ump_bdev_get_spin_time(struct spdk_io_channel *ch);

void ump_failback(struct ump_bdev_iopath *iopath, struct spdk_bdev_io *bdev_io, struct ump_bdev_channel *ump_channel);
void ump_failback_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);
int ump_failback_io_fn(void *arg1);

/* sump_util.c */
struct ump_bdev *get_ump_bdev_by_uuid(struct spdk_uuid *uuid);
void sump_printf(const char *fmt, ...);

/* sump.c */
int ump_bdev_construct(struct spdk_bdev *bdev);
int ump_bdev_add_bdev(struct ump_bdev *mbdev, struct spdk_bdev *bdev);

/***************************************************************************************
*  spdk中的块设备后端的函数表（spdk_bdev结构体的fn_table成员）提供了一组允许与后端通信的API，
*  为了截取I/O请求，以自己的逻辑实现路径下发，进而实现路径选择和I/O加速等功能。
*  我们实现了自己的函数表umplib_fn_table，并在设备构造时将其赋值给spdk_bdev结构体的fn_table成员。
***************************************************************************************/
static const struct spdk_bdev_fn_table umplib_fn_table = {
    .destruct = ump_bdev_destruct,
    .submit_request = ump_bdev_submit_request,
    .io_type_supported = ump_bdev_io_type_supported,
    .get_io_channel = ump_bdev_get_io_channel,
    .dump_info_json = ump_bdev_dump_info_json,
    .write_config_json = ump_bdev_write_config_json,
    .get_spin_time = ump_bdev_get_spin_time,
};
