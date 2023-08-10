#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <spdk/bdev.h>
#include <spdk/thread.h>
#include <spdk/bdev_module.h>
#include <spdk/string.h>
#include <spdk/tree.h>
#include <stdlib.h>
#include <sys/time.h>
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

// ljx 👇
#define SPDK_MAX_THREAD_NAME_LEN	256
#define SPDK_MAX_POLLER_NAME_LEN	256
enum bdev_nvme_multipath_policy {
	BDEV_NVME_MP_POLICY_ACTIVE_PASSIVE,
	BDEV_NVME_MP_POLICY_ACTIVE_ACTIVE,
};

enum bdev_nvme_multipath_selector {
	BDEV_NVME_MP_SELECTOR_ROUND_ROBIN = 1,
	BDEV_NVME_MP_SELECTOR_QUEUE_DEPTH,
};

enum spdk_poller_state {
	/* The poller is registered with a thread but not currently executing its fn. */
	SPDK_POLLER_STATE_WAITING,

	/* The poller is currently running its fn. */
	SPDK_POLLER_STATE_RUNNING,

	/* The poller was unregistered during the execution of its fn. */
	SPDK_POLLER_STATE_UNREGISTERED,

	/* The poller is in the process of being paused.  It will be paused
	 * during the next time it's supposed to be executed.
	 */
	SPDK_POLLER_STATE_PAUSING,

	/* The poller is registered but currently paused.  It's on the
	 * paused_pollers list.
	 */
	SPDK_POLLER_STATE_PAUSED,
};
enum spdk_thread_state {
	/* The thread is processing poller and message by spdk_thread_poll(). */
	SPDK_THREAD_STATE_RUNNING,

	/* The thread is in the process of termination. It reaps unregistering
	 * poller are releasing I/O channel.
	 */
	SPDK_THREAD_STATE_EXITING,

	/* The thread is exited. It is ready to call spdk_thread_destroy(). */
	SPDK_THREAD_STATE_EXITED,
};
struct spdk_poller {
	TAILQ_ENTRY(spdk_poller)	tailq;
	RB_ENTRY(spdk_poller)		node;

	/* Current state of the poller; should only be accessed from the poller's thread. */
	enum spdk_poller_state		state;

	uint64_t			period_ticks;
	uint64_t			next_run_tick;
	uint64_t			run_count;
	uint64_t			busy_count;
	uint64_t			id;
	spdk_poller_fn			fn;
	void				*arg;
	struct spdk_thread		*thread;
	/* Native interruptfd for period or busy poller */
	int				interruptfd;
	spdk_poller_set_interrupt_mode_cb set_intr_cb_fn;
	void				*set_intr_cb_arg;

	char				name[SPDK_MAX_POLLER_NAME_LEN + 1];
};
struct spdk_thread {
	uint64_t			tsc_last;
	struct spdk_thread_stats	stats;
	/*
	 * Contains pollers actively running on this thread.  Pollers
	 *  are run round-robin. The thread takes one poller from the head
	 *  of the ring, executes it, then puts it back at the tail of
	 *  the ring.
	 */
	TAILQ_HEAD(active_pollers_head, spdk_poller)	active_pollers;
	/**
	 * Contains pollers running on this thread with a periodic timer.
	 */
	RB_HEAD(timed_pollers_tree, spdk_poller)	timed_pollers;
	struct spdk_poller				*first_timed_poller;
	/*
	 * Contains paused pollers.  Pollers on this queue are waiting until
	 * they are resumed (in which case they're put onto the active/timer
	 * queues) or unregistered.
	 */
	TAILQ_HEAD(paused_pollers_head, spdk_poller)	paused_pollers;
	struct spdk_ring		*messages;
	int				msg_fd;
	SLIST_HEAD(, spdk_msg)		msg_cache;
	size_t				msg_cache_count;
	spdk_msg_fn			critical_msg;
	uint64_t			id;
	uint64_t			next_poller_id;
	enum spdk_thread_state		state;
	int				pending_unregister_count;

	RB_HEAD(io_channel_tree, spdk_io_channel)	io_channels;
	TAILQ_ENTRY(spdk_thread)			tailq;

	char				name[SPDK_MAX_THREAD_NAME_LEN + 1];
	struct spdk_cpuset		cpumask;
	uint64_t			exit_timeout_tsc;

	int32_t				lock_count;

	/* Indicates whether this spdk_thread currently runs in interrupt. */
	bool				in_interrupt;
	bool				poller_unregistered;
	struct spdk_fd_group		*fgrp;

	/* User context allocated at the end */
	uint8_t				ctx[0];
};

static inline int
timed_poller_compare(struct spdk_poller *poller1, struct spdk_poller *poller2)
{
	if (poller1->next_run_tick < poller2->next_run_tick) {
		return -1;
	} else {
		return 1;
	}
}

RB_GENERATE_STATIC(timed_pollers_tree, spdk_poller, node, timed_poller_compare);
// ljx 👆

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

