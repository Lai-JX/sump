#include "sump.h"

/**************************************************************************/
/*******************************tools begin********************************/
/**************************************************************************/

/********************************************************
* Function name:    sump_printf
* Description:      用于调试时输出打印信息
* Parameter:
*   @fmt            要打印的信息 
* Return:           无        
**********************************************************/
void sump_printf(const char *fmt, ...)
{
#ifdef DEBUG_OUT
    printf("[SUMP DEBUG] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#endif
}

/********************************************************
* Function name:    get_ump_bdev_by_uuid
* Description:      根据传入的uuid，遍历全局变量尾队列ump_bdev_manage，查找相应的ump_bdev设备
* Parameter:
*   @uuid           spdk_uuid结构体指针uuid
* Return:           查找成功则返回相应的ump_bdev结构体指针，否则返回NULL      
**********************************************************/
struct ump_bdev *get_ump_bdev_by_uuid(struct spdk_uuid *uuid)
{
    struct ump_bdev *mbdev = NULL;
    TAILQ_FOREACH(mbdev, &ump_bdev_manage.ump_bdev_list, tailq)
    {
        if (!spdk_uuid_compare(&mbdev->bdev.uuid, uuid))
        {
            return mbdev;
        }
    }
    return NULL;
}

void ump_time_queue_init(struct time_queue *t_queue)
{
    TAILQ_INIT(&t_queue->time_list);
    t_queue->len = 0;
    t_queue->io_time_all = 0;
    t_queue->io_time_avg = 0;
}


// BKDR_Hash function 
unsigned int BKDR_Hash(char *s)
{
    unsigned int seed = 1313; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;
 
    while (*s)
    {
        hash = hash * seed + (*s++);
    }
 
    return (hash & 0x7FFFFFFF);
}

/**************************************************************************/
/*******************************tools end********************************/
/**************************************************************************/
