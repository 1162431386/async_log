#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>
#include "async_log.h"
#include "stor_printf.h"


#define TRUE 1
#define FALSE 0
/*
 *@brief 安全的文件关闭
 */
#ifndef SAFE_CLOSE
#define SAFE_CLOSE(x) do { \
    if (-1 != (x)) \
    { \
        close((x)); \
        (x) = -1; \
    } \
}while (0)
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(x) do { \
        if (NULL != (x)) \
        { \
            free((x)); \
            (x) = NULL; \
        } \
    }while(0)
#endif

#ifndef SAFE_STRNCPY
#define SAFE_STRNCPY(dst, src, n) do { \
    memset((char *)(dst), 0, (n)); \
    strncpy((dst), (src), n); \
    (dst)[n - 1] = '\0'; \
}while (0)
#endif




#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m\n"

#define gettid() syscall(__NR_gettid)


typedef struct 
{
    pthread_mutex_t log_mutex;
    volatile  int  _level;
    volatile  int  _PthreadName;
    int iFifoId;
    int iStorPrtId;
    int bUsed;
}logger_t;


logger_t  g_logger_t = {
    .log_mutex = PTHREAD_MUTEX_INITIALIZER,
    ._level = 0,   /*默认全部开启*/
    ._PthreadName = 0, /*默认关闭线程名打印*/
    .iFifoId = -1,
    .iStorPrtId = -1,
    .bUsed = FALSE, 

};

typedef struct 
{
    uint32_t year;
    uint32_t  month;
    uint32_t  day;
    uint32_t  hour;
    uint32_t  minute;
    uint32_t  second;
    uint32_t  millisecond;
}StDateTime;


static const char* g_level_label[][2] =
{
    {"trace",     "TRACE"},
    {"debug",     "DEBUG"},  
    {"info",      "INFO" },
    {"warn",      "WARN" },
    {"err",       "ERROR"},
    {"critical",  "CRITI"},
};

static const char * g_color_label[][2] =
{
    {ANSI_COLOR_RESET, ANSI_COLOR_RESET},
    {ANSI_COLOR_YELLOW, ANSI_COLOR_RESET},
    {ANSI_COLOR_GREEN, ANSI_COLOR_RESET},
    {ANSI_COLOR_BLUE, ANSI_COLOR_RESET},
    {ANSI_COLOR_RED, ANSI_COLOR_RESET},
    {ANSI_COLOR_CYAN, ANSI_COLOR_RESET},
};






static void getDateTime(StDateTime *time)
{
    struct timeval tv;
    struct tm *now;

    (void)gettimeofday(&tv, NULL);
    now = localtime(&(tv.tv_sec));
    time->year = (uint32_t)(now->tm_year + 1900);
    time->month = (uint32_t)(now->tm_mon + 1);
    time->day = (uint32_t)now->tm_mday;
    time->hour = (uint32_t)now->tm_hour;
    time->minute = (uint32_t)now->tm_min;
    time->second = (uint32_t)now->tm_sec;
    time->millisecond = (uint32_t)(tv.tv_usec / 1000);
}


static int get_thread_name(char *pthread_name) 
{
    pthread_t thread = pthread_self();
    char thread_name[16]; 
    pthread_getname_np(thread, thread_name, sizeof(thread_name));
    strncpy(pthread_name, thread_name, sizeof(thread_name));
    return 0;
}



void  setLogLevel(eLoggerLevel level)
{
    pthread_mutex_lock(&g_logger_t.log_mutex);
    g_logger_t._level = level;
    pthread_mutex_unlock(&g_logger_t.log_mutex);
}

static eLoggerLevel getLogLevel()
{
    return g_logger_t._level;
}




int asynclog(eLoggerLevel level,  const char *format, ...)
{
    if(getLogLevel() <= level)
    {
        logger_t *p = &g_logger_t;
        va_list args;
        char buffer[1024*4] = {0};
        StDateTime time_t;
        memset(&time_t, 0, sizeof(time_t));
        getDateTime(&time_t);

        sprintf(buffer, "%s[%04u/%02u/%02u %02u:%02u:%02u:%03u] [%s] [%ld] ", g_color_label[level][0],
                time_t.year, time_t.month, time_t.day, time_t.hour, time_t.minute, time_t.second, time_t.millisecond,
                g_level_label[level][1], gettid());

        va_start (args, format);
        vsnprintf(buffer + strlen(buffer), 1024*4 - strlen(buffer), format, args);
        va_end (args);
        sprintf(buffer + strlen(buffer), "%s", g_color_label[level][1]);
        printf("%s",buffer);
        if (-1 != p->iStorPrtId) {
            stor_printf(p->iStorPrtId, "PRT", "%s", buffer);
        }        
    }
}

