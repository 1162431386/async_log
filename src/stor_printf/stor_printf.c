
/**@file stor_printf.c 
 * @note HangZhou Hikvision System Technology Co., Ltd. All Right Reserved.
 * @brief  通过预分配文件结合存储组件实现记录打印信息的功能实现
 * 
 * @author   xujian
 * @date     2019-11-27
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h> 
#include <dirent.h>
#ifdef __APPLE__
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/prctl.h>
#include <sys/vfs.h>
#include <mntent.h>
#endif
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <regex.h>
#include "common_utils.h"
#include "lstLib.h"
#include "stor_printf.h"

#define STOR_PRINTF_INFO_print
#ifdef STOR_PRINTF_INFO_print
#define INFO(fmt, args...) printf("I[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define INFO(fmt, args...)
#endif

#define STOR_PRINTF_WARN_print
#ifdef STOR_PRINTF_WARN_print
#define WARN(fmt, args...) printf("\033[33mW[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)
#else
#define WARN(fmt, args...)
#endif

#define STOR_PRINTF_ERR_print
#ifdef STOR_PRINTF_ERR_print
#define ERR(fmt, args...) printf("\033[31mE[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)
#else
#define ERR(fmt, args...)
#endif

static int sg_enable_storprintf_dbg = 0;

#define STOR_PRINTF_DBG_print
#ifdef STOR_PRINTF_DBG_print
#define DBG(fmt, args...) do { \
    if (sg_enable_storprintf_dbg) { \
        printf("\033[32mD[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args); \
    } \
} while (0)
#else
#define DBG(fmt, args...)
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define STOR_PRINTF_ERRNO_SET(err, e) do { \
    if (NULL != (err)) { \
        switch ((e) / 100) { \
            case 3: \
                if (STOR_PRINTF_TIMEOUT == (e)) { \
                    DBG("SET stor printf errno => %d(%s)\n", (int)(e), stor_printf_strerr((e))); \
                } \
                else { \
                    WARN("SET stor printf errno => %d(%s)\n", (int)(e), stor_printf_strerr((e))); \
                } \
                break; \
            case 4: \
                ERR("SET stor printf errno => %d(%s)\n", (int)(e), stor_printf_strerr((e))); \
                break; \
            default: \
                break; \
        } \
        *(err) = (e); \
    } \
} while (0)

static TYPE_STR_T sg_storprintf_errstr_tab[] = {
    TYPE_STR_ITEM(STOR_PRINTF_SUCC),
    TYPE_STR_ITEM(STOR_PRINTF_SEARCH_COMPLETE),
        
    TYPE_STR_ITEM(STOR_PRINTF_NO_RESOURCES),
    TYPE_STR_ITEM(STOR_PRINTF_RESOURCES_UNINITILISED),
    TYPE_STR_ITEM(STOR_PRINTF_IO_WAKEUP),
    TYPE_STR_ITEM(STOR_PRINTF_WRITE_PARTLY),
    TYPE_STR_ITEM(STOR_PRINTF_READ_PARTLY),
    TYPE_STR_ITEM(STOR_PRINTF_TIMEOUT),
    TYPE_STR_ITEM(STOR_PRINTF_NO_AVAILABLE_SP_FILE),
    TYPE_STR_ITEM(STOR_PRINTF_SP_FILE_FULL),
    TYPE_STR_ITEM(STOR_PRINTF_CREATE_DIR_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_SPACE_NOT_ENOUGH),
    TYPE_STR_ITEM(STOR_PRINTF_SP_FILELENGTH_MISMATCH),
    
    TYPE_STR_ITEM(STOR_PRINTF_INVALID_INPUT),
    TYPE_STR_ITEM(STOR_PRINTF_INVALID_ARGS),
    TYPE_STR_ITEM(STOR_PRINTF_PTHREAD_CREATE_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_INVALID_ID),
    TYPE_STR_ITEM(STOR_PRINTF_BAD_ID),
    TYPE_STR_ITEM(STOR_PRINTF_OUT_OF_MEMORY),
    TYPE_STR_ITEM(STOR_PRINTF_SOCKETPAIR_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_SELECT_FAIL), 
    TYPE_STR_ITEM(STOR_PRINTF_FILE_CREAT_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_FILE_LSEEK_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_WRITE_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_READ_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_CHECKSUM_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_INVALID_MAGIC),
    TYPE_STR_ITEM(STOR_PRINTF_SP_BOTH_HEAD_ERROR),
    TYPE_STR_ITEM(STOR_PRINTF_IO_ERROR),
    TYPE_STR_ITEM(STOR_PRINTF_FILE_OPEN_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_FILE_WRITE_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_BAD_SEARCH_HANDLE),
    TYPE_STR_ITEM(STOR_PRINTF_SEARCH_TAGS_CONVERT_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_PTHREAD_COND_WAIT_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_SP_FILE_ERROR),
    TYPE_STR_ITEM(STOR_PRINTF_SETMNTENT_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_NO_MATCH_ENTRY),
    TYPE_STR_ITEM(STOR_PRINTF_STATFS_FAIL),
    TYPE_STR_ITEM(STOR_PRINTF_FGETS_FAIL),
};

/* stor_printf接口中打印缓存尺寸 */
#define STOR_PRINTF_PRTBUFF_SIZE (4 * 1024)

/* sp文件结构
 *          |<------------sp_head_t.data_size----------->|
 *          |                                            |
 *  --------------------------------------------------------------
 * | s_head |                    data                    | e_head |
 *  --------------------------------------------------------------
 * |                                                |
 * |<--- sizeof (sp_head_t) + sp_head_t.data_offset ---->|
 *                                           file_lseek_offset
 */

/* sp文件magic */
#define SP_MAGIC 0x6480

/* sp文件头同步时间间隔, 单位: 秒 */
#define SP_FILE_HEAD_SYNC_INTER 30

/* sp文件头结构描述 */
struct sp_head_t {
    unsigned int magic; /* sp文件标志, 用于判断该文件是否为sp文件, 默认为SP_MAGIC */

    /* 状态 */
    struct {
        unsigned int error:1;
        /* 该sp文件起始时间被调整过: 导致sp文件中时间不均匀, 存在跳变的情况。
         * 检索时只能按序从前往后检索, 不能采用二分法查找 
         */
        unsigned int starttime_adjust:1;
        /* 该sp文件空间是否已分配好, 如果是预分配模式, 则该文件创建之后即置1, 如果为非预分配模式, 则置为0, 待该文件数据写入data_size大小后, 置为1
         */
        unsigned int allocated:1;
        unsigned int res:29;
    }status;

    /* 从尺寸上描述sp文件 */
    unsigned int data_size; /* sp文件数据区大小, 单位: 字节 */
    unsigned int data_offset; /* sp文件当前写入数据的偏移量, 范围[0, data_size - 1] */

    /* 从时间尺度上描述sp文件 */
    unsigned int start_time; /* sp文件数据的开始时间 */
    unsigned int end_time; /* sp文件数据的截止时间 */  

    /* sp文件头上一次写入文件的时间 */
    unsigned int head_writetime;

    /* sp文件头写入的总次数, 从0开始 */
    unsigned int head_total_writecount;

    char res[476]; /* 保留字节 */

    unsigned int checksum; /* sp文件头校验和 */
};

/* sp文件大小 */
#define SP_FILE_SIZE(data_size) (sizeof (struct sp_head_t) + (data_size) + sizeof (struct sp_head_t))

/* sp文件偏移量 */
#define SP_FILE_OFFSET(data_offset) (sizeof (struct sp_head_t) + (data_offset))

/* sp文件头起始偏移量 */
#define SP_FILE_START_HEAD_OFFSET 0

/* sp文件头结尾偏移量 */
#define SP_FILE_END_HEAD_OFFSET(data_size) (sizeof (struct sp_head_t) + (data_size))

/* 主动创建sp文件时, 分区预留的最小磁盘空间默认值, 32MB */
#define DISK_PART_MIN_KEEPSIZE_DEF (32 * 1024 * 1024)

/* sp文件结构描述 */
struct sp_t {
    NODE node;
    char path[128]; /* sp文件的路径 */
    int fd; /* sp文件的文件描述符 */

    /* sp文件头 */
    struct sp_head_t head;
};

struct stor_printf_t {
    pthread_mutex_t mutex;
    int inuse;

    pthread_cond_t cond;
	int refcount;
    
    struct stor_printf_param_t param;

    pthread_mutex_t chat_mutex; /* 交互互斥锁 */
    int chatfds[2]; /* 0用于主线程, 1用于外部接口 */
    int wakeupfds[2]; /* 0用于主线程, 1用于外部接口 */

    /* sp文件链表读写锁
     * 写sp文件以及检索sp文件时, 占用读锁
     * 往sp文件链表中添加或删除sp文件时, 占用写锁
     */
    pthread_rwlock_t splst_rwlock;
    LIST splst; /* 用于存放分区中每个sp文件的链表, 对应结构体struct sp_t */
    struct sp_t *sp_inuse; /* 当前正在写入的文件 */

    /* se搜索者链表读写锁
     * 写se搜索者遍历时, 占用读锁
     * 往se搜索者链表中添加或删除storprintf_se时, 占用写锁
     */
    pthread_rwlock_t selst_rwlock;
    LIST selst; /* 用于存放搜索资源的链表, 对应结构体struct stor_printf_search_t */

    pthread_t tid; 
    
    char *prtbuff; /* 用于打印的缓存, 大小为STOR_PRINTF_PRTBUFF_SIZE */  
};

#define STOR_PRINTF_MAX_NUM 8

struct stor_printfs_t {
    pthread_mutex_t mutex;
    int init;

    struct stor_printf_t storprintf[STOR_PRINTF_MAX_NUM];
};

static struct stor_printfs_t sg_storprintfs = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .init = 0,
};

/* 用于搜索的sp文件描述结构 */
struct sp_search_t {
    NODE node;
    char path[128]; /* sp文件的路径 */

    /* 该sp文件起始时间被调整过: 导致sp文件中时间不均匀, 存在跳变的情况。
     * 检索时只能按序从前往后检索, 不能采用二分法查找 
     */
    int starttime_adjust; 

     /* 该sp文件数据是否已分配完成
     */
    int allocated;

    unsigned int data_size; /* sp文件数据区大小, 单位: 字节 */
    unsigned int data_offset; /* sp文件当前写入数据的偏移量, 范围[0, data_size - 1] */
    
    unsigned int start_time; /* sp文件数据的开始时间 */
    unsigned int end_time; /* sp文件数据的截止时间 */
};

/* stor_printf搜索描述结构 */
struct stor_printf_search_t {
    NODE node;

    struct stor_printf_search_param_t param;
    /* 检索用标签对应的正则表达式 */
    char tags_regex[256];
    
    LIST spsearch_lst; /* 用于存放时间上符合检索范围的sp文件搜索描述结构 struct sp_search_t */

    FILE *fh; /* 文件操作句柄 */
};

extern const char * strtime(time_t t, char *pszTime, unsigned int uStrSize);
extern int pthreadIdVerify(pthread_t tid);
extern int is_regex_match(const char *str, const char *pattern, int cflags);
extern int str_seg_replace(const char *pszSrcStr, const char *pszSrcDelim, const char *pszDstDelim, char *pszDstBuf, int iDstBufLen);
extern unsigned int myself_mktime(struct tm *tm);

static inline int stor_printf_to_id(const struct stor_printf_t *storprintf)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sg_storprintfs.storprintf); i++) {
        if (&(sg_storprintfs.storprintf[i]) == (struct stor_printf_t *)storprintf) {
            return i;
        }
    }

    return -1;

}

static inline enum STOR_PRINTF_ERRNO id_to_stor_printf(int id, struct stor_printf_t **pp_storprintf)
{
    if (id < 0 || id > (ARRAY_SIZE(sg_storprintfs.storprintf) - 1)) {
        return STOR_PRINTF_INVALID_ID;
    }

    if (!sg_storprintfs.init) {
        return STOR_PRINTF_RESOURCES_UNINITILISED;
    }

    *pp_storprintf = &(sg_storprintfs.storprintf[id]);

    return STOR_PRINTF_SUCC;

}

static enum STOR_PRINTF_ERRNO stor_printf_waitms(struct stor_printf_t *storprintf, int waitms)
{
    fd_set rset;
    int ret = -1;
    struct timeval timeout;
    struct timeval *p_timeout;

    FD_ZERO(&rset);
    FD_SET(storprintf->wakeupfds[0], &rset);

    if (waitms < 0) {
        p_timeout = NULL;
    }
    else {
        p_timeout = &timeout;
        p_timeout->tv_sec = waitms / 1000;
        p_timeout->tv_usec = (waitms % 1000) * 1000;
    }

    do {
        ret = select(storprintf->wakeupfds[0] + 1, &rset, NULL, NULL, p_timeout);
    } while (-1 == ret && EINTR == errno);
    if (-1 == ret) {
        ERR("wakeupfds[0] FAIL to select for read, %s\n", strerror(errno));
        return STOR_PRINTF_SELECT_FAIL;
    }

    if (0 == ret) {
        return STOR_PRINTF_SUCC;
    }

    return STOR_PRINTF_IO_WAKEUP;

}

static unsigned int stor_printf_checksum(const char *buff, unsigned int len)
{
    int i;
    unsigned int checksum = 0;

    for (i = 0; i < len; i++) {
        checksum += (unsigned int)buff[i];
    }

    return checksum;

}

