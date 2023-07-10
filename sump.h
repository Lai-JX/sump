#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <spdk/bdev.h>
#include "spdk/thread.h"
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
    TAILQ_HEAD(, ump_bdev_iopath)
    iopath_list;
};

/* ump_bdev逻辑路径结构 */
struct ump_bdev_iopath
{
    struct spdk_io_channel *io_channel;
    struct spdk_bdev *bdev;
    bool available;
    TAILQ_ENTRY(ump_bdev_iopath) tailq;
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
/* 全局变量，组织所有ump_bdev设备 */
extern struct ump_bdev_manage ump_bdev_manage;
/* 全局变量，保存真正处理设备注册的函数指针 */
extern int (*real_spdk_bdev_register)(struct spdk_bdev *bdev);

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