int asynclog_pthreadName(eLoggerLevel level,  const char *format, ...)
{
    if(getLogLevel() <= level)
    {
        char ptrName[16] = {0};
        logger_t *p = &g_logger_t;
        va_list args;
        char buffer[1024*4] = {0};
        StDateTime time_t;
        memset(&time_t, 0, sizeof(time_t));
        getDateTime(&time_t);
        get_thread_name(ptrName);
        sprintf(buffer, "%s(%s)[%04u/%02u/%02u %02u:%02u:%02u:%03u] [%s] [%ld] ", g_color_label[level][0], ptrName,
                time_t.year, time_t.month, time_t.day, time_t.hour, time_t.minute, time_t.second, time_t.millisecond,
                g_level_label[level][1], gettid());

        va_start (args, format);
        vsnprintf(buffer + strlen(buffer), 1024*4 - strlen(buffer), format, args);
        va_end (args);
        sprintf(buffer + strlen(buffer), "%s", g_color_label[level][1]);
        printf("%s",buffer);
        if (-1 != p->iStorPrtId) {
            stor_printf(p->iStorPrtId, "PRT", "%s", buffer);
        }        
    }
}

int log_stor_printf_part_add(const char *pszPartPath)
{
    logger_t *p = &g_logger_t;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;

    if (NULL == pszPartPath || '\0' == pszPartPath[0]) {
        return -1;
    }
    if (-1 == p->iStorPrtId) {
        return -1;
    }

    if (-1 == stor_printf_part_add(p->iStorPrtId, pszPartPath, &errNo)) {
        LOGE("FAIL to add stor print part: %s, iStorPrtId = %d, %s\n", pszPartPath, p->iStorPrtId, stor_printf_strerr(errNo));
        return -1;
    }
    return 0;
}



int log_stor_printf_init(const char *fileName)
{
    int iStorPrtId = 0;
    struct stor_printf_param_t struStorPrtParam;
    enum STOR_PRINTF_ERRNO storPrtErr = STOR_PRINTF_SUCC;
    memset((char *)&struStorPrtParam, 0, sizeof (struStorPrtParam));
    SAFE_STRNCPY(struStorPrtParam.filename, fileName, sizeof (struStorPrtParam.filename));
    struStorPrtParam.sp_count = 8;
    struStorPrtParam.sp_datasize = 64 * 1024 * 1024;
    struStorPrtParam.sp_prealloc = 0;
    struStorPrtParam.sp_head_sync_inter = 30;
    struStorPrtParam.wr_waitms = 3000;
    struStorPrtParam.diskpart_min_keepsize = -1;
    iStorPrtId = stor_printf_open(&struStorPrtParam, &storPrtErr);
    if (-1 == iStorPrtId) {
        LOGE("FAIL to open stor printf instance, %s\n", stor_printf_strerr(storPrtErr));
        return -1;
    }
    return iStorPrtId;
}



int log_stor_init()
{
    logger_t *p = &g_logger_t;

    /*暂时只支持单个实例*/
    if(p->bUsed)
    {
        LOGI("stor log Ready!\n");
        return -1;
    }
    /*初始化日志存储模块*/
    p->iStorPrtId = log_stor_printf_init("stor_log");
    if(-1 == p->iStorPrtId)
    {
        LOGE("FAIL to init stor printf\n");
        goto errExit;
    }
    
    /*添加默认路径 使用者可以在别的地方随处添加*/
    if(log_stor_printf_part_add("stor_log"))
    {
        LOGE("stor_printf_part_add failed!\n");
        goto errExit;
    }

    p->bUsed = TRUE;
    return 0;

errExit:
    if (-1 != p->iStorPrtId) {
        stor_printf_close(p->iStorPrtId, NULL);
        p->iStorPrtId = -1;
    }
    return -1;
}