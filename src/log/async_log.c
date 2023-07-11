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
#include <semaphore.h>
#include <regex.h>
#include "async_log.h"
#include "stor_printf.h"
#include "ftp_printf.h"
#include "common_utils.h"



#define TRUE 1
#define FALSE 0

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

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
    pthread_mutex_t logMutex;
    volatile  int  _level;
    volatile  int  _showPthreadName;
    int iFifoId;
    int iStorPrtId;
    int bUsed;
}logger_t;

typedef struct
{
    time_t tStart;
    time_t tEnd;
    char pszFtpURL[256];
    BOOL bUploadSp;
    BOOL bCompress;
    sem_t *pstruSyncSem;
}ftp_upload_t;


logger_t  g_logger_t = {
    .logMutex = PTHREAD_MUTEX_INITIALIZER,
    ._level = 0,   /*默认全部开启*/
    ._showPthreadName = 0, /*默认关闭线程名打印*/
    .iFifoId = -1,
    .iStorPrtId = -1,
    .bUsed = FALSE, 

};

typedef struct {
    char ip[32];
    unsigned short port;
    char user[32];
    char pass[32];
    char dir[32];
}FTP_PARAM_T;

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


typedef struct {
    const char *regex;
    int (*parse)(const char *ftpurl, FTP_PARAM_T *ftp);
}FTP_URL_PARSER_T;


static const char* g_level_label[][2] =
{
    {"trace",     "TRACE"},
    {"debug",     "DEBUG"},  
    {"info",      "LOGI" },
    {"warn",      "LOGW" },
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



#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)
#define YEAR   (365*DAY)
static int month[12] = 
{   //－－每月初所经过的秒数
    0,
    DAY*(31),
    DAY*(31+29),
    DAY*(31+29+31),
    DAY*(31+29+31+30),
    DAY*(31+29+31+30+31),
    DAY*(31+29+31+30+31+30),
    DAY*(31+29+31+30+31+30+31),
    DAY*(31+29+31+30+31+30+31+31),
    DAY*(31+29+31+30+31+30+31+31+30),
    DAY*(31+29+31+30+31+30+31+31+30+31),
    DAY*(31+29+31+30+31+30+31+31+30+31+30)
};


unsigned int self_mktime(struct tm *tm)
{
    unsigned int res;
    int year;
    year = tm->tm_year - 70;                       //－－年数
    /* magic offsets (y+1) needed to get leapyears right.*/
    res = YEAR * year + DAY * ((year+1)/4);       //－－年数＋闰年数
    res += month[tm->tm_mon];                      //－－月
    /* and (y+2) here. If it wasn't a leap-year, we have to adjust */
    if ((tm->tm_mon > 1) && ((year+2) % 4))      //－－若(y+2)不是闰年，则减一天
    {
        res -= DAY;
    }
    res += DAY * (tm->tm_mday-1);                  //－－当月已经过的天数
    res += HOUR * tm->tm_hour;                     //－－当天已经过的小时数
    res += MINUTE * tm->tm_min;                    //－－此时已经过的分钟
    res += tm->tm_sec;                             //－－此分已经过的秒
    return res;
}


time_t time_str_to_time_t(const char *str)
{
    struct tm tmp_tm;

    if (NULL == str || '\0' == str[0]) {
        return 0;
    }

    /*eg: 20190808_190413, => unsigned int */
    memset((char *)&tmp_tm, 0, sizeof (tmp_tm));
    sscanf(str, "%02d%02d%02d_%02d%02d%02d", 
        &tmp_tm.tm_year, &tmp_tm.tm_mon, &tmp_tm.tm_mday, &tmp_tm.tm_hour, &tmp_tm.tm_min, &tmp_tm.tm_sec);
    tmp_tm.tm_year += 2000;
    tmp_tm.tm_year -= 1900;
    tmp_tm.tm_mon -= 1;

    return self_mktime(&tmp_tm);

}




static void get_date_time(StDateTime *time)
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
    pthread_mutex_lock(&g_logger_t.logMutex);
    g_logger_t._level = level;
    pthread_mutex_unlock(&g_logger_t.logMutex);
}

static eLoggerLevel getLogLevel()
{
    return g_logger_t._level;
}


void set_log_show_thread_name(int isShowThreadName)
{
    pthread_mutex_lock(&g_logger_t.logMutex);
    g_logger_t._showPthreadName = isShowThreadName;
    pthread_mutex_unlock(&g_logger_t.logMutex);
}