static char * stor_printf_fd_2_path(int fd, char *path, unsigned int size)
{
    char proc_path[128] = {0};

    if (-1 == fd || NULL == path || 0 == size) {
        return NULL;
    }

    snprintf(proc_path, sizeof (proc_path), "/proc/self/fd/%d", fd);
    if (-1 == readlink(proc_path, path, size)) {
        return NULL;
    }
	path[size - 1] = '\0';

    return path;

}

static const char * stor_printf_strsize(unsigned long long size, char *sizestr, unsigned int sizestr_size)
{
    if (NULL == sizestr || 0 == sizestr_size) {
        return "strsize_INVALID_INPUT";
    }

    if (size < 1024) {
        snprintf(sizestr, sizestr_size, "%lluB", size);
    }
    else if (size >= 1024 && size < (1024 * 1024)) {
        snprintf(sizestr, sizestr_size, "%0.2fKB", ((float)size) / 1024.0f);
    }
    else if (size >= (1024 * 1024) && size < (1024 * 1024 * 1024)) {
        snprintf(sizestr, sizestr_size, "%0.2fMB", ((float)size) / 1024.0f / 1024.0f);
    }
    else {
        snprintf(sizestr, sizestr_size, "%0.2fGB", ((float)size) / 1024.0f / 1024.0f / 1024.0f);
    }

    return sizestr;

}

#define SPFILE_SNPRINTF(prtbuff, size, format, args...) do { \
    if (strlen(prtbuff) >= size) { \
        WARN("prtbuff too small, strlen(prtbuff) = %u, size = %u\n", (unsigned int)strlen(prtbuff), size); \
        return; \
    } \
    snprintf(prtbuff + strlen(prtbuff), size - strlen(prtbuff), format, ##args); \
} while (0)

static void sp_file_head_print(const struct sp_head_t *head, const char *tag, char *prtbuff, unsigned int size)
{
    char str_datasize[32] = {0};
    char str_dataoffset[32] = {0};
	char str_starttime[32] = {0};
	char str_endtime[32] = {0};
	char str_head_writetime[32] = {0};

	if (NULL == head) {
		return;
    }

    if (NULL == prtbuff || 0 == size) {
        printf("+++ [%s]magic: 0x%x\n", (NULL == tag) ? "" : tag, head->magic);
    	printf("+++ [%s]status.error: %d\n", (NULL == tag) ? "" : tag, head->status.error);
        printf("+++ [%s]status.starttime_adjust: %d\n", (NULL == tag) ? "" : tag, head->status.starttime_adjust);
    	printf("+++ [%s]data_size: %s\n", (NULL == tag) ? "" : tag, stor_printf_strsize(head->data_size, str_datasize, sizeof (str_datasize)));
    	printf("+++ [%s]data_offset: %s\n", (NULL == tag) ? "" : tag, stor_printf_strsize(head->data_offset, str_dataoffset, sizeof (str_dataoffset)));
    	printf("+++ [%s]start_time: %s\n", (NULL == tag) ? "" : tag, strtime((time_t)head->start_time, str_starttime, sizeof (str_starttime)));
    	printf("+++ [%s]end_time: %s\n", (NULL == tag) ? "" : tag, strtime((time_t)head->end_time, str_endtime, sizeof (str_endtime)));
    	printf("+++ [%s]head_writetime: %s\n", (NULL == tag) ? "" : tag, strtime(head->head_writetime, str_head_writetime, sizeof (str_head_writetime)));	
    	printf("+++ [%s]head_total_writecount: %u\n", (NULL == tag) ? "" : tag, head->head_total_writecount);	
    	printf("+++ [%s]checksum: 0x%x\n", (NULL == tag) ? "" : tag, (head->checksum & 0xffffffff));
    }
	else {
        SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]magic: 0x%x\n", (NULL == tag) ? "" : tag, head->magic);
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]status.error: %d\n", (NULL == tag) ? "" : tag, head->status.error);
        SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]status.starttime_adjust: %d\n", (NULL == tag) ? "" : tag, head->status.starttime_adjust);
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]data_size: %s\n", (NULL == tag) ? "" : tag, stor_printf_strsize(head->data_size, str_datasize, sizeof (str_datasize)));
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]data_offset: %s\n", (NULL == tag) ? "" : tag, stor_printf_strsize(head->data_offset, str_dataoffset, sizeof (str_dataoffset)));
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]start_time: %s\n", (NULL == tag) ? "" : tag, strtime((time_t)head->start_time, str_starttime, sizeof (str_starttime)));
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]end_time: %s\n", (NULL == tag) ? "" : tag, strtime((time_t)head->end_time, str_endtime, sizeof (str_endtime)));
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]head_writetime: %s\n", (NULL == tag) ? "" : tag, strtime(head->head_writetime, str_head_writetime, sizeof (str_head_writetime)));	
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]head_total_writecount: %u\n", (NULL == tag) ? "" : tag, head->head_total_writecount);	
    	SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]checksum: 0x%x\n", (NULL == tag) ? "" : tag, head->checksum);
    }

	return;
	
}

static inline int sp_file_is_full(struct sp_t *sp)
{
    if ((sp->head.data_offset >= (sp->head.data_size - STOR_PRINTF_PRTBUFF_SIZE)) 
        && (sp->head.data_offset < sp->head.data_size)) {
        return 1;
    }

    return 0;

}

static inline enum STOR_PRINTF_ERRNO sp_file_head_write(int fd, unsigned int offset, const struct sp_head_t *head)
{
    int ret = -1;
    int err_no = 0;

    do {
        if (-1 == lseek(fd, offset, SEEK_SET)) {
            ERR("FAIL to lseek, fd = %d, offset = %u, %s\n", fd, offset, strerror(errno));
            return STOR_PRINTF_FILE_LSEEK_FAIL;
        }
        ret = write(fd, (char *)head, sizeof (*head));
    } while (-1 == ret && EINTR == errno);
    if (-1 == ret) {
        err_no = errno;
        ERR("FAIL to write, fd = %d, data: %p, len = %d, %s\n", fd, head, (int)sizeof (*head), strerror(err_no));
        if (ENOSPC == err_no) {
            return STOR_PRINTF_SPACE_NOT_ENOUGH;
        }
        return STOR_PRINTF_WRITE_FAIL;
    }
    else if (sizeof (*head) != ret) {
        WARN("PARTLY write, fd = %d, data: %p, len = %d, ret = %d, %s\n", fd, head, (int)sizeof (*head), ret, strerror(errno));
        return STOR_PRINTF_WRITE_PARTLY;
    }

    return STOR_PRINTF_SUCC;

}

static inline enum STOR_PRINTF_ERRNO sp_file_head_read(int fd, unsigned int offset, struct sp_head_t *head)
{
    int ret = -1;

    do {
        if (-1 == lseek(fd, offset, SEEK_SET)) {
            ERR("FAIL to lseek, fd = %d, offset = %u, %s\n", fd, offset, strerror(errno));
            return STOR_PRINTF_FILE_LSEEK_FAIL;
        }
        ret = read(fd, (char *)head, sizeof (*head));
    } while (-1 == ret && EINTR == errno);
    if (-1 == ret) {
        ERR("FAIL to read, fd = %d, data: %p, len = %d, %s\n", fd, head, (int)sizeof (*head), strerror(errno));
        return STOR_PRINTF_READ_FAIL;
    }
    else if (sizeof (*head) != ret) {
        WARN("PARTLY read, fd = %d, data: %p, len = %d, ret = %d, %s\n", fd, head, (int)sizeof (*head), ret, strerror(errno));
        return STOR_PRINTF_READ_PARTLY;
    }

    return STOR_PRINTF_SUCC;
    
}

static enum STOR_PRINTF_ERRNO sp_file_heads_write(struct sp_t *sp)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (NULL == sp) {
        ERR("INVALID input param, sp = %p\n", sp);
        return STOR_PRINTF_INVALID_INPUT;
    }

    if (0 == sp->head.status.allocated) {
        sp->head.status.allocated = sp_file_is_full(sp) ? 1 : 0;
    }
    sp->head.head_writetime = time(NULL);
    sp->head.head_total_writecount++;
    sp->head.checksum = stor_printf_checksum((char *)&sp->head, sizeof (sp->head) - sizeof (sp->head.checksum));

    err = sp_file_head_write(sp->fd, SP_FILE_START_HEAD_OFFSET, &sp->head);
    if (STOR_PRINTF_SUCC != err) {
        ERR("FAIL to write start head, sp file: %s,%s\n", sp->path, stor_printf_strerr(err));
        return err;
    }
    
    if (1 == sp->head.status.allocated) {
        err = sp_file_head_write(sp->fd, SP_FILE_END_HEAD_OFFSET(sp->head.data_size), &sp->head);
        if (STOR_PRINTF_SUCC != err) {
            ERR("FAIL to write end head, sp file: %s,%s\n", sp->path, stor_printf_strerr(err));
            return err;
        }
    }
    
    return STOR_PRINTF_SUCC;

}

static void sp_file_printf(const struct sp_t *sp, const char *tag, char *prtbuff, unsigned int size)
{
	if (NULL == sp) {
		return;
    }

    if (NULL == prtbuff || 0 == size) {
        printf("\n");
    	printf("+++ [%s]path: %s\n", (NULL == tag) ? "" : tag, sp->path);
    	printf("+++ [%s]fd: %d\n", (NULL == tag) ? "" : tag, sp->fd);
    }
    else {
        SPFILE_SNPRINTF(prtbuff, size, "\n");        
        SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]path: %s\n", (NULL == tag) ? "" : tag, sp->path);
        SPFILE_SNPRINTF(prtbuff, size, "+++ [%s]fd: %d\n", (NULL == tag) ? "" : tag, sp->fd);
    }
	
	sp_file_head_print(&sp->head, tag, prtbuff, size);

	return;
	
}

static enum STOR_PRINTF_ERRNO sp_file_close(struct sp_t *sp, int sync_head)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    char tmpstr[128] = {0};

    if (NULL == sp) {
        ERR("INVALID input param, sp = %p\n", sp);
        return STOR_PRINTF_INVALID_INPUT;
    }

    if (sync_head && -1 != sp->fd) {
        DBG(" CLOSE \"%s\" to write sp file(%s) heads, total_wr_count: %u\n", strtime(time(NULL), tmpstr, sizeof (tmpstr)), sp->path, sp->head.head_total_writecount);
        err = sp_file_heads_write(sp);
		if (STOR_PRINTF_SUCC != err) {
			WARN("FAIL to write sp file(%s) heads, %s\n", sp->path, stor_printf_strerr(err));
	    }
    }

    SAFE_CLOSE(sp->fd);

    free(sp);
    sp = NULL;

    return STOR_PRINTF_SUCC;

}

static inline enum STOR_PRINTF_ERRNO sp_file_head_check(const struct sp_head_t *head, const struct stor_printf_param_t *param)
{
    unsigned int calc_checksum = 0;
    char size_str1[32] = {0};
    char size_str2[32] = {0};

    /* 文件头magic字段检查 */
    if (SP_MAGIC != head->magic) {
        ERR("INVALID sp file head magic: 0x%x, right: 0x%x\n", head->magic, SP_MAGIC);
        return STOR_PRINTF_INVALID_MAGIC;
    }

    /* 文件头检验和检查 */
    calc_checksum = stor_printf_checksum((char *)head, sizeof (*head) - sizeof (head->checksum));
    if (head->checksum != calc_checksum) {
        ERR("MISMATCH sp file head checksum: %u, calc: %u\n", head->checksum, calc_checksum);
        return STOR_PRINTF_CHECKSUM_FAIL;
    }

    /* sp文件头状态错误 */
    if (1 == head->status.error) {
        ERR("ERROR sp file, head->status.error == 1\n");
        return STOR_PRINTF_SP_FILE_ERROR;
    }

    /* 检查sp文件长度是否一致 */
    if (param->sp_datasize != head->data_size) {
        ERR("MISMATCH sp file length, param->sp_datasize: %s, head->data_size: %s\n", 
            stor_printf_strsize(param->sp_datasize, size_str1, sizeof (size_str1)),
            stor_printf_strsize(head->data_size, size_str2, sizeof (size_str2)));
        return STOR_PRINTF_SP_FILELENGTH_MISMATCH;
    }

    return STOR_PRINTF_SUCC;
    
}

static enum STOR_PRINTF_ERRNO sp_file_head_read_and_check(int fd, unsigned int offset, struct sp_head_t *head, const struct stor_printf_param_t *param)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    struct sp_head_t tmp_head;

    if (-1 == fd || NULL == param) {
        ERR("INVALID input param, fd = %d, param = %p\n", fd, param);
        return STOR_PRINTF_INVALID_INPUT;
    }

    memset((char *)&tmp_head, 0, sizeof (tmp_head));

    err = sp_file_head_read(fd, offset, &tmp_head);
    if (STOR_PRINTF_SUCC != err) {
        WARN("FAIL to read sp file head, fd = %d, %s\n", fd, stor_printf_strerr(err));
        return err;
    }

    err = sp_file_head_check(&tmp_head, param);
    if (STOR_PRINTF_SUCC != err) {
        WARN("FAIL to check sp file head, %s\n", stor_printf_strerr(err));
        return err;
    }

    if (NULL != head) {
        memcpy((char *)head, (char *)&tmp_head, sizeof (struct sp_head_t));
    }

    return STOR_PRINTF_SUCC;

}

