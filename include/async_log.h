#ifndef _ASYNC_LOG_H
#define _ASYNC_LOG_H

#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_CRITICAL 5
#define LOG_LEVEL_OFF 6

typedef enum 
{
    trace       = LOG_LEVEL_TRACE,
    debug       = LOG_LEVEL_DEBUG,
    info        = LOG_LEVEL_INFO,
    warn        = LOG_LEVEL_WARN,
    err         = LOG_LEVEL_ERROR,
    critical    = LOG_LEVEL_CRITICAL,
    off         = LOG_LEVEL_OFF,
    n_levels
}eLoggerLevel;


int asynclog(eLoggerLevel level,  const char *format, ...);
void  setLogLevel(eLoggerLevel level);
int log_stor_init();

#define FILE_NAME ((NULL == strrchr(__FILE__, '/')) ? (__FILE__) : (strrchr(__FILE__, '/') + 1))
#define ASYNC_LOGT(fmt, ...) asynclog(trace,    "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)
#define ASYNC_LOGD(fmt, ...) asynclog(debug,    "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)
#define ASYNC_LOGI(fmt, ...) asynclog(info,     "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)
#define ASYNC_LOGW(fmt, ...) asynclog(warn,     "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)
#define ASYNC_LOGE(fmt, ...) asynclog(err,      "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)
#define ASYNC_LOGA(fmt, ...) asynclog(critical, "[%s() %s:%d]" fmt, __func__, FILE_NAME, __LINE__, ##__VA_ARGS__)

#define LOGV      ASYNC_LOGT
#define LOGD      ASYNC_LOGD
#define LOGI      ASYNC_LOGI
#define LOGW      ASYNC_LOGW
#define LOGE      ASYNC_LOGE
#define LOGA      ASYNC_LOGA







#endif