static int get_log_show_thread_name(void)
{
    return g_logger_t._showPthreadName; 
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
        get_date_time(&time_t);

        if(get_log_show_thread_name())
        {
            char ptrName[16] = {0};
            get_thread_name(ptrName);
            sprintf(buffer, "%s(%s)[%04u/%02u/%02u %02u:%02u:%02u:%03u] [%s] [%ld] ", g_color_label[level][0], ptrName,
                time_t.year, time_t.month, time_t.day, time_t.hour, time_t.minute, time_t.second, time_t.millisecond,
                g_level_label[level][1], gettid());
        }
        else
        {
            sprintf(buffer, "%s[%04u/%02u/%02u %02u:%02u:%02u:%03u] [%s] [%ld] ", g_color_label[level][0],
                time_t.year, time_t.month, time_t.day, time_t.hour, time_t.minute, time_t.second, time_t.millisecond,
                g_level_label[level][1], gettid());
        }
        va_start (args, format);
        vsnprintf(buffer + strlen(buffer), 1024*4 - strlen(buffer), format, args);
        va_end (args);
        sprintf(buffer + strlen(buffer), "%s", g_color_label[level][1]);
        fprintf(stdout, buffer);
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


/* S1: ftpurl=ftp://user:pass@ip 使用默认端口, ftp根目录处理 */
static int ftp_url_s1_parse(const char *ftpurl, FTP_PARAM_T *ftp)
{
    sscanf(ftpurl, "ftp://%[^:]:%[^@]@%s", ftp->user, ftp->pass, ftp->ip);
    ftp->port = 21;
    memset(ftp->dir, 0, sizeof(ftp->dir));

    return 0;

}

/* S2: ftpurl=ftp://user:pass@ip:port ftp根目录处理 */
static int ftp_url_s2_parse(const char *ftpurl, FTP_PARAM_T *ftp)
{
    int port = 21;

    sscanf(ftpurl, "ftp://%[^:]:%[^@]@%[^:]:%d", ftp->user, ftp->pass, ftp->ip, &port);
    ftp->port = (unsigned short)port;
    memset(ftp->dir, 0, sizeof(ftp->dir));

    return 0;

}

/* S3: ftpurl=ftp://user:pass@ip/dir 使用默认端口处理 */
static int ftp_url_s3_parse(const char *ftpurl, FTP_PARAM_T *ftp)
{
    sscanf(ftpurl, "ftp://%[^:]:%[^@]@%[^/]/%s", ftp->user, ftp->pass, ftp->ip, ftp->dir);
    ftp->port = 21;

    return 0;

}

/* S4: ftpurl=ftp://user:pass@ip:port/dir */
static int ftp_url_s4_parse(const char *ftpurl, FTP_PARAM_T *ftp)
{
    int port = 21;

    sscanf(ftpurl, "ftp://%[^:]:%[^@]@%[^:]:%d/%s", ftp->user, ftp->pass, ftp->ip, &port, ftp->dir);
    ftp->port = (unsigned short)port;

    return 0;

}

static FTP_URL_PARSER_T sg_ftpurl_parser_tab[] = {
    {"ftp:\\/\\/.*:.*@[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$", ftp_url_s1_parse},
    {"ftp:\\/\\/.*:.*@[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+:[0-9]+$", ftp_url_s2_parse},
    {"ftp:\\/\\/.*:.*@[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\/.*$", ftp_url_s3_parse},
    {"ftp:\\/\\/.*:.*@[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+:[0-9]+\\/.*$", ftp_url_s4_parse},
};

extern int is_regex_match(const char *str, const char *pattern, int cflags);
int log_ftp_url_parse(const char *ftpurl, FTP_PARAM_T *param)
{
    int i = 0;

    if (NULL == ftpurl || '\0' == ftpurl[0] || NULL == param) {
        return -1;
    }

    /* FTP参数解析 */
    for (i = 0; i < ARRAY_SIZE(sg_ftpurl_parser_tab); i++) {
        if (NULL == sg_ftpurl_parser_tab[i].regex || '\0' == sg_ftpurl_parser_tab[i].regex[0]) {
            continue;
        }

        if (is_regex_match(ftpurl, sg_ftpurl_parser_tab[i].regex, REG_EXTENDED | REG_ICASE)) {
            if (NULL == sg_ftpurl_parser_tab[i].parse) {
                LOGE("NO parse func specified, regex: \"%s\"\n", sg_ftpurl_parser_tab[i].regex);
                return -1;
            }

            return sg_ftpurl_parser_tab[i].parse(ftpurl, param);
        }
    }

    return -1;

}


static inline int app_stor_printf_ftpprt_open(time_t tStart, time_t tEnd, const char *pszFtpURL, BOOL bUploadSp)
{
    FTP_PARAM_T struFtpParam;
    char szStartTime[32] = {0};
    char szEndTime[32] = {0};
    struct ftp_printf_param_t struFtpPrtParam;
    enum ftp_printf_errno ftpErr = FTP_PRINTF_SUCC;
    int iFtpPrtId = -1;

    memset((char *)&struFtpParam, 0, sizeof (struFtpParam));
    if (0 == log_ftp_url_parse(pszFtpURL, &struFtpParam)) {
        memset((char *)&struFtpPrtParam, 0, sizeof (struFtpPrtParam));
        SAFE_STRNCPY(struFtpPrtParam.addr, struFtpParam.ip, sizeof (struFtpPrtParam.addr));
        struFtpPrtParam.port = struFtpParam.port;
        SAFE_STRNCPY(struFtpPrtParam.user, struFtpParam.user, sizeof (struFtpPrtParam.user));
        SAFE_STRNCPY(struFtpPrtParam.pass, struFtpParam.pass, sizeof (struFtpPrtParam.pass));
        SAFE_STRNCPY(struFtpPrtParam.dir, struFtpParam.dir, sizeof (struFtpPrtParam.dir));
        snprintf(struFtpPrtParam.filename, sizeof (struFtpPrtParam.filename), "%s_%s-%s_%u.%s", bUploadSp ? "STORPRT" : "storprt", strtime(tStart, szStartTime, sizeof (szStartTime)), strtime(tEnd, szEndTime, sizeof (szEndTime)), (unsigned int)time(NULL), "log");
        struFtpPrtParam.multifile = 0;
        struFtpPrtParam.wr_waitms = 6000;
        struFtpPrtParam.rd_waitms = 6000;
        struFtpPrtParam.sync = 1;
        struFtpPrtParam.sync_waitms = 16000;
        iFtpPrtId = ftp_printf_open(&struFtpPrtParam, &ftpErr);
        if (-1 == iFtpPrtId) {
            LOGW("FAIL to open ftp printf instance, %s\n", ftp_printf_strerr(ftpErr));
        }
        else {
            ftp_printf(iFtpPrtId, "stor_printf search: %s => %s\n", strtime(tStart, szStartTime, sizeof (szStartTime)), strtime(tEnd, szEndTime, sizeof (szEndTime)));
            ftp_printf(iFtpPrtId, "          filename: %s\n", struFtpPrtParam.filename);
        }
        return iFtpPrtId;
    }

    return -1;

}


static inline int app_stor_printf_search_spfiles_print(int iStorPrtId, STOR_PRINTF_SE_HANDLE hSpSe, int iFtpPrtId)
{
    struct stor_printf_spfile_t struSpFile;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;
    int iRet = -1;
    char szStartTime[32] = {0};
    char szEndTime[32] = {0};

    do {
        iRet = stor_printf_search_spfile_next(iStorPrtId, hSpSe, &struSpFile, &errNo);
        if (-1 == iRet) {
            if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                break;
            }
            if (-1 == iFtpPrtId) {
                LOGE("FAIL to spfile search next, iStorPrtId = %d, hSpSe = %p, %s\n", iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to spfile search next, iStorPrtId = %d, hSpSe = %p, %s\n", iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            return -1;
        }

        if (-1 == iFtpPrtId) {
            LOGD("+++ SP.path: %s\n", struSpFile.path);
            LOGD("+++ SP.data_size: %u\n", struSpFile.data_size);
            LOGD("+++ SP.data_offset: %u\n", struSpFile.data_offset);
            LOGD("+++ SP.start_time: %s\n", strtime((time_t)struSpFile.start_time, szStartTime, sizeof (szStartTime)));
            LOGD("+++ SP.end_time: %s\n\n", strtime((time_t)struSpFile.end_time, szEndTime, sizeof (szEndTime)));
        }
        else {
            ftp_printf(iFtpPrtId, "+++ SP.path: %s\n", struSpFile.path);
            ftp_printf(iFtpPrtId, "+++ SP.data_size: %u\n", struSpFile.data_size);
            ftp_printf(iFtpPrtId, "+++ SP.data_offset: %u\n", struSpFile.data_offset);
            ftp_printf(iFtpPrtId, "+++ SP.start_time: %s\n", strtime((time_t)struSpFile.start_time, szStartTime, sizeof (szStartTime)));
            ftp_printf(iFtpPrtId, "+++ SP.end_time: %s\n\n", strtime((time_t)struSpFile.end_time, szEndTime, sizeof (szEndTime)));
        }
    } while (1);

    return 0;

}



static inline int app_stor_printf_all_spfiles_print(int iStorPrtId, STOR_PRINTF_SE_HANDLE hSpSe, int iFtpPrtId)
{
    char szBuff[1024] = {0};
    char *pszPrtBuff = NULL;
    int i = 0;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;

    pszPrtBuff = (char *)malloc(1024 * 8);
    if (NULL == pszPrtBuff) {
        if (-1 == iFtpPrtId) {
            LOGE("OUT OF MEMORY!!!\n");
        }
        else {
            ftp_printf(iFtpPrtId, "OUT OF MEMORY!!!\n");
        }
        return -1;
    }

    memset(pszPrtBuff, 0, 1024 * 8);
    if (-1 == stor_printf_spfiles_print(iStorPrtId, SPFILE_PRTMASK_EMPTY | SPFILE_PRTMASK_INUSE | SPFILE_PRTMASK_FULL | SPFILE_PRTMASK_OLDEST | SPFILE_PRTMASK_NEWEST, pszPrtBuff, 1024 * 8, &errNo)) {
        SAFE_FREE(pszPrtBuff);
        return -1;
    }

    for (i = 0; i < ((1024 * 8) / sizeof (szBuff)); i++) {
        memcpy(szBuff, pszPrtBuff + (i * sizeof (szBuff)), sizeof (szBuff));
        if (-1 == iFtpPrtId) {
            LOGI("%s\n", szBuff);
        }
        else {
            ftp_printf(iFtpPrtId, "%s\n", szBuff);
        }
    }
    SAFE_FREE(pszPrtBuff);

    return 0;

}

void  *app_stor_printf_search(void *arg)
{
    logger_t *p = &g_logger_t;
    if(NULL == arg)
    {
        LOGE("INVALID ftp_upParam\n");
        return NULL;
    }
    ftp_upload_t *pftp_upload_t = (ftp_upload_t *)arg;
    const char *_pszFtpURL = NULL;
    enum ftp_printf_errno ftpErr = FTP_PRINTF_SUCC;
    int iFtpPrtId = -1;
    STOR_PRINTF_SE_HANDLE hSpSe = NULL;
    struct stor_printf_spfile_t struSpFile;
    STOR_PRINTF_SE_HANDLE hSe = NULL;
    struct stor_printf_search_param_t struSeParam;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;
    int iRet = -1;
    char szBuff[1024] = {0};

    if (NULL != pftp_upload_t->pszFtpURL) {
        _pszFtpURL = strdup(pftp_upload_t->pszFtpURL);
    }
    if (NULL !=  pftp_upload_t->pstruSyncSem) {
        sem_post(pftp_upload_t->pstruSyncSem);
    }

    if (0 == pftp_upload_t->tStart || 0 == pftp_upload_t->tEnd || pftp_upload_t->tStart > pftp_upload_t->tEnd) {
        LOGE("INVALID time range, tStart: %u, tEnd: %u\n", (unsigned int)pftp_upload_t->tStart, (unsigned int)pftp_upload_t->tEnd);
        return NULL;
    }
    if (!p->bUsed || -1 == p->iStorPrtId) {
        LOGE("p->bInit = %d, p->iStorPrtId = %d\n", p->bUsed, p->iStorPrtId);
        return NULL;
    }

    if (NULL != _pszFtpURL && '\0' != _pszFtpURL[0]) {
        iFtpPrtId = app_stor_printf_ftpprt_open(pftp_upload_t->tStart, pftp_upload_t->tEnd, _pszFtpURL, pftp_upload_t->bUploadSp);
    }
    
    memset((char *)&struSeParam, 0, sizeof (struSeParam));
    struSeParam.starttime = pftp_upload_t->tStart;
    struSeParam.endtime = pftp_upload_t->tEnd;
      
    if (1)
    {
        /* 开启检索 */
        hSpSe = stor_printf_search_start(p->iStorPrtId, &struSeParam, &errNo);
        if (NULL == hSpSe) {
            if (-1 == iFtpPrtId) {
                LOGE("FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to start stor printf spfiles search, %s\n", stor_printf_strerr(errNo));
            }
            iRet = -1;
            goto exit;
        }

        /* 检索处理 */
        if (stor_printf_search_spfile_count(p->iStorPrtId, hSpSe, NULL) > 0) {
            if (-1 == app_stor_printf_search_spfiles_print(p->iStorPrtId, hSpSe, iFtpPrtId)) {
                iRet = -1;
                goto exit;
            }
        }
        else {
            app_stor_printf_all_spfiles_print(p->iStorPrtId, hSpSe, iFtpPrtId);
        }
        
        /* 关闭检索 */
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSpSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                LOGW("FAIL to stop spfile stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop spfile stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
        }
        hSpSe = NULL;
    }

    if (-1 == iFtpPrtId && '\0' != _pszFtpURL[0]) {
        iRet = -1;
        goto exit;
    }
    if (2)
    {
        hSe = stor_printf_search_start(p->iStorPrtId, &struSeParam, &errNo);
        if (NULL == hSe) {
            if (-1 == iFtpPrtId) {
                LOGE("FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            iRet = -1;
            goto exit;
        }

        memset(szBuff, 0, sizeof (szBuff));

        if (pftp_upload_t->bUploadSp) {
            if (-1 != iFtpPrtId) {
                do {
                    iRet = stor_printf_search_spfile_next(p->iStorPrtId, hSe, &struSpFile, &errNo);
                    if (-1 == iRet) {
                        if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                            break;
                        }
                        ftp_printf(iFtpPrtId, "FAIL to spfile search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                        goto exit;
                    }    
                   
                    if (-1 == ftp_printf_file_upload(iFtpPrtId, struSpFile.path, &ftpErr)) 
                    {
                        LOGE("FAIL to upload sp file: %s => ftp, %s\n", struSpFile.path, ftp_printf_strerr(ftpErr));
                        goto exit;
                    }
                    
                } while (1);
            }
        } 
        else
        {
            do {
                iRet = stor_printf_search_next(p->iStorPrtId, hSe, szBuff, sizeof (szBuff), 3000, &errNo);
                if (-1 == iRet) {
                    if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                        if (-1 == iFtpPrtId) {
                            LOGI("search complete!\n");
                        }
                        else {
                            ftp_printf(iFtpPrtId, "search complete!\n");
                        }
                        break;
                    }
                    if (-1 == iFtpPrtId) {
                        LOGE("FAIL to search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                    }
                    else {
                        ftp_printf(iFtpPrtId, "FAIL to search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                    }
                    goto exit;
                }
                else if (0 == iRet) {
                    if (-1 == iFtpPrtId) {
                        LOGW("searching...\n");
                    }
                    else {
                        ftp_printf(iFtpPrtId, "searching...\n");
                    }
                    continue;
                }
                if (-1 == iFtpPrtId) {
                   // LOGI("+++ %s", szBuff);
                }
                else {
                    ftp_printf(iFtpPrtId, "%s", szBuff);
                }
            } while (1);
        }
    }
    
    iRet = 0;

exit:

    if (NULL != hSe) {
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                LOGW("FAIL to stop stor printf search, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop stor printf search, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
            }
        }
        hSe = NULL;
    }

    if (NULL != hSpSe) {
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSpSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                LOGW("FAIL to stop stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
        }
        hSpSe = NULL;
    }

    if (-1 != iFtpPrtId) {
        ftp_printf_close(iFtpPrtId, NULL);
        iFtpPrtId = -1;
    }

    return NULL;

}


int app_stor_printf_search_async(time_t tStart, time_t tEnd, const char *pszFtpURL, BOOL bUploadSp, BOOL bCompress)
{
    pthread_t tId = (pthread_t)-1;
    sem_t struSyncSem;
    unsigned int uRet = 0;
    ftp_upload_t ftp_upParam;
    sem_init(&struSyncSem, 0, 0);
    memset(&ftp_upParam, 0, sizeof(ftp_upload_t));

    ftp_upParam.tStart = tStart;
    ftp_upParam.tEnd = tEnd;
    memcpy(ftp_upParam.pszFtpURL, pszFtpURL,sizeof(ftp_upParam.pszFtpURL));
    ftp_upParam.bUploadSp = bUploadSp;
    ftp_upParam.bCompress = bCompress;
    ftp_upParam.pstruSyncSem = &struSyncSem;
    uRet = pthread_create(&tId, NULL, app_stor_printf_search, &ftp_upParam);
    if (0 != uRet) {
        LOGE("FAIL to create %s\n", strerror(uRet));
        sem_destroy(&struSyncSem);
        return -1;
    }
    pthread_detach(tId);  /*线程分离*/
    sem_wait(&struSyncSem);
    sem_destroy(&struSyncSem);
    return 0;
}


int app_stor_printf_search_stopall(void)
{
    logger_t *p = &g_logger_t;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;

    if (!p->bUsed || -1 == p->iStorPrtId) {
        return -1;
    }

    if (-1 == stor_printf_search_stop(p->iStorPrtId, NULL, &errNo)) {
        LOGE("FAIL to stop all stor printf search, iStorPrtId = %d, %s\n", p->iStorPrtId, stor_printf_strerr(errNo));
        return -1;
    }

    return 0;

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