static inline enum STOR_PRINTF_ERRNO sp_file_create(const struct stor_printf_param_t *param, const char *path, int *p_fd, struct sp_head_t *p_head)
{
    int fd = -1;
    struct sp_head_t head;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    char sizestr[32] = {0};

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (-1 == fd) {
        ERR("FAIL to create \"%s\", %s\n", path, strerror(errno));
        return STOR_PRINTF_FILE_CREAT_FAIL;
    }

    memset((char *)&head, 0, sizeof (head));
    head.magic = SP_MAGIC;
    head.status.allocated = param->sp_prealloc ? 1 : 0;
    head.data_size = param->sp_datasize;
    head.data_offset = 0;
    head.start_time = 0;
    head.end_time = 0;
    head.head_writetime = 0/* 空文件文件头写入时间初始化成0, 使第一次写入就立即刷新一次文件头 */;
    head.head_total_writecount = 0;
    head.checksum = stor_printf_checksum((char *)&head, sizeof (head) - sizeof (head.checksum));

    err = sp_file_head_write(fd, SP_FILE_START_HEAD_OFFSET, &head);
    if (STOR_PRINTF_SUCC != err) {
        ERR("FAIL to write start head, fd = %d, %s\n", fd, stor_printf_strerr(err));
        goto err_exit;
    }

    if (param->sp_prealloc) {
        err = sp_file_head_write(fd, SP_FILE_END_HEAD_OFFSET(param->sp_datasize), &head);
        if (STOR_PRINTF_SUCC != err) {
            ERR("FAIL to write end head, fd = %d, %s\n", fd, stor_printf_strerr(err));
            goto err_exit;
        }
    }

    INFO("SUCC to create sp file, path: %s, sp_datasize = %s\n", path, stor_printf_strsize(param->sp_datasize, sizestr, sizeof (sizestr)));

    if (NULL != p_fd) {
        *p_fd = fd;
    }

    if (NULL != p_head) {
        memcpy((char *)p_head, (char *)&head, sizeof (struct sp_head_t));
    }

    return STOR_PRINTF_SUCC;

err_exit:

    SAFE_CLOSE(fd);

    if (STOR_PRINTF_SPACE_NOT_ENOUGH == err) {
        /* 该文件因为写数据时发现空间不足, 此处直接删除 */
        if (-1 == remove(path)) {
            WARN("FAIL to remove \"%s\", %s\n", path, strerror(errno));
        }
    }
    
    return err;

}

typedef enum STOR_PRINTF_ERRNO (*SP_FILE_HEADS_CMP_SYNC_T)(struct sp_t *, const struct stor_printf_param_t *, struct sp_head_t *, struct sp_head_t *);

static enum STOR_PRINTF_ERRNO heads_cmp_sync_start_error_end_error(struct sp_t *sp, const struct stor_printf_param_t *param, struct sp_head_t *start_head, struct sp_head_t *end_head)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    INFO("sp file: \"%s\", BOTH HEAD ERROR, remove it and create it\n", sp->path);
    
    if (-1 == remove(sp->path)) {
        WARN("FAIL to remove \"%s\", %s\n", sp->path, strerror(errno));
        return STOR_PRINTF_SP_BOTH_HEAD_ERROR;
    }

    err = sp_file_create(param, sp->path, &sp->fd, &sp->head);
    if (STOR_PRINTF_SUCC != err) {
        WARN("FAIL to create sp file: \"%s\", %s\n", sp->path, stor_printf_strerr(err));
        return STOR_PRINTF_SP_BOTH_HEAD_ERROR;
    }

    return STOR_PRINTF_SUCC;

}

static enum STOR_PRINTF_ERRNO heads_cmp_sync_start_error_and_end_ok(struct sp_t *sp, const struct stor_printf_param_t *param, struct sp_head_t *start_head, struct sp_head_t *end_head)
{
    memcpy((char *)&sp->head, (char *)end_head, sizeof (struct sp_head_t));

    return sp_file_head_write(sp->fd, SP_FILE_START_HEAD_OFFSET, &sp->head);

}

static enum STOR_PRINTF_ERRNO heads_cmp_sync_start_ok_and_end_error(struct sp_t *sp, const struct stor_printf_param_t *param, struct sp_head_t *start_head, struct sp_head_t *end_head)
{
    memcpy((char *)&sp->head, (char *)start_head, sizeof (struct sp_head_t));

    if (0 == sp->head.status.allocated) {
        /* 非预分配文件, 且该sp文件还未写完整, 无需同步到end头 */
        return STOR_PRINTF_SUCC;
    }

    return sp_file_head_write(sp->fd, SP_FILE_END_HEAD_OFFSET(param->sp_datasize), &sp->head);

}

static enum STOR_PRINTF_ERRNO heads_cmp_sync_start_ok_and_end_ok(struct sp_t *sp, const struct stor_printf_param_t *param, struct sp_head_t *start_head, struct sp_head_t *end_head)
{
    /* 两个头都校验成功, 对比两个都是否一致, 如果不一致, 以起始头为准, 同步到结尾的头 */
    if (0 == memcmp((char *)start_head, (char *)end_head, sizeof (struct sp_head_t))) {
        memcpy((char *)&sp->head, (char *)start_head, sizeof (struct sp_head_t));
        return STOR_PRINTF_SUCC;
    }
    
    return heads_cmp_sync_start_ok_and_end_error(sp, param, start_head, end_head);

}

static SP_FILE_HEADS_CMP_SYNC_T sg_sp_file_heads_cmp_sync[2][2] = {
    {heads_cmp_sync_start_error_end_error, heads_cmp_sync_start_error_and_end_ok},
    {heads_cmp_sync_start_ok_and_end_error, heads_cmp_sync_start_ok_and_end_ok},
};

static inline int sp_file_is_empty(struct sp_t *sp)
{
    if (0 == sp->head.data_offset) {
        return 1;
    }

    return 0;
    
}

static inline int sp_file_is_inuse(struct sp_t *sp)
{
    /* 判断sp文件中正在写入数据的文件 */
    if ((sp->head.data_offset > 0) 
        && (sp->head.data_offset < (sp->head.data_size - STOR_PRINTF_PRTBUFF_SIZE))) {
        return 1;
    }

    return 0;

}

static inline int stor_printf_strStartsWith(const char *line, const char *prefix)
{
    const char *tmpline = line;

    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    if ('\0' != *prefix) {
        return 0;
    }

    return (line - tmpline);
    
}

/* 通过一个文件路径获取其所在磁盘分区的大小 */

struct mntmatch_entry_t {
    NODE node;
    char mnt_dir[128];
    int nr_match;
};

#ifndef __APPLE__
static enum STOR_PRINTF_ERRNO stor_printf_part_leftspace_get(const char *path, unsigned long long *size, char *mntpath, unsigned int mntpath_size)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    char cwd_path[128] = {0};
    char abs_path[256] = {0};
    #define MOUNTS_PATH "/proc/mounts"
    FILE *fh = NULL;
    struct mntent *m = NULL;
    LIST mnt_matchlst;
    struct mntmatch_entry_t *p = NULL;
    int nr_match = 0;
    struct mntmatch_entry_t *p_max = NULL;
    struct statfs sfs_buf;

    if (NULL == path || '\0' == path[0] || NULL == size) {
        ERR("INVALID input param, path = %p, size = %p\n", path, size);
        return STOR_PRINTF_INVALID_INPUT;
    }

    /* 获取path对应的绝对路径 */
    if ('/' != path[0]) {
        /* 如果当前目录是相对路径, 就先组装出绝对路径 */
        snprintf(abs_path, sizeof (abs_path), "%s/%s", getcwd(cwd_path, sizeof (cwd_path)), path);
    }
    else {
        SAFE_STRNCPY(abs_path, path, sizeof (abs_path));
    }

    fh = setmntent(MOUNTS_PATH, "r");
    if (NULL == fh) {
        ERR("FAIL to setmntent, filename: %s, %s\n", MOUNTS_PATH, strerror(errno));
        return STOR_PRINTF_SETMNTENT_FAIL;
    }

    lstInit(&mnt_matchlst);
    do {
        m = getmntent(fh);
        if (NULL == m) {
            break;
        }
        nr_match = stor_printf_strStartsWith(abs_path, m->mnt_dir);
        if (nr_match) {
            p = (struct mntmatch_entry_t *)malloc(sizeof (*p));
            if (NULL == p) {
                ERR("OUT OF MEMORY, sizeof (struct mntmatch_entry_t) = %u\n", (unsigned int)sizeof (struct mntmatch_entry_t));
                err = STOR_PRINTF_OUT_OF_MEMORY;
                goto exit;
            }
            memset((char *)p, 0, sizeof (*p));
            SAFE_STRNCPY(p->mnt_dir, m->mnt_dir, sizeof (p->mnt_dir));
            p->nr_match = nr_match;
            lstAdd(&mnt_matchlst, &(p->node));
        }
    } while (1);
    if (lstCount(&mnt_matchlst) <= 0) {
        err = STOR_PRINTF_NO_MATCH_ENTRY;
        goto exit;
    }

#if 0
    LIST_FOR_EACH(struct mntmatch_entry_t, p, &mnt_matchlst) {
        INFO("\n+++ path: %s\n", path);
        INFO("+++ abs_path: %s\n", abs_path);
        INFO("+++ mnt_dir: %s\n", p->mnt_dir);
        INFO("+++ nr_match: %d\n", p->nr_match);
    }
#endif

    LIST_FOR_EACH(struct mntmatch_entry_t, p, &mnt_matchlst) {
        if (NULL == p_max) {
            p_max = p;
        }
        else {
            p_max = (p_max->nr_match < p->nr_match) ? p : p_max;
        }
    }

    memset((char *)&sfs_buf, 0, sizeof (sfs_buf));
    if (-1 == statfs(p_max->mnt_dir, &sfs_buf)) {
        ERR("FAIL to statfs \"%s\", %s\n", p_max->mnt_dir, strerror(errno));
        err = STOR_PRINTF_STATFS_FAIL;
        goto exit;
    }

    *size = sfs_buf.f_bsize * sfs_buf.f_bfree;
    if (NULL != mntpath && mntpath_size > 0) {
        SAFE_STRNCPY(mntpath, p_max->mnt_dir, mntpath_size);
    }
    
    err = STOR_PRINTF_SUCC;

exit:

    lstFree(&mnt_matchlst);

    if (NULL != fh) {
        endmntent(fh);
        fh = NULL;
    }
    
    return err;

}
#endif

static enum STOR_PRINTF_ERRNO sp_file_open(const struct stor_printf_param_t *param, const char *path, struct sp_t **pp_sp)
{
    struct sp_t *sp;
    unsigned long long leftsize = 0ull;
    unsigned long long min_keepsize = DISK_PART_MIN_KEEPSIZE_DEF;
    char partdev_path[128] = {0};
    char str1[32] = {0};
    char str2[32] = {0};
    char str3[32] = {0};
    char str4[32] = {0};
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    struct sp_head_t start_head;
    struct sp_head_t end_head;
    int start_head_ok = 0;
    int end_head_ok = 0;

    if (NULL == param || NULL == path || '\0' == path[0] || NULL == pp_sp) {
        ERR("INVALID input param, param = %p, path = %p, pp_sp = %p\n", param, path, pp_sp);
        return STOR_PRINTF_INVALID_INPUT;
    }

    sp = (struct sp_t *)malloc(sizeof (*sp));
    if (NULL == sp) {
        ERR("OUT OF MEMORY, sizeof (struct sp_t) = %d\n", (int)sizeof (struct sp_t));
        return STOR_PRINTF_OUT_OF_MEMORY;
    }
    memset((char *)sp, 0, sizeof (*sp));
    sp->fd = -1;

    SAFE_STRNCPY(sp->path, path, sizeof (sp->path));

    sp->fd = open(sp->path, O_RDWR, 0666);
    if (-1 == sp->fd) {
        if (ENOENT != errno) {
            goto err_exit;
        }
        INFO("\"%s\" not exist, create it...\n", sp->path);

#ifndef __APPLE__
        /* 如果用户指定了分区路径, 则获取一下分区空间, 并判断当前这个sp文件是否可以创建 */
        if (-1 != param->diskpart_min_keepsize) {
            min_keepsize = (0 == param->diskpart_min_keepsize) ? DISK_PART_MIN_KEEPSIZE_DEF : (unsigned long long)param->diskpart_min_keepsize;
            if (STOR_PRINTF_SUCC == stor_printf_part_leftspace_get(sp->path, &leftsize, partdev_path, sizeof (partdev_path))) {
                if (leftsize < ((unsigned long long)SP_FILE_SIZE(param->sp_datasize) + min_keepsize)) {
                    WARN("DISK_PART: \"%s\" leftsize(%s) has no enough space for sp file(\"%s\":%s) + min_keepsize(%s) = (%s)\n", 
                        partdev_path, 
                        stor_printf_strsize(leftsize, str1, sizeof (str1)), 
                        sp->path, 
                        stor_printf_strsize(SP_FILE_SIZE(param->sp_datasize), str2, sizeof (str2)), 
                        stor_printf_strsize(min_keepsize, str3, sizeof (str3)), 
                        stor_printf_strsize((SP_FILE_SIZE(param->sp_datasize) + min_keepsize), str4, sizeof (str4)));
                    err = STOR_PRINTF_SPACE_NOT_ENOUGH;
                    goto err_exit;
                }
            }
        }
 #endif
        err = sp_file_create(param, sp->path, &sp->fd, NULL);
        if (STOR_PRINTF_SUCC != err) {
            goto err_exit;
        }
    }

    /* 读取前后文件头,并进行校验, 恢复 */
    memset((char *)&start_head, 0, sizeof (start_head));
    err = sp_file_head_read_and_check(sp->fd, SP_FILE_START_HEAD_OFFSET, &start_head, param);
    start_head_ok = (STOR_PRINTF_SUCC == err) ? 1 : 0;