/* iopath队列 */ 
struct ump_bdev_channel
{
    struct nvme_io_path			*current_io_path;           // ljx 填充用，可能会有其它问题，先试试
	enum bdev_nvme_multipath_policy		mp_policy;          // ljx 填充用，可能会有其它问题，先试试
	enum bdev_nvme_multipath_selector	mp_selector;        // ljx 填充用，可能会有其它问题，先试试
	uint32_t				rr_min_io;                      // ljx 填充用，可能会有其它问题，先试试
	uint32_t				rr_counter;                     // ljx 填充用，可能会有其它问题，先试试
	STAILQ_HEAD(, nvme_io_path)		io_path_list;           // ljx 填充用，可能会有其它问题，先试试
	TAILQ_HEAD(retry_io_head, spdk_bdev_io)	retry_io_list;  // ljx 填充用，可能会有其它问题，先试试
	struct spdk_poller			*retry_io_poller;           // ljx 填充用，可能会有其它问题，先试试
    // 以上是struct nvme_bdev_channel中的内容，由于fail时，ump_channel与 nvme_bdev_channel 重合，为避免ump_bdev_channel的成员被无辜修改，故先填充 nvme_bdev_channel的成员

    TAILQ_HEAD(, ump_bdev_iopath)
    iopath_list;
    TAILQ_ENTRY(ump_bdev_channel) tailq;                // ljx
    uint64_t max_id;                                    // iopath_list 中最大的iopath->id(未使用)
};


struct time_queue
{
    TAILQ_HEAD(, time_queue_ele) time_list;
    unsigned int len;
    uint64_t io_time_all;
    uint64_t io_time_avg;
};
struct time_queue_ele
{
    TAILQ_ENTRY(time_queue_ele) tailq;
    uint64_t io_time;
};

/* ump_bdev逻辑路径结构 */
struct ump_bdev_iopath
{
    TAILQ_ENTRY(ump_bdev_iopath) tailq;
    struct spdk_io_channel *io_channel;
    struct spdk_bdev *bdev;
    bool available;                          // 是否可用
    // uint64_t io_time_read;                // io 时间
    // uint64_t io_time_write;               // io 时间
    bool reconnecting;						// 是否正在重连
	struct spdk_poller *reconnect_poller;
	struct time_queue io_read_time;
	struct time_queue io_write_time;
    uint64_t id;
    uint64_t io_incomplete;                 // 未完成的io请求数
};


/* 参数上下文，用于保留必要变量并传递给回调函数 */
struct ump_bdev_io_completion_ctx
{
    struct spdk_io_channel *ch;
    void *real_caller_ctx;
    spdk_bdev_io_completion_cb real_completion_cb;
    struct ump_bdev_iopath *iopath; // sd
};


// ljx sump_ctrl.c
struct ump_failback_ctx
{
    struct ump_bdev_iopath *iopath;     
    struct spdk_bdev_io *bdev_io;       // 用于发出io请求
    // struct spdk_thread *thread;
    // struct ump_bdev_channel *ump_channel;
    // struct ump_bdev_iopath *tqh_first;
    // struct spdk_poller *poller;
    // struct ump_failback_ctx **addr;                         // 保存指向当前结构体地址的指针
    // struct spdk_spinlock lock;          // 用于同步，避免double free等
};
struct ump_channel_list { struct ump_bdev_channel *tqh_first; struct ump_bdev_channel * *tqh_last;
};
/* 全局变量，组织所有ump_bdev设备 */
extern struct ump_bdev_manage ump_bdev_manage;
/* 全局变量，保存真正处理设备注册的函数指针 */
extern int (*real_spdk_bdev_register)(struct spdk_bdev *bdev);
/* 全局变量，保存所有的ump_channel */
TAILQ_HEAD(, ump_bdev_channel) g_ump_bdev_channels;

/* sump_data.c */
void __attribute__((constructor)) ump_init(void);
void ump_bdev_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io);
struct spdk_io_channel *ump_bdev_get_io_channel(void *ctx);

/* sump_ctrl.c */
void ump_bdev_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);
struct ump_bdev_iopath *ump_bdev_find_iopath(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io);
// 路径选择算法
struct ump_bdev_iopath *ump_find_iopath_round_robin(struct ump_bdev_channel *ump_channel);
struct ump_bdev_iopath *ump_find_iopath_service_time(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io);
struct ump_bdev_iopath *ump_find_iopath_queue_length(struct ump_bdev_channel *ump_channel);
struct ump_bdev_iopath *ump_find_iopath_random(struct ump_bdev_channel *ump_channel);
struct ump_bdev_iopath *ump_find_iopath_random_weight_static(struct ump_bdev_channel *ump_channel);
struct ump_bdev_iopath *ump_find_iopath_hash(struct ump_bdev_channel *ump_channel, struct spdk_bdev_io *bdev_io);
void update_io_time(struct ump_bdev_iopath *iopath, struct spdk_bdev_io *bdev_io);
void update_io_time_detail(struct time_queue_ele *time_ele, struct time_queue *q);
int ump_io_count_fn(); // 测试各路径的io次数并重置时延

void ump_bdev_channel_clear_all_iopath(struct ump_bdev_channel *ump_channel);
int ump_bdev_channel_create_cb(void *io_device, void *ctx_buf);
void ump_bdev_channel_destroy_cb(void *io_device, void *ctx_buf);

int ump_bdev_destruct(void *ctx);
bool ump_bdev_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type);
int ump_bdev_dump_info_json(void *ctx, struct spdk_json_write_ctx *w);
void ump_bdev_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w);
uint64_t ump_bdev_get_spin_time(struct spdk_io_channel *ch);

void ump_failback(struct ump_bdev_iopath *iopath, struct spdk_bdev_io *bdev_io);
void ump_failback_io_completion_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);
int ump_failback_io_fn(void *arg1);

/* sump_util.c */
struct ump_bdev *get_ump_bdev_by_uuid(struct spdk_uuid *uuid);
void sump_printf(const char *fmt, ...);
void ump_time_queue_init(struct time_queue *t_queue);
unsigned int BKDR_Hash(char *s);

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
