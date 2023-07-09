
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common_utils.h"
#include "stor_printf.h"

extern unsigned int myself_mktime(struct tm *tm);
extern void TEST_stor_printf_part_leftspace_get(void);

static int TEST_stor_printf_open(void)
{
    struct stor_printf_param_t param;
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;
    int storprt_id = -1;

    /* 打开stor_printf实例 */
    memset((char *)&param, 0, sizeof (param));
    SAFE_STRNCPY(param.filename, "test", sizeof (param.filename));
    param.sp_count = 5;
    param.sp_datasize = 1 * 1024 * 1024;
    param.sp_prealloc = 0;
    param.wr_waitms = 3000;
    param.sp_head_sync_inter = 30;
    param.diskpart_min_keepsize = 32 * 1024 * 1024;
    storprt_id = stor_printf_open(&param, &err);
    if (-1 == storprt_id) {
        printf("FAIL to open stor_printf, %s\n", stor_printf_strerr(err));
        return -1;
    }

    return storprt_id;
    
}

static void TEST_stor_printf_parts_add(int storprt_id)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (-1 == stor_printf_part_add(storprt_id, "spfile", &err)) {
        printf("FAIL to add part spfile, %s\n", stor_printf_strerr(err));
    }
    
#if 0	
    if (-1 == stor_printf_part_add(storprt_id, "test42/test2_1", &err)) {
        printf("FAIL to add part test2/test2_1, %s\n", stor_printf_strerr(err));
    }
#endif    
	
    return;
    
}

static void * TEST_stor_printf_thread(void *arg)
{
    int id = -1;
    int i = 0;
	int ret = -1;
    pthread_t tid = (pthread_t)-1;

    if (NULL == arg) {
        return NULL;
    }

	id = *((int *)arg);

    if (-1 == id) {
        return NULL;
    }

    tid = pthread_self();

	//printf("THREAD.tid = %u, id = %d\n", (unsigned int)tid, id);

    for (; ;) {
        ret = stor_printf(id, "THREAD", "tid = %llu, stor_printf test %d\n", (unsigned long long)tid, i++);
        if (-1 == ret) {
			break;
        }
        usleep(500 * 1000);
    }

    printf("THREAD tid: %llu exit\n", (unsigned long long)tid);

    return NULL;
    
}

static inline void TEST_stor_printf_writer_create(int *storprt_id, pthread_t *writer_tid)
{
    pthread_create(writer_tid, NULL, TEST_stor_printf_thread, storprt_id);
    
    return;
    
}

static inline void TEST_stor_printf_parts_remove(int storprt_id)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (-1 == stor_printf_part_remove(storprt_id, "test2/test2_1", &err)) {
        printf("FAIL to remove part test2/test2_1, %s\n", stor_printf_strerr(err));
    }

	if (-1 == stor_printf_part_remove(storprt_id, "test3", &err)) {
        printf("FAIL to remove part test3, %s\n", stor_printf_strerr(err));
    }
    
    return;
    
}

static inline void TEST_stor_printf_close(int storprt_id)
{
    enum STOR_PRINTF_ERRNO err = STOR_PRINTF_SUCC;

    if (-1 == stor_printf_close(storprt_id, &err)) {
        printf("FAIL to close stor printf, id = %d, %s\n", storprt_id, stor_printf_strerr(err));
    }
    
    return;
    
}


#if 0
int main(int argc, char *argv[])
{
    int storprt_id = -1;
    int i;
    pthread_t thread_tids[50] = {(pthread_t)-1};

    stor_printf_dbg_set(1);

    storprt_id = TEST_stor_printf_open();
    if (-1 == storprt_id) {
        return -1;
    }

    /* 添加存储器分区 */
    TEST_stor_printf_parts_add(storprt_id);
    

    /* 多线程任意调用stor_printf接口，类似与printf的格式 */
    for (i = 0; i < ARRAY_SIZE(thread_tids); i++) {
        TEST_stor_printf_writer_create(&storprt_id, &(thread_tids[i]));
    }

    sleep(60 * 10);

    /* 任意线程检索指定目标, 指定时间内的日志 */    
    //TEST_stor_printf_search(storprt_id);

    /* 从stor_printf实例中删除指定存储资源 */
    TEST_stor_printf_parts_remove(storprt_id);
    

    /* 关闭stor_printf实例 */
    TEST_stor_printf_close(storprt_id);

    for (i = 0; i < ARRAY_SIZE(thread_tids); i++) {
        pthread_join(thread_tids[i], NULL);
    }

    return 0;
    
}

#endif