    if (start_head_ok && (0 == start_head.status.allocated)) {
        end_head_ok = 0;
    }
    else {
        memset((char *)&end_head, 0, sizeof (end_head));
        err = sp_file_head_read_and_check(sp->fd, SP_FILE_END_HEAD_OFFSET(param->sp_datasize), &end_head, param);
        end_head_ok = (STOR_PRINTF_SUCC == err) ? 1 : 0;
    }
    
    err = sg_sp_file_heads_cmp_sync[start_head_ok][end_head_ok](sp, param, &start_head, &end_head);
    if (STOR_PRINTF_SUCC != err) {
        goto err_exit;
    }    

    /* TODO: 对文件数据体进行检查,恢复因索引未即使更新而丢失的数据体 */ 

    SAFE_CLOSE(sp->fd);

    *pp_sp = sp;

    return STOR_PRINTF_SUCC;

err_exit:

    if (NULL != sp) {
        sp_file_close(sp, 0);
        sp = NULL;
    }

    return err;

}

static enum STOR_PRINTF_ERRNO sp_file_write(struct sp_t *sp, unsigned int write_time, const char *buff, unsigned int len, int head_sync_inter, int *head_syncd)
{
    int ret;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
	enum STOR_PRINTF_ERRNO tmperr = STOR_PRINTF_SUCC;
    int sync_head = 0;
	char tmpstr[32] = {0};

    if (NULL == sp || NULL == buff || 0 == len) {
        ERR("INVALID input param, sp = %p, buff = %p, len = %u\n", sp, buff, len);
        return STOR_PRINTF_INVALID_INPUT;
    }

    if (-1 == sp->fd) {
        sp->fd = open(sp->path, O_RDWR, 0666);
        if (-1 == sp->fd) {
            ERR("FAIL to open %s for read and write, %s\n", sp->path, strerror(errno));
            err = STOR_PRINTF_FILE_OPEN_FAIL;
            goto err_exit;
        }
    }

    do {
        ret = lseek(sp->fd, SP_FILE_OFFSET(sp->head.data_offset), SEEK_SET);
        if (-1 == ret) {
            ERR("FAIL to lseek, SP_FILE_OFFSET(sp->head.data_offset = %u) = %u, sizeof (struct sp_head_t) = %d, data_size = %u\n",
                sp->head.data_offset, (int)SP_FILE_OFFSET(sp->head.data_offset), (int)sizeof (struct sp_head_t), sp->head.data_size);
            err = STOR_PRINTF_FILE_LSEEK_FAIL;
            goto err_exit;
        }
        ret = write(sp->fd, buff, len);
    } while (-1 == ret && EINTR == errno);
    
    if (len != ret) {
        ERR("FAIL to write %u data to %s, ret = %d, %s\n", len, sp->path, ret, strerror(errno));
        err = STOR_PRINTF_FILE_WRITE_FAIL;
        goto err_exit;
    }

    if (0 == sp->head.data_offset) {
        INFO("FIRST data_offset %u to write sp file(%s) heads, total_wr_count: %u\n", sp->head.data_offset, sp->path, sp->head.head_total_writecount);
        sp->head.data_offset = len;
        sync_head = 1;
    }
    else {
        sp->head.data_offset += len;
    }
    if (sp_file_is_full(sp)) {
		INFO(" FULL to write sp file(%s) heads, total_wr_count: %u\n", sp->path, sp->head.head_total_writecount);
        sync_head = 1;
    }

    if (0 == sp->head.start_time) {
        sp->head.start_time = write_time;
		INFO("FIRST time \"%s\" to write sp file(%s) heads, total_wr_count: %u\n", strtime((time_t)sp->head.start_time, tmpstr, sizeof (tmpstr)), sp->path, sp->head.head_total_writecount);
        sync_head = 1;
    }
    else {
        if (write_time < sp->head.start_time) {
            /* 系统发生校时, 将时间校回先前的时间, 导致当前时间比该sp文件的起始时间还早时, 则立即更新该sp文件的起始时间 */
            sp->head.status.starttime_adjust = 1;
            sp->head.start_time = write_time;
            INFO("TIME_ADJUST time \"%s\" to write sp file(%s) heads, total_wr_count: %u\n", strtime((time_t)sp->head.start_time, tmpstr, sizeof (tmpstr)), sp->path, sp->head.head_total_writecount);
            sync_head = 1;
        }
    }
    sp->head.end_time = write_time;

    if (!sync_head) {
        if (-1 != head_sync_inter && labs(time(NULL) - sp->head.head_writetime) >= head_sync_inter) {
			DBG(" TIME \"%s\" to write sp file(%s) heads, total_wr_count: %u\n", strtime(time(NULL), tmpstr, sizeof (tmpstr)), sp->path, sp->head.head_total_writecount);
            sync_head = 1;
        }
    }

    if (sync_head) {
        err = sp_file_heads_write(sp);
		if (STOR_PRINTF_SUCC != err) {
			WARN("FAIL to write sp file(%s) heads, %s\n", sp->path, stor_printf_strerr(err));
	    }
    }

    if (NULL != head_syncd) {
        *head_syncd = sync_head;
    }

    if (sp_file_is_full(sp)) {
        SAFE_CLOSE(sp->fd);
		return STOR_PRINTF_SP_FILE_FULL;
    }
    
    return STOR_PRINTF_SUCC;

err_exit:

    sp->head.status.error = 1;
	ERR("MARK ERROR to write sp file(%s) heads\n", sp->path);
    tmperr = sp_file_heads_write(sp);
	if (STOR_PRINTF_SUCC != tmperr) {
		WARN("FAIL to write sp file(%s) heads, %s\n", sp->path, stor_printf_strerr(tmperr));
    }
    SAFE_CLOSE(sp->fd);

    return err;

}

static enum STOR_PRINTF_ERRNO stor_printf_check(const struct stor_printf_param_t *param)
{
    if (NULL == param) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    if ('\0' == param->filename[0]) {
        return STOR_PRINTF_INVALID_ARGS;
    }

    return STOR_PRINTF_SUCC;
    
}

static inline void stor_printfs_init(struct stor_printfs_t *storprintfs)
{
    int i;
    struct stor_printf_t *storprintf = NULL;

    for (i = 0; i < ARRAY_SIZE(storprintfs->storprintf); i++) {
        storprintf = &(storprintfs->storprintf[i]);

        memset((char *)storprintf, 0, sizeof (*storprintf));
        pthread_mutex_init(&storprintf->mutex, NULL);
        storprintf->inuse = 0;

        pthread_cond_init(&storprintf->cond, NULL);
        storprintf->refcount = 0;

        pthread_mutex_init(&storprintf->chat_mutex, NULL);
        storprintf->chatfds[0] = -1;
        storprintf->chatfds[1] = -1;

        storprintf->wakeupfds[0] = -1;
        storprintf->wakeupfds[1] = -1;

        pthread_rwlock_init(&storprintf->splst_rwlock, NULL);
        lstInit(&storprintf->splst);

        pthread_rwlock_init(&storprintf->selst_rwlock, NULL);
        lstInit(&storprintf->selst);

        storprintf->tid = (pthread_t)-1;       
        
        storprintf->prtbuff = NULL;    
    }

    return;

}

static enum STOR_PRINTF_ERRNO stor_printf_search_fini(struct stor_printf_search_t *storprintf_se, int fini_spsearch_lst, int fini_self)
{
    if (NULL == storprintf_se) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    if (NULL != storprintf_se->fh) {
        fclose(storprintf_se->fh);
        storprintf_se->fh = NULL;
    }

    if (fini_spsearch_lst) {
        lstFree(&storprintf_se->spsearch_lst);
    }

    if (fini_self) {
        SAFE_FREE(storprintf_se);
    }

    return STOR_PRINTF_SUCC;
    
}

static inline enum STOR_PRINTF_ERRNO _stor_printf_fini(struct stor_printf_t *storprintf, int sync_head)
{
    struct stor_printf_search_t *storprintf_se = NULL;
    struct stor_printf_search_t *storprintf_se_nxt = NULL;
    struct sp_t *sp = NULL;
    struct sp_t *sp_nxt = NULL;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    SAFE_FREE(storprintf->prtbuff);
    

    if (0 == pthreadIdVerify(storprintf->tid)) {
        do {
            (void)write(storprintf->wakeupfds[1], "exit", sizeof ("exit"));
            if (0 == pthread_join(storprintf->tid, NULL)) {
                INFO("SUCC to wait stor printf main thread to exit...\n");
                break;
            }
        } while (0 == pthreadIdVerify(storprintf->tid));
    }
    
    storprintf->tid = (pthread_t)-1;

    /* storprintf_se lst资源释放 */
    pthread_rwlock_wrlock(&storprintf->selst_rwlock);
    {
        for (storprintf_se = (struct stor_printf_search_t *)lstFirst(&storprintf->selst); NULL != storprintf_se; ) {
            storprintf_se_nxt = (struct stor_printf_search_t *)lstNext(&storprintf_se->node);
            lstDelete(&storprintf->selst, &(storprintf_se->node));
            
            err = stor_printf_search_fini(storprintf_se, 1, 1);
            if (STOR_PRINTF_SUCC != err) {
                WARN("FAIL to fini storprintf_se, %s\n", stor_printf_strerr(err));
            }

            storprintf_se = storprintf_se_nxt;
        }
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    /* splst资源释放 */
    pthread_rwlock_wrlock(&storprintf->splst_rwlock);
    {
        for (sp = (struct sp_t *)lstFirst(&storprintf->splst); NULL != sp; ) {
            sp_nxt = (struct sp_t *)lstNext(&sp->node);
            lstDelete(&storprintf->splst, &sp->node);

            err = sp_file_close(sp, sync_head);
            if (STOR_PRINTF_SUCC != err) {
                WARN("FAIL to close sp file(%s), %s\n", sp->path, stor_printf_strerr(err));
            }
            
            sp = sp_nxt;
        }
    }
    pthread_rwlock_unlock(&storprintf->splst_rwlock);
    
    SAFE_CLOSE(storprintf->wakeupfds[0]);
    SAFE_CLOSE(storprintf->wakeupfds[1]);
    
    SAFE_CLOSE(storprintf->chatfds[0]);
    SAFE_CLOSE(storprintf->chatfds[1]);

    memset((char *)&storprintf->param, 0, sizeof (storprintf->param));

    storprintf->refcount = 0;
    
    return STOR_PRINTF_SUCC;

}

static inline enum STOR_PRINTF_ERRNO _stor_printf_init(const struct stor_printf_param_t *param, struct stor_printf_t *storprintf)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    storprintf->refcount = 0;
    
    memcpy((char *)&storprintf->param, (char *)param, sizeof (struct stor_printf_param_t));

    if (-1 == socketpair(AF_LOCAL, SOCK_DGRAM, 0, storprintf->chatfds)) {
        ERR("FAIL to create socketpair for chat, %s\n", strerror(errno));
        err = STOR_PRINTF_SOCKETPAIR_FAIL;
        goto err_exit;
    }

    if (-1 == socketpair(AF_LOCAL, SOCK_DGRAM, 0, storprintf->wakeupfds)) {
        ERR("FAIL to create socketpair for wakeup, %s\n", strerror(errno));
        err = STOR_PRINTF_SOCKETPAIR_FAIL;
        goto err_exit;
    }

    lstInit(&storprintf->splst);

    lstInit(&storprintf->selst);

    storprintf->tid = (pthread_t)-1;

    storprintf->prtbuff = (char *)malloc(STOR_PRINTF_PRTBUFF_SIZE);
    if (NULL == storprintf->prtbuff) {
        ERR("OUT of memory!!!\n");
        err = STOR_PRINTF_OUT_OF_MEMORY;
        goto err_exit;
    }
    memset(storprintf->prtbuff, 0, STOR_PRINTF_PRTBUFF_SIZE);

    return STOR_PRINTF_SUCC;

err_exit:

    (void)_stor_printf_fini(storprintf, 0);

    return err;

}

static enum STOR_PRINTF_ERRNO stor_printf_init(const struct stor_printf_param_t *param, struct stor_printf_t **pp_storprintf)
{
    struct stor_printf_t *storprintf = NULL;
    int i;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (NULL == pp_storprintf) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    pthread_mutex_lock(&sg_storprintfs.mutex);
    {
        if (!sg_storprintfs.init) {
            stor_printfs_init(&sg_storprintfs);
            sg_storprintfs.init = 1;
        }
    
        for (i = 0; i < ARRAY_SIZE(sg_storprintfs.storprintf); i++) {
            storprintf = &(sg_storprintfs.storprintf[i]);
            if (storprintf->inuse) {
                continue;
            }
            err = _stor_printf_init(param, storprintf);
            if (STOR_PRINTF_SUCC == err) {
                storprintf->inuse = 1;
                *pp_storprintf = storprintf;
            }
            pthread_mutex_unlock(&sg_storprintfs.mutex);
            return err;
        }
    }
    pthread_mutex_unlock(&sg_storprintfs.mutex);   

    return STOR_PRINTF_NO_RESOURCES;

}

static inline enum STOR_PRINTF_ERRNO stor_printf_userdata_read(struct stor_printf_t *storprintf, char *buff, unsigned int size, unsigned int *len, int waitms)
{
    fd_set rset;
    int nfds = -1;
    int ret = -1;
    struct timeval timeout;
    struct timeval *p_timeout;     

    FD_ZERO(&rset);
    FD_SET(storprintf->chatfds[0], &rset);
    nfds = storprintf->chatfds[0];
    FD_SET(storprintf->wakeupfds[0], &rset);
    nfds = max(nfds, storprintf->wakeupfds[0]);

    if (waitms < 0) {
        p_timeout = NULL;
    }
    else {
        p_timeout = &timeout;
        p_timeout->tv_sec = waitms / 1000;
        p_timeout->tv_usec = (waitms % 1000) * 1000;
    }

    do {
        ret = select(nfds + 1, &rset, NULL, NULL, p_timeout);
    } while (-1 == ret && EINTR == errno);
    if (-1 == ret) {
        ERR("chatfds[0] or wakeupfds[0] FAIL to select for read, %s\n", strerror(errno));
        return STOR_PRINTF_SELECT_FAIL;
    }

    if (0 == ret) {
        return STOR_PRINTF_TIMEOUT;
    }

    if (FD_ISSET(storprintf->wakeupfds[0], &rset)) {
        return STOR_PRINTF_IO_WAKEUP;
    }

    if (FD_ISSET(storprintf->chatfds[0], &rset)) {
        do {
            ret = read(storprintf->chatfds[0], buff, size);
        } while (-1 == ret && EINTR == errno);
        if (-1 == ret) {
            ERR("chatfds[0] %d, FAIL to read, %s\n", storprintf->chatfds[0], strerror(errno));
        }

        if (NULL != len) {
            *len = (unsigned int)ret;
        }

        return STOR_PRINTF_SUCC;
    }

    return STOR_PRINTF_IO_ERROR;

}

static enum STOR_PRINTF_ERRNO stor_printf_available_sp_file_get(struct stor_printf_t * storprintf, struct sp_t **pp_sp)
{
    struct sp_t *sp = NULL;
    struct sp_t *nearest_inuse_sp = NULL;
    struct sp_t *oldest_sp = NULL;

    if (NULL != storprintf->sp_inuse) {
        sp = storprintf->sp_inuse;
        goto found;
    }

    /* I 尝试寻找其他正在使用的文件 */
    LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
        if (sp->head.status.error) {
            /* 跳过所有出错的文件 */
            continue;
        }
        /* 查找一个空闲的sp文件 */
        if (sp_file_is_inuse(sp)) {
            if (NULL == nearest_inuse_sp) {
                nearest_inuse_sp = sp;
            }
            else {
                if (sp->head.end_time > nearest_inuse_sp->head.end_time) {
                    nearest_inuse_sp = sp;
                }
            }
        }
    }
    if (NULL != nearest_inuse_sp) {
        sp = nearest_inuse_sp;
        goto found;
    }

    /* II 尝试查找一个空闲的sp文件 */
    LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
        if (sp->head.status.error) {
            /* 跳过所有出错的文件 */
            continue;
        }
        /* 查找一个空闲的sp文件 */
        if (sp_file_is_empty(sp)) {
            goto found;
        }
    }

    /* III 寻找一个时间最老的文件 */
    LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
        if (sp->head.status.error) {
            /* 跳过所有出错的文件 */
            continue;
        }
        if (NULL == oldest_sp) {
            oldest_sp = sp;
        }
        else {
            if (sp->head.start_time < oldest_sp->head.start_time) {
                oldest_sp = sp;
            }
        }
    }      
    if (NULL == oldest_sp) {
        return STOR_PRINTF_NO_AVAILABLE_SP_FILE;
    }
    oldest_sp->head.data_offset = 0;
    oldest_sp->head.start_time = 0;
    oldest_sp->head.end_time = 0;
    sp = oldest_sp;

found:

	storprintf->sp_inuse = sp;

    if (NULL != pp_sp) {
        *pp_sp = sp;
    }

    return STOR_PRINTF_SUCC;

}

static void * stor_printf_main_thread(void *arg)
{
    struct stor_printf_t *storprintf = (struct stor_printf_t *)arg;
    char threadname[16] = {0};
    char buff[STOR_PRINTF_PRTBUFF_SIZE] = {0};
    unsigned int len = 0;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    struct sp_t *sp = NULL;
    unsigned int write_time = 0;
    int head_syncd = 0;
    char tmpstr[32] = {0};

    if (NULL == storprintf) {
        ERR("INVALID input param, arg = %p\n", arg);
        return NULL;
    }

#ifndef __APPLE__
    snprintf(threadname, sizeof (threadname), "storprintf%d", stor_printf_to_id(storprintf));
    prctl(PR_SET_NAME, threadname);
#endif

    /* 加入实际的文件管理及日志记录功能 */
    for (; ;) {
        err = stor_printf_userdata_read(storprintf, buff, sizeof (buff), &len, 1000*30);
        if (STOR_PRINTF_SUCC != err) {
            if (STOR_PRINTF_IO_WAKEUP == err) {
                INFO("USER wakeup \"%s\", exit now\n", threadname);
                goto exit;
            }
            if (STOR_PRINTF_TIMEOUT == err) {
                DBG("Got userdata timeout, sp_inuse = %p(%s), head_syncd = %d\n", storprintf->sp_inuse, (NULL == storprintf->sp_inuse) ? "NULL" : storprintf->sp_inuse->path, head_syncd);
                if (NULL != storprintf->sp_inuse && !head_syncd && -1 != storprintf->param.sp_head_sync_inter) {
                    if (labs(time(NULL) - storprintf->sp_inuse->head.head_writetime) >= storprintf->param.sp_head_sync_inter) { 
                        DBG(" TIME \"%s\" to write sp file(%s) heads\n", strtime(time(NULL), tmpstr, sizeof (tmpstr)), storprintf->sp_inuse->path);
                        err = sp_file_heads_write(storprintf->sp_inuse);
                		if (STOR_PRINTF_SUCC != err) {
                            /* TODO: 如果这里写失败了怎么办??? */
                			WARN("FAIL to write sp file(%s) heads, %s\n", storprintf->sp_inuse->path, stor_printf_strerr(err));
                	    }
                        head_syncd = 1;
                    }
                }
                continue;
            }
            ERR("FAIL to read user data, %s\n", stor_printf_strerr(err));
            goto exit;
        }

        /* 缓存结构
         * unsigned int 结构的时间（4B）+ 字符串数据, 长度必须大于0
         */
        if (len <=sizeof (unsigned int)) {
            continue;
        }

        /* 遍历链表, 查找最合适的文件写入数据 */
        pthread_rwlock_rdlock(&storprintf->splst_rwlock);
        {
            err = stor_printf_available_sp_file_get(storprintf, &sp);
            if (STOR_PRINTF_SUCC != err) {
                /* 当前没有可用的sp文件, 继续等待 */
                DBG("NO available sp file, discard user data, %s\n", stor_printf_strerr(err));
                pthread_rwlock_unlock(&storprintf->splst_rwlock);
                continue;
            }

            memcpy((char *)&write_time, buff, sizeof (unsigned int));
            err = sp_file_write(sp, write_time, buff + sizeof (unsigned int), len - sizeof (unsigned int), storprintf->param.sp_head_sync_inter, &head_syncd);
            if (STOR_PRINTF_SUCC != err) {
				if (STOR_PRINTF_SP_FILE_FULL == err) {
					WARN(" FULL sp file(%s), set sp_inuse => NULL\n", sp->path);
			    }
				else {
                	WARN("FAIL to write user data to sp file(%s), %s, set sp_inuse => NULL\n", sp->path, stor_printf_strerr(err));
			    }
                storprintf->sp_inuse = NULL; /* 调过当前错误的sp文件, 并且去寻找下一个可用的sp文件 */
            }
        }
        pthread_rwlock_unlock(&storprintf->splst_rwlock);
    }

exit:

    return NULL;

}

static enum STOR_PRINTF_ERRNO stor_printf_main(struct stor_printf_t *storprintf)
{
    int ret = 0;

    if (-1 == pthreadIdVerify(storprintf->tid)) {
        ret = pthread_create(&storprintf->tid, NULL, stor_printf_main_thread, storprintf);
        if (0 != ret) {
            ERR("FAIL to create pthread, %s\n", strerror(ret));
            return STOR_PRINTF_PTHREAD_CREATE_FAIL;
        }
    }

    return STOR_PRINTF_SUCC;

}

static int stor_printf_is_dir_exist(const char *dirpath)
{
    DIR *dir = NULL;

    dir = opendir(dirpath);
    if (NULL == dir) {
        return 0;
    }

    closedir(dir);
    dir = NULL;
    
    return 1;
    
}

static inline int _stor_printf_dir_make(char *path, long mode)
{
	mode_t mask;
	const char *fail_msg = "success";
	char *s = path;
	char c;
	struct stat st;

	mask = umask(0);
	if (mode == -1) {
		umask(mask);
		mode = (S_IXUSR | S_IXGRP | S_IXOTH |
				S_IWUSR | S_IWGRP | S_IWOTH |
				S_IRUSR | S_IRGRP | S_IROTH) & ~mask;
	} else {
		umask(mask & ~0300);
	}

	do {
		c = 0;

		/* Bypass leading non-'/'s and then subsequent '/'s. */
		while (*s) {
			if (*s == '/') {
				do {
					++s;
				} while (*s == '/');
				c = *s;		/* Save the current char */
				*s = 0;		/* and replace it with nul. */
				break;
			}
			++s;
		}

		if (mkdir(path, 0777) < 0) {
			/* If we failed for any other reason than the directory
			 * already exists, output a diagnostic and return 0.*/
			if (errno != EEXIST) {
				umask(mask);
				return 0;
			}
            else if ((stat(path, &st) < 0 || !S_ISDIR(st.st_mode))) {
                umask(mask);
				break;
            }
			/* Since the directory exists, don't attempt to change
			 * permissions if it was the full target.  Note that
			 * this is not an error conditon. */
			if (!c) {
				umask(mask);
				return 0;
			}
		}

		if (!c) {
			/* Done.  If necessary, updated perms on the newly
			 * created directory.  Failure to update here _is_
			 * an error.*/
			umask(mask);
			if ((mode != -1) && (chmod(path, mode) < 0)){
				fail_msg = "set permissions of";
				break;
			}
			return 0;
		}

		/* Remove any inserted nul from the path (recursive mode). */
		*s = c;

	} while (1);

	ERR("cannot %s directory '%s'", fail_msg, path);
	return -1;
    
}

static enum STOR_PRINTF_ERRNO stor_printf_dir_make(const char *dirpath)
{
    char *tmp_dirpath = NULL;

    if (NULL == dirpath || '\0' == dirpath[0]) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    tmp_dirpath = strdup(dirpath);
    if (NULL == tmp_dirpath) {
        ERR("OUT OF MEMORY, dirpath: %s\n", dirpath);
        return STOR_PRINTF_OUT_OF_MEMORY;
    }

    if (-1 == _stor_printf_dir_make(tmp_dirpath, -1)) {
        SAFE_FREE(tmp_dirpath);
        return STOR_PRINTF_CREATE_DIR_FAIL;
    }

    SAFE_FREE(tmp_dirpath);
    
    return STOR_PRINTF_SUCC;
    
}

static int _stor_printf_is_spfile_in_splst(LIST *splst, const char *path)
{
    struct sp_t *sp = NULL;

    if (NULL == splst || NULL == path || '\0' == path[0]) {
        return 0;
    }

    LIST_FOR_EACH(struct sp_t, sp, splst) {
        if (0 == strcmp(sp->path, path)) {
            return 1;
        }
    }

    return 0;
    
}

static inline enum STOR_PRINTF_ERRNO _stor_printf_part_add(struct stor_printf_t *storprintf, const char *partpath)
{
    struct sp_t *sp = NULL;
    int i = 0;
    char path[128] = {0};
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;   

	pthread_rwlock_wrlock(&storprintf->splst_rwlock);
	{
        /* 如果partpath不存在, 则递归创建 */
        if (!stor_printf_is_dir_exist(partpath)) {
            WARN("DIR: \"%s\" not exist, try to make it\n", path);
            err = stor_printf_dir_make(partpath);
            if (STOR_PRINTF_SUCC != err) {
                pthread_rwlock_unlock(&storprintf->splst_rwlock);
                return err;
            }
        }
        
	    for (i = 0; i < storprintf->param.sp_count; i++) {
	        /* 分区中sp文件名从0开始 */
	        snprintf(path, sizeof (path), "%s/%s%d.sp", partpath, storprintf->param.filename, i);

            /* 检查该文件是否已经添加到splst中 */
            if (_stor_printf_is_spfile_in_splst(&storprintf->splst, path)) {
                WARN("INUSE \"%s\" is already in splst\n", path);
                continue;
            }
	        
	        DBG("  TRY to open sp file: \"%s\"\n", path);
	        err = sp_file_open(&storprintf->param, path, &sp);
	        if (STOR_PRINTF_SUCC != err) {
                if (STOR_PRINTF_SPACE_NOT_ENOUGH == err) {
                    WARN("NO SPACE for \"%s\", abort left sp files\n", path);
                    break;
                }
                if (STOR_PRINTF_SP_BOTH_HEAD_ERROR == err) {
                    WARN("BOTH HEAD error, REMOVE sp file: \"%s\"\n", path);
                    (void)remove(path);
                }
	            WARN("FAIL to open sp file(%s), %s\n", path, stor_printf_strerr(err));
	            continue;
	        }
            
	        DBG(" SUCC to open sp file: \"%s\"\n", path);

			/* TODO: 对于文件头状态字段中被标记成error的sp文件是否要加到splst中? */

	        /* 遍历sp文件,  打开sp文件,并添加到sp文件链表中 */
            lstAdd(&storprintf->splst, &(sp->node));
            if (NULL == storprintf->sp_inuse && sp_file_is_inuse(sp)) {
                storprintf->sp_inuse = sp;
            }
	    }
	}
	pthread_rwlock_unlock(&storprintf->splst_rwlock);

    return STOR_PRINTF_SUCC;

}

static inline int stor_printf_is_spfile_in_time_range(const struct sp_t *sp, unsigned int starttime, unsigned int endtime)
{
    if (sp->head.end_time < starttime || sp->head.start_time > endtime) {
        return 0;
    }

    return 1;
    
}

static enum STOR_PRINTF_ERRNO stor_printf_search_delete_specified_spfile(struct stor_printf_search_t *storprintf_se, const char *sp_path)
{
    struct sp_search_t *sp_se = NULL;
    char tmppath[128] = {0};

    if (NULL == storprintf_se || NULL == sp_path || '\0' == sp_path[0]) {
        ERR("INVALID input param, storprintf_se = %p, sp_path: %s\n", storprintf_se, sp_path);
        return STOR_PRINTF_INVALID_INPUT;
    }

    /* 删除sp_search_t */
    LIST_FOR_EACH(struct sp_search_t, sp_se, &storprintf_se->spsearch_lst) {
        if (0 == strcmp(sp_se->path, sp_path)) {
            break;
        }
    }
    if (NULL == sp_se) {
        return STOR_PRINTF_SUCC;
    }
    lstDelete(&storprintf_se->spsearch_lst, &(sp_se->node));

    /* 关闭对应的fh */
    if (NULL != storprintf_se->fh) {
        if (NULL != stor_printf_fd_2_path(fileno(storprintf_se->fh), tmppath, sizeof (tmppath))) {
            if (0 == strcmp(tmppath, sp_path)) {
                fclose(storprintf_se->fh);
                storprintf_se->fh = NULL;
            }
        }
    }

    return STOR_PRINTF_SUCC;
    
}

static enum STOR_PRINTF_ERRNO _stor_printf_search_start(struct stor_printf_t *storprintf, const struct stor_printf_search_param_t *param, struct stor_printf_search_t **pp_se)
{
    struct stor_printf_search_t *storprintf_se = NULL;
    char tags_convert[256] = {0};
    struct sp_search_t *sp_se = NULL;
    struct sp_t *sp = NULL;
    char str_starttime[32] = {0};
    char str_endtime[32] = {0};

    if (param->endtime < param->starttime) {
        ERR("INVALID search time range, endtime < starttime\n");
        return STOR_PRINTF_INVALID_ARGS;
    }

    storprintf_se = (struct stor_printf_search_t *)malloc(sizeof (*storprintf_se));
    if (NULL == storprintf_se) {
        ERR("  OUT of memory, sizeof (struct stor_printf_search_t) = %d\n", (int)sizeof (struct stor_printf_search_t));
        return STOR_PRINTF_OUT_OF_MEMORY;
    }
    memset((char *)storprintf_se, 0, sizeof (*storprintf_se));
    memcpy((char *)&storprintf_se->param, (char *)param, sizeof (struct stor_printf_search_param_t));

    /* 根据用户tags组正则表达式 */
    if ('\0' == storprintf_se->param.tags[0]) {
        SAFE_STRNCPY(storprintf_se->tags_regex, "^\\[.*\\][0-9]{8}_[0-9]{6},", sizeof (storprintf_se->tags_regex));
    }
    else {
        if (NULL == strchr(storprintf_se->param.tags, '#')) {
            SAFE_STRNCPY(tags_convert, storprintf_se->param.tags, sizeof (tags_convert));
        }
        else {
            /* 将用户指定的搜索tags转换成正则表达式需要的格式, eg: dial#gps#gpsinfo => dial|gps|gpsinfo */
            if (-1 == str_seg_replace(storprintf_se->param.tags, "#", "|", tags_convert, sizeof (tags_convert))) {
                SAFE_FREE(storprintf_se);
                return STOR_PRINTF_SEARCH_TAGS_CONVERT_FAIL;
            }
        }
        snprintf(storprintf_se->tags_regex, sizeof (storprintf_se->tags_regex), "^\\[(%s)\\][0-9]{8}_[0-9]{6},", tags_convert);
    }

    /* 从sp文件列表中找到时间上符合要求的信息, 并将该信息存放到spsearch列表中 */
    pthread_rwlock_rdlock(&storprintf->splst_rwlock);
    {
        LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
            if (1 == sp->head.status.error) {
                WARN("IGNORE status error sp file: %s\n", sp->path);
                continue;
            }
            if (stor_printf_is_spfile_in_time_range(sp, (unsigned int)storprintf_se->param.starttime, (unsigned int)storprintf_se->param.endtime)) {
                sp_se = (struct sp_search_t *)malloc(sizeof (*sp_se));
                if (NULL == sp_se) {
                    lstFree(&storprintf_se->spsearch_lst);
                    SAFE_FREE(storprintf_se);
                    pthread_rwlock_unlock(&storprintf->splst_rwlock);
                    return STOR_PRINTF_OUT_OF_MEMORY;
                }
                INFO("ADD sp_se: %s(%s => %s) => storprintf_se->spsearch_lst\n", sp->path, strtime((time_t)sp->head.start_time, str_starttime, sizeof (str_starttime)), strtime(sp->head.end_time, str_endtime, sizeof (str_endtime)));
                memset((char *)sp_se, 0, sizeof (*sp_se));
                SAFE_STRNCPY(sp_se->path, sp->path, sizeof (sp_se->path));
                sp_se->starttime_adjust = (1 == sp->head.status.starttime_adjust) ? 1 : 0;
                sp_se->data_size = sp->head.data_size;
                sp_se->data_offset = sp->head.data_offset;
                sp_se->start_time = sp->head.start_time;
                sp_se->end_time = sp->head.end_time;
                lstAdd(&storprintf_se->spsearch_lst, &(sp_se->node));
            }
        }
    }
    pthread_rwlock_unlock(&storprintf->splst_rwlock);

    storprintf_se->fh = NULL;

    pthread_rwlock_wrlock(&storprintf->selst_rwlock);
    {
        lstAdd(&storprintf->selst, &(storprintf_se->node));
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    if (NULL != pp_se) {
        *pp_se = storprintf_se;
    }

    return STOR_PRINTF_SUCC;

}

static int stor_printf_search_is_valid(struct stor_printf_t *storprintf, const struct stor_printf_search_t *storprintf_se)
{
    struct stor_printf_search_t *p = NULL;

    if (NULL == storprintf || NULL == storprintf_se) {
        return 0;
    }

    LIST_FOR_EACH(struct stor_printf_search_t, p, &storprintf->selst) {
        if ((struct stor_printf_search_t *)storprintf_se == p) {
            return 1;
        }
    }
    
    return 0;
    
}

static enum STOR_PRINTF_ERRNO stor_printf_time_str_to_time_t(const char *str, unsigned int *t)
{
    struct tm tmp_tm;

    if (NULL == str || '\0' == str[0] || NULL == t) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    /*eg: [GPS]20190808_190413, => unsigned int */
    memset((char *)&tmp_tm, 0, sizeof (tmp_tm));
    sscanf(str, "[%*[^]]]%04d%02d%02d_%02d%02d%02d,", 
        &tmp_tm.tm_year, &tmp_tm.tm_mon, &tmp_tm.tm_mday, &tmp_tm.tm_hour, &tmp_tm.tm_min, &tmp_tm.tm_sec);
    tmp_tm.tm_year -= 1900;
    tmp_tm.tm_mon -= 1;
    *t = myself_mktime(&tmp_tm);

    return STOR_PRINTF_SUCC;

}

/* -1: 字符串时间小于搜索起始时间 
 *  0: 字符串时间在搜索时间范围内
 *  1: 字符串时间大于搜索结束时间
 */
static int stor_printf_search_timestring_cmp(struct stor_printf_search_t *storprintf_se, const char *str)
{
    unsigned int t = 0;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    /* 解析时间字符串, 并通过时间来检索 */
    err = stor_printf_time_str_to_time_t(str, &t);
    if (STOR_PRINTF_SUCC != err) {
        ERR("FAIl to convert time_str: \"%s\" => time_t(unsigned int), %s\n", str, stor_printf_strerr(err));
        return -1;
    }

    if (t < (unsigned int)storprintf_se->param.starttime) {
        return -1;
    }

    if (t > (unsigned int)storprintf_se->param.endtime) {
        return 1;
    }

    return 0;
    
}

static long stor_printf_timems_get(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));

}

static enum STOR_PRINTF_ERRNO stor_printf_search_start_offset(struct stor_printf_search_t *storprintf_se, unsigned int start_offset, unsigned int end_offset, unsigned int min_timeoffset)
{
    unsigned int s_off = 0;
    unsigned int m_off = 0;
    unsigned int e_off = 0;
    char buff[1024] = {0};
    unsigned int t = 0;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (NULL == storprintf_se || NULL == storprintf_se->fh || start_offset >= end_offset) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    s_off = start_offset;
    e_off = end_offset;
    m_off = (s_off + e_off) / 2;

    while (m_off < e_off) {
        DBG("s_off = %u, m_off = %u, e_off = %u\n", s_off, m_off, e_off);
        if (-1 == fseek(storprintf_se->fh, m_off, SEEK_SET)) {
            ERR("FAIL to fseek, fh = %p, seek_offset = %u, %s\n", storprintf_se->fh, m_off, strerror(errno));
            err = STOR_PRINTF_FILE_LSEEK_FAIL;
            goto err_exit;
        }

        /* 找到第一个有时间的字符串 */
        t = 0;
        do {
            if (NULL == fgets(buff, sizeof (buff), storprintf_se->fh)) {
                /* ??? 这里该如何处理 */
                ERR("FAIL to fgets, fh = %p, offset = %u, THIS SHOULD NEVER HAPPEN\n", storprintf_se->fh, m_off);
                err = STOR_PRINTF_FGETS_FAIL;
                goto err_exit;
            }

            if (is_regex_match(buff, "^\\[.*\\][0-9]{8}_[0-9]{6},", REG_EXTENDED | REG_ICASE)) {
                err = stor_printf_time_str_to_time_t(buff, &t);
                if (STOR_PRINTF_SUCC == err) {
                    break;
                }
                else {
                    WARN("FAIl to convert time_str: \"%s\" => time_t(unsigned int), %s\n", buff, stor_printf_strerr(err));
                }
            }

            m_off = (unsigned int)ftell(storprintf_se->fh);
            if (m_off >= e_off) {
                break;
            }
        } while (1);

        if (0 == t) {
            e_off = m_off;
        }
        else {
            if (t >= (unsigned int)storprintf_se->param.starttime) {
                e_off = m_off;
            }
            else if (((unsigned int)storprintf_se->param.starttime - t) > min_timeoffset) {
                s_off = m_off;
            }
            else {
                break;
            }
            m_off = (s_off + e_off) / 2;
        }        
    }

    return STOR_PRINTF_SUCC;

err_exit:

    (void)fseek(storprintf_se->fh, start_offset, SEEK_SET);

    return err;
    
}

static enum STOR_PRINTF_ERRNO __stor_printf_search_next(struct stor_printf_t *storprintf, struct stor_printf_search_t *storprintf_se, void *buff, unsigned int size, unsigned int *len, int waitms)
{
    struct sp_search_t *sp_se = NULL; 
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    unsigned int loop_count = 0;
    long startsearch_ms = 0;
    int cmp_result = -1;

    /* 先判断storprintf_se是否合法 */
    if (!stor_printf_search_is_valid(storprintf, storprintf_se)) {
        return STOR_PRINTF_BAD_SEARCH_HANDLE;
    }

    loop_count = 0;
    startsearch_ms = stor_printf_timems_get();
    
    do {
        loop_count++;
        
        sp_se = (struct sp_search_t *)lstFirst(&storprintf_se->spsearch_lst);
        if (NULL == sp_se) {
            return STOR_PRINTF_SEARCH_COMPLETE;
        }

        if (NULL == storprintf_se->fh) {
            storprintf_se->fh = fopen(sp_se->path, "r");
            if (NULL == storprintf_se->fh) {
                WARN("FAIL to open \"%s\", %s, try next...\n", sp_se->path, strerror(errno));
                goto try_next;
            }
            /* 切换到数据开始的地方 */
            if (-1 == fseek(storprintf_se->fh, SP_FILE_OFFSET(0), SEEK_SET)) {
                WARN("FAIL to fseek, \"%s\", fh = %p, seek_offset = %u, %s\n", sp_se->path, storprintf_se->fh, (unsigned int)SP_FILE_OFFSET(0), strerror(errno));
                goto try_next;
            }
            /* 对于sp文件的起始时间早于搜索起始时间, 并且该sp文件是时间连续的文件, 
             * 则可以通过二分法跳转到精确的位置 
             */
            if ((sp_se->start_time < (unsigned int)storprintf_se->param.starttime) && !sp_se->starttime_adjust) {
                /* 二分法跳转到起始位置 */
                stor_printf_search_start_offset(storprintf_se, SP_FILE_OFFSET(0), SP_FILE_OFFSET(0) + sp_se->data_offset, 3);
            }
        }

        /* 当搜索时间大于waitms后, 返回超时 */
        if (labs(stor_printf_timems_get() - startsearch_ms) >= waitms) {
            return STOR_PRINTF_TIMEOUT;
        }

        /* 避免占用太多cpu, 每连续循环1000次, 休眠10ms */
        if (0 == (loop_count % 1000)) {
            DBG("searching, storprintf_se = %p, sp_se->path: %s, fh = %p\n", storprintf_se, sp_se->path, storprintf_se->fh);
            err = stor_printf_waitms(storprintf, 10);
            if (STOR_PRINTF_IO_WAKEUP == err) {
                goto err_exit;
            }
        }

        /* 读取一行字符串 */
        memset(buff, 0, size);
        if (NULL == fgets(buff, size, storprintf_se->fh)) {
            goto try_next;
        }

        //DBG("Got: %s\n", (char *)buff);

        /* 判断该字符串格式上是否满足要求 */
        if (!is_regex_match(buff, storprintf_se->tags_regex, REG_EXTENDED | REG_ICASE)) {
            continue;
        }

        /* 判断该字符串时间上是否满足要求 */
        cmp_result = stor_printf_search_timestring_cmp(storprintf_se, buff);
        if (-1 == cmp_result) {
            /* 如果当前字符串时间小于搜索起始时间, 则继续检索下一个 */
            continue;
        }
        if (1 == cmp_result) {
            if (sp_se->starttime_adjust) {
                continue;
            }
            /* 如果当前字符串时间大于搜索截止时间, 并且该sp文件时间连续, 则没必要继续往下检索, 直接跳到下一个sp文件 */
            DBG("searching, storprintf_se = %p, sp_se->path: %s, fh = %p, starttime not adjust, try next\n", storprintf_se, sp_se->path, storprintf_se->fh);
            goto try_next;
        }

		if (NULL != len) {
			*len = strlen(buff);
		}
    
        /* all done 当前字符串满足需求, 返回给用户 */
        return STOR_PRINTF_SUCC;

try_next:

        (void)stor_printf_search_fini(storprintf_se, 0, 0);
        
        lstDelete(&(storprintf_se->spsearch_lst), &(sp_se->node));
    } while (1);

err_exit:

    (void)stor_printf_search_fini(storprintf_se, 0, 0);

    return err;

}

static enum STOR_PRINTF_ERRNO _stor_printf_search_next(struct stor_printf_t *storprintf, struct stor_printf_search_t *storprintf_se, void *buff, unsigned int size, unsigned int *len, int waitms)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    pthread_rwlock_rdlock(&storprintf->selst_rwlock);
    {
        err = __stor_printf_search_next(storprintf, storprintf_se, buff, size, len, waitms);
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    return err;
    
}


static enum STOR_PRINTF_ERRNO _stor_printf_spfile_search_next(struct stor_printf_t *storprintf, struct stor_printf_search_t *storprintf_se, struct stor_printf_spfile_t *spfile)
{
    struct sp_search_t *sp_se = NULL;

    pthread_rwlock_rdlock(&storprintf->selst_rwlock);
    {
        sp_se = (struct sp_search_t *)lstGet(&(storprintf_se->spsearch_lst));
        if (NULL == sp_se) {
            pthread_rwlock_unlock(&storprintf->selst_rwlock);
            return STOR_PRINTF_SEARCH_COMPLETE;
        }

        memset((char *)spfile, 0, sizeof (*spfile));
        SAFE_STRNCPY(spfile->path, sp_se->path, sizeof (spfile->path));
        spfile->file_len = sizeof (struct sp_head_t) + (sp_se->allocated ? (sp_se->data_size + sizeof (struct sp_head_t)) : sp_se->data_offset);
        spfile->starttime_adjust = sp_se->starttime_adjust;
        spfile->data_size = sp_se->data_size;
        spfile->data_offset = sp_se->data_offset;
        spfile->start_time = sp_se->start_time;
        spfile->end_time = sp_se->end_time;
        SAFE_FREE(sp_se);
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    return STOR_PRINTF_SUCC;
    
}

static enum STOR_PRINTF_ERRNO _stor_printf_search_stop(struct stor_printf_t *storprintf, struct stor_printf_search_t *storprintf_se)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    pthread_rwlock_wrlock(&storprintf->selst_rwlock);
    {
        /* 先判断storprintf_se是否合法 */
        if (!stor_printf_search_is_valid(storprintf, storprintf_se)) {
            pthread_rwlock_unlock(&storprintf->selst_rwlock);
            return STOR_PRINTF_BAD_SEARCH_HANDLE;
        }
        
        lstDelete(&storprintf->selst, &(storprintf_se->node));

        err = stor_printf_search_fini(storprintf_se, 1, 1);
        if (STOR_PRINTF_SUCC != err) {
            pthread_rwlock_unlock(&storprintf->selst_rwlock);
            return err;
        }
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    return STOR_PRINTF_SUCC;
    
}

static enum STOR_PRINTF_ERRNO _stor_printf_search_stop_all(struct stor_printf_t *storprintf)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    struct stor_printf_search_t *storprintf_se = NULL;

    pthread_rwlock_wrlock(&storprintf->selst_rwlock);
    {
        while (NULL != (storprintf_se = (struct stor_printf_search_t *)lstGet(&storprintf->selst))) {
            err = stor_printf_search_fini(storprintf_se, 1, 1);
            if (STOR_PRINTF_SUCC != err) {
                pthread_rwlock_unlock(&storprintf->selst_rwlock);
                return err;
            }
        }
    }
    pthread_rwlock_unlock(&storprintf->selst_rwlock);

    return STOR_PRINTF_SUCC;
    
}

static inline enum STOR_PRINTF_ERRNO _stor_printf_part_remove(struct stor_printf_t *storprintf, const char *partpath)
{
    struct sp_t *sp = NULL;
    int i;
    char path[128] = {0};
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC; 
    struct stor_printf_search_t *storprintf_se = NULL;

	WARN("REMOVE part(%s), sp file count = %u\n", partpath, storprintf->param.sp_count);

    for (i = 0; i < storprintf->param.sp_count; i++) {
        snprintf(path, sizeof (path), "%s/%s%d.sp", partpath, storprintf->param.filename, i);
        pthread_rwlock_wrlock(&storprintf->splst_rwlock);
        {
            LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
                if (0 == strcmp(sp->path, path)) {
                    break;
                }
            }
            if (NULL == sp) {
                /* 当前文件不在splst中, 则去检索下一个 */
                pthread_rwlock_unlock(&storprintf->splst_rwlock);
                continue;
            }
            
			WARN("REMOVE sp file(%s)\n", sp->path);
			if (sp == storprintf->sp_inuse) {
				storprintf->sp_inuse = NULL;
		    }
            lstDelete(&storprintf->splst, &sp->node);
            /* 主动关闭sp文件前, 强制刷新一次文件头 */
            err = sp_file_close(sp, 1);
            if (STOR_PRINTF_SUCC != err) {
                WARN("FAIL to close sp file(%s), %s\n", sp->path, stor_printf_strerr(err));
            }
        }
        pthread_rwlock_unlock(&storprintf->splst_rwlock);

        /* 检查path对应的sp文件是否被搜索接口占用, 如果被占用, 则占用selst_rwlock写锁, 从spsearch_lst中删除对应的sp_search_t */
        pthread_rwlock_wrlock(&storprintf->selst_rwlock);
        {
            LIST_FOR_EACH(struct stor_printf_search_t, storprintf_se, &storprintf->selst) {
                /* 从storprintf_se将与path相关的sp_search_t删掉, 并且将对应的fd关闭 */
                err = stor_printf_search_delete_specified_spfile(storprintf_se, path);
                if (STOR_PRINTF_SUCC != err) {
                    WARN("FAIL to delete sp file \"%s\" from storprintf_se: %p, %s\n", path, storprintf_se, stor_printf_strerr(err));
                }
            }
        }
        pthread_rwlock_unlock(&storprintf->selst_rwlock);
    }

    return STOR_PRINTF_SUCC;

}

static enum STOR_PRINTF_ERRNO _stor_printf_spfiles_print(struct stor_printf_t *storprintf, unsigned int prtmask, char *prtbuff, unsigned int size)
{
    struct sp_t *sp = NULL;
    struct sp_t *sp_oldest = NULL;
    struct sp_t *sp_newest = NULL;
    const char *tag = "INUSE";
	int needprt = 0;

    if (NULL == storprintf) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    pthread_rwlock_rdlock(&storprintf->splst_rwlock);
    {
        LIST_FOR_EACH(struct sp_t, sp, &storprintf->splst) {
			needprt = 0;			
			if (sp_file_is_empty(sp) && (0 != (prtmask & SPFILE_PRTMASK_EMPTY))) {
                tag = "EMPTY";
				needprt = 1;
			}
			if (sp_file_is_inuse(sp) && (0 != (prtmask & SPFILE_PRTMASK_INUSE))) {
                tag = "INUSE";
				needprt = 1;
			}
			if (sp_file_is_full(sp) && (0 != (prtmask & SPFILE_PRTMASK_FULL))) {
                tag = "FULL";
				needprt = 1;
			}
			if (needprt) {
            	sp_file_printf(sp, tag, prtbuff, size);
			}

            if (!sp_file_is_empty(sp)) {
                if (NULL == sp_oldest) {
                    sp_oldest = sp;
                }
                else {
                    if (sp->head.start_time < sp_oldest->head.start_time) {
                        sp_oldest = sp;
                    }
                }
                if (NULL == sp_newest) {
                    sp_newest = sp;
                }
                else {
                    if (sp->head.end_time > sp_newest->head.end_time) {
                        sp_newest = sp;
                    }
                }
            }
        }

        if (NULL != sp_oldest) {
            sp_file_printf(sp_oldest, "OLDEST", prtbuff, size);
        }
        if (NULL != sp_newest) {
            sp_file_printf(sp_newest, "NEWEST", prtbuff, size);
        }
    }
    pthread_rwlock_unlock(&storprintf->splst_rwlock);

	return STOR_PRINTF_SUCC;

}

static enum STOR_PRINTF_ERRNO stor_printf_fini(struct stor_printf_t *storprintf, int sync_head)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (NULL == storprintf) {
        return STOR_PRINTF_INVALID_INPUT;
    }

    err = _stor_printf_fini(storprintf, sync_head);

    pthread_mutex_lock(&sg_storprintfs.mutex);
    {
        storprintf->inuse = 0;
    }
    pthread_mutex_unlock(&sg_storprintfs.mutex);

    return err;
    
}

#ifdef __cplusplus
extern "C" {
#endif

#define STORPRINTF_REFCOUNT_INC_EX(storprintf, id, err, ret) do { \
	pthread_mutex_lock(&storprintf->mutex); \
    { \
        if (!storprintf->inuse) { \
            STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_BAD_ID); \
            pthread_mutex_unlock(&storprintf->mutex); \
            return ret; \
        } \
        storprintf->refcount++; \
    } \
    pthread_mutex_unlock(&storprintf->mutex); \
} while (0)

#define STORPRINTF_REFCOUNT_INC(storprintf, id, err) STORPRINTF_REFCOUNT_INC_EX(storprintf, id, err, -1)

#define STORPRINTF_REFCOUNT_DEC(storprintf) do { \
	pthread_mutex_lock(&storprintf->mutex); \
	{ \
		storprintf->refcount--; \
		if (storprintf->refcount <= 0) { \
			pthread_cond_signal(&storprintf->cond); \
		} \
	} \
	pthread_mutex_unlock(&storprintf->mutex); \
} while (0)

/**@fn          int stor_printf_open(const struct stor_printf_param_t *param, enum STOR_PRINTF_ERRNO *err)
 * @brief       创建一个stor_printf模块实例, 并返回操作id
 * @param[in]   const struct stor_printf_param_t *param    - 指向stor_printf模块参数结构的指针
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_open(const struct stor_printf_param_t *param, enum STOR_PRINTF_ERRNO *err)
{
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    struct stor_printf_t *storprintf = NULL;

    e = stor_printf_check(param);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }

    e = stor_printf_init(param, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }

    e = stor_printf_main(storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        goto err_exit;
    }

    STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_SUCC);

    return stor_printf_to_id(storprintf);

err_exit:

    if (NULL != storprintf) {
        (void)stor_printf_fini(storprintf, 0);
        storprintf = NULL;
    }

    return -1;

}

/**@fn          int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       添加一个分区到制定id的stor_printf模块中, 并在该分区中创建(如不存在)并打开对应的sp文件工stor_printf主任务记录用户数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *partpath    - 分区挂载或目录路径, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
    if (NULL == partpath || '\0' == partpath[0]) {
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return -1;
    }

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        e = _stor_printf_part_add(storprintf, partpath);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);

    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}

/**@fn          int stor_printf(int id, const char *tag, const char *format, ...)
 * @brief       像指定id的stor_printf模块中以类似于printf函数的格式记录数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *tag    - 记录在用户数据前的标签(注: 标签后面默认带有时间, 时间格式(YYYYMMDD_HHMMSS), 举例20191127_214848). 该参数为NULL或空时,不加标签及时间,只存入用户的原始数据
 * @param[in]   const char *format    - 类似于printf的格式化字符串
 * @param[out]  void
 * @return      > 0: 表示成功记录的数据长度, 0: 写入超时, -1: 失败, 通过输出参数err获取具体的错误号
 * @warning     该接口最多允许记录4096字节用户数据, 超过部分将被截断      
 */
int stor_printf(int id, const char *tag, const char *format, ...)
{
    struct stor_printf_t *storprintf = NULL;
    enum STOR_PRINTF_ERRNO *err = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    va_list args;
	int ret = -1;
	int len = 0;
	fd_set wset;
	fd_set rset;
	int nfds = -1;
	struct timeval timeout;
	struct timeval *ptimeout;
    char szTime[32] = {0};

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        DBG("INVALID stor printf id: %d, %s\n", id, stor_printf_strerr(e));
        return -1;
    }

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        if (-1 == pthreadIdVerify(storprintf->tid)) {
            DBG("stor printf main thread not exist, id: %d\n", id);
            ret = -1;
    		goto exit;
        }

        pthread_mutex_lock(&storprintf->chat_mutex);
        {
        	va_start(args, format);
        	do {
        		FD_ZERO(&wset);
        		FD_SET(storprintf->chatfds[1], &wset);
        		FD_ZERO(&rset);
        		FD_SET(storprintf->wakeupfds[0], &rset);
        		nfds = max(storprintf->chatfds[1], storprintf->wakeupfds[0]);
        		if (storprintf->param.wr_waitms < 0) {
        			ptimeout = NULL;
        		}
        		else {
        			ptimeout = &timeout;
        			ptimeout->tv_sec = storprintf->param.wr_waitms / 1000;
        			ptimeout->tv_usec = (storprintf->param.wr_waitms % 1000) * 1000;
        		}

        		do {
        			ret = select(nfds + 1, &rset, &wset, NULL, ptimeout);
        		} while (-1 == ret && EINTR == errno);
        		if (-1 == ret) {
        			ERR("FAIL to select for write, %s\n", strerror(errno));
        			ret = -1;
        			break;
        		}
        		else if (0 == ret) {
        			DBG("TIMEOUT to write stor printf data\n");
        			ret = 0;
        			break;

        		}

        		if (FD_ISSET(storprintf->wakeupfds[0], &rset)) {
        			WARN("STOR PRINTF wakeup by wakeupfds[0]: %d\n", storprintf->wakeupfds[0]);
        			ret = -1;
        			break;
        		}

        		if (FD_ISSET(storprintf->chatfds[1], &wset)) {
        			memset(storprintf->prtbuff, 0, STOR_PRINTF_PRTBUFF_SIZE);
                    *((unsigned int *)storprintf->prtbuff) = (unsigned int)time(NULL);
                    len = sizeof (unsigned int);
                    if (NULL != tag && '\0' != tag[0]) {
                        len += snprintf(storprintf->prtbuff  + len, STOR_PRINTF_PRTBUFF_SIZE - len, "[%s]%s,", tag, strtime(time(NULL), szTime, sizeof (szTime)));
                    }
                    ret = vsnprintf(storprintf->prtbuff + len, STOR_PRINTF_PRTBUFF_SIZE - len, format, args);
                    if (-1 == ret) {
                        len = STOR_PRINTF_PRTBUFF_SIZE;
                    }
                    else {
                        len += ret;
                        len = min(STOR_PRINTF_PRTBUFF_SIZE, len);
                    }
        			do {
        				ret = write(storprintf->chatfds[1], storprintf->prtbuff, len);
        			} while (-1 == ret && EINTR == errno);
        			if (-1 == ret) {
						ERR("FAIL to write stor printf data, (%d)%s\n", errno, strerror(errno));
    					break;   				
        			}
        		}
        	} while (0);
        	va_end(args);
        }
        pthread_mutex_unlock(&storprintf->chat_mutex);
    }

exit:
    
    STORPRINTF_REFCOUNT_DEC(storprintf);

    return ret;

}

/**@fn          STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err)
 * @brief       开启一个stor_printf搜索实例, 该接口会返回一个搜索句柄, 用户循环调用stor_printf_search_next接口每次获取一个搜索结果
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const struct stor_printf_search_param_t *param    - 搜索参数, 包含要搜索的标签, 以及搜索时间范围
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      != NULL: 搜索实例句柄, NULL: 表示开启搜索失败, 通过输出参数err获取具体的错误号
 * @note
 */
STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    struct stor_printf_search_t *storprintf_se = NULL;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return NULL;
    }
    if (NULL == param) {
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return NULL;
    }

    STORPRINTF_REFCOUNT_INC_EX(storprintf, id, err, NULL);
    {
        e = _stor_printf_search_start(storprintf, param, &storprintf_se);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);

    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? (STOR_PRINTF_SE_HANDLE)storprintf_se : NULL;

}

/**@fn          int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的用户数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  void *buff    - 指向存放搜索结果缓存的指针
 * @param[in]   unsigned int size    - 存放搜索结果缓存的最大尺寸
 * @param[in]   int waitms    - 单次检索等待超时时间, 单位: 毫秒, -1 表示一直等待, 0 表示不等待,没有数据则立即返回
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      > 0: 表示搜索的数据实际长度, 0: 表示检索超时, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf = NULL;
    struct stor_printf_search_t *storprintf_se = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    unsigned int len = 0;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
    if (NULL == h || NULL == buff || 0 == size) {
        ERR("INVALID input param, h = %p, buff = %p, size = %u\n", h, buff, size);
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return -1;
    }
    storprintf_se = (struct stor_printf_search_t *)h;

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        e = _stor_printf_search_next(storprintf, storprintf_se, buff, size, &len, waitms);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);
    
    STOR_PRINTF_ERRNO_SET(err, e);

    if (STOR_PRINTF_TIMEOUT == e) {
        return 0;
    }

    return (STOR_PRINTF_SUCC == e) ? (int)len : -1;

}

/**@fn          int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       关闭指定id的stor_printf模块的指定搜索句柄h的搜索实例
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄, 为NULL时表示关闭所有搜索实例
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 表示成功, -1: 表示失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
{   
    struct stor_printf_t *storprintf = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        if (NULL == h) {
            e = _stor_printf_search_stop_all(storprintf);
        }
        else {
            e = _stor_printf_search_stop(storprintf, (struct stor_printf_search_t *)h);
        }
    }  
    STORPRINTF_REFCOUNT_DEC(storprintf);
    
    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}


/**@fn          int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的sp文件个数
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      >= 0: 表示sp文件个数, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf = NULL;
    struct stor_printf_search_t *storprintf_se = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    int count = 0;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
    if (NULL == h) {
        ERR("INVALID input param, h = %p\n", h);
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return -1;
    }
    storprintf_se = (struct stor_printf_search_t *)h;

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        count = lstCount(&(storprintf_se->spsearch_lst));
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);
    
    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? count : -1;

}


/**@fn          int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的sp文件信息
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  struct stor_printf_spfile_t *spfile    - 指向sp文件信息结构的指针
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 表示成功, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf = NULL;
    struct stor_printf_search_t *storprintf_se = NULL;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
    if (NULL == h || NULL == spfile) {
        ERR("INVALID input param, h = %p, spfile = %p\n", h, spfile);
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return -1;
    }
    storprintf_se = (struct stor_printf_search_t *)h;

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        e = _stor_printf_spfile_search_next(storprintf, storprintf_se, spfile);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);
    
    STOR_PRINTF_ERRNO_SET(err, e);

    if (STOR_PRINTF_TIMEOUT == e) {
        return 0;
    }

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}


/**@fn          int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id的stor_printf模块中删除指定分区
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *partpath    - 分区或目录路径, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
{
    struct stor_printf_t *storprintf;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
	if (NULL == partpath || '\0' == partpath[0]) {
        STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
        return -1;
    }

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        e = _stor_printf_part_remove(storprintf, partpath);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);

    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}

/**@fn          int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err)
 * @brief       打印指定id的stor_printf模块中所有sp文件信息
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   unsigned int prtmask    - sp文件信息打印掩码, 详见enum STOR_PRINTF_SPFILE_PRTMASK
 * @param[in]   char *prtbuff    - 存放打印的缓存, 该参数为NULL时表示将打印输出到标准输出中
 * @param[in]   unsigned int size    - 存放打印缓存的最大空间大小, 该参数为0时表示将打印输出到标准输出中
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err)
{
	struct stor_printf_t *storprintf;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
	if (0 == prtmask) {
		STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_INVALID_INPUT);
		return -1;
	}

    STORPRINTF_REFCOUNT_INC(storprintf, id, err);
    {
        e = _stor_printf_spfiles_print(storprintf, prtmask, prtbuff, size);
    }
    STORPRINTF_REFCOUNT_DEC(storprintf);

    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}

/**@fn          int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err)
 * @brief       创建一个stor_printf模块实例, 并返回操作id
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err)
{      
    struct stor_printf_t *storprintf;
    enum STOR_PRINTF_ERRNO e = STOR_PRINTF_SUCC;
    int ret = -1;

    e = id_to_stor_printf(id, &storprintf);
    if (STOR_PRINTF_SUCC != e) {
        STOR_PRINTF_ERRNO_SET(err, e);
        return -1;
    }

    pthread_mutex_lock(&storprintf->mutex);
    {
        if (!storprintf->inuse) {
            STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_BAD_ID);
            pthread_mutex_unlock(&storprintf->mutex);
            return -1;
        }
        
        /* 通知所有select任务推出 */
        write(storprintf->wakeupfds[1], "wakeup", strlen("wakeup"));
		/* 通知外部接口从write阻塞中退出 */
		shutdown(storprintf->chatfds[1], SHUT_WR);
		/* 通知主线程从read阻塞中退出 */
		shutdown(storprintf->chatfds[0], SHUT_RD);
        
        /* 等待其他任务退出 */
        while (storprintf->refcount > 0) {
			WARN("storprintf id = %d, refcount = %d, still wait\n", id, storprintf->refcount);
			ret = pthread_cond_wait(&storprintf->cond, &storprintf->mutex);
			if (0 != ret) {
				ERR("FAIL to pthread_cond_wait, %s\n", strerror(ret));
				STOR_PRINTF_ERRNO_SET(err, STOR_PRINTF_PTHREAD_COND_WAIT_FAIL);
				pthread_mutex_unlock(&storprintf->mutex);
				return -1;
			}
		}
        e = stor_printf_fini(storprintf, 1);
    }
    pthread_mutex_unlock(&storprintf->mutex);

    STOR_PRINTF_ERRNO_SET(err, e);

    return (STOR_PRINTF_SUCC == e) ? 0 : -1;

}

/**@fn          const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err)
 * @brief       将指定stor_printf模块的错误号转换成对应字符串的接口
 * @param[in]   enum STOR_PRINTF_ERRNO err    - stor_printf模块错误号
 * @param[out]  void
 * @return      描述stor_printf模块错误号的字符串
 * @note          
 */
const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err)
{
    return strtype(sg_storprintf_errstr_tab, ARRAY_SIZE(sg_storprintf_errstr_tab), (int)err);
    
}

/**@fn          void stor_printf_dbg_set(int enable_dbg)
 * @brief       开启stor_printf模块调试打印信息
 * @param[in]   int enable_dbg    - 是否使能调试打印
 * @param[out]  void
 * @return      void
 * @note          
 */
void stor_printf_dbg_set(int enable_dbg)
{
    sg_enable_storprintf_dbg = enable_dbg;
    return;
    
}

void TEST_stor_printf_search(int storprt_id)
{
    struct stor_printf_search_param_t search_param;
    struct tm start_tm;
    struct tm end_tm;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    STOR_PRINTF_SE_HANDLE h_se = NULL;
    char buff[1024] = {0};
    int ret = -1;

    if (-1 == storprt_id) {
        return;
    }

    memset((char *)&search_param, 0, sizeof (search_param));
    SAFE_STRNCPY(search_param.tags, "", sizeof (search_param.tags));
    memset((char *)&start_tm, 0, sizeof (start_tm));
    start_tm.tm_year = 2019 - 1900;
    start_tm.tm_mon = 12 - 1;
    start_tm.tm_mday = 27;
    start_tm.tm_hour = 0;
    start_tm.tm_min = 6;
    start_tm.tm_sec = 0;
 
    memset((char *)&end_tm, 0, sizeof (end_tm));
    end_tm.tm_year = 2019 - 1900;
    end_tm.tm_mon = 12 - 1;
    end_tm.tm_mday = 27;
    end_tm.tm_hour = 23;
    end_tm.tm_min = 59;
    end_tm.tm_sec = 0;
    
    search_param.starttime = myself_mktime(&start_tm);
    search_param.endtime = myself_mktime(&end_tm);
    
    h_se = stor_printf_search_start(storprt_id, &search_param, &err);
    if (NULL == h_se) {
        ERR("FAIL to start stor printf search, %s\n", stor_printf_strerr(err));
        return;
    }

    do {
        ret = stor_printf_search_next(storprt_id, h_se, buff, sizeof (buff), 3000, &err);
        if (-1 == ret) {
            ERR("FAIL to search next, storprt_id = %d, h_se = %p, %s\n", storprt_id, h_se, stor_printf_strerr(err));
            goto exit;
        }
        else if (0 == ret) {
		    /* WARNING 注意返回0表示暂时搜索不到，需要延时下，处理该异常 */
            WARN("searching...\n");
            continue;
        }
        INFO("+++ %s\n", buff);
    } while (1);


exit:

    if (NULL != h_se) {
        if (-1 == stor_printf_search_stop(storprt_id, h_se, &err)) {
            ERR("FAIL to stop stor printf search, storprt_id = %d, h_se = %p, %s\n", storprt_id, h_se, stor_printf_strerr(err));
        }
        h_se = NULL;
    }
    
    return;
    
}

#ifndef __APPLE__
void TEST_stor_printf_part_leftspace_get(void)
{
    unsigned long long leftspace = 0llu;
    char strleftspace[32] = {0};
    char mntpath[128] = {0};
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    err = stor_printf_part_leftspace_get("test", &leftspace, mntpath, sizeof (mntpath));
    if (STOR_PRINTF_SUCC != err) {
        ERR("FAIL to get stor printf leftspace, %s\n", stor_printf_strerr(err));
        return;
    }

    INFO("SUCC to get left space, %s, %s\n", stor_printf_strsize(leftspace, strleftspace, sizeof (strleftspace)), mntpath);

    return;
    
}
#endif

#ifdef __cplusplus
}
#endif

