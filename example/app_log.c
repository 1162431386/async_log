#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "async_log.h"
#include "stor_printf.h"



void *task_test1(void *arg)
{
    prctl(PR_SET_NAME,"DEMO_TASK");
    while(1)
    {
       LOGV("hello, this is test demo!\n");
       LOGD("hello, this is test demo!\n");
       LOGE("hello, this is test demo!\n");
       LOGI("hello, this is test demo!\n");
       LOGA("hello, this is test demo!\n");
       sleep(1);
    }
    return NULL;
}



int main()
{
    log_stor_init();

    pthread_t tId  = (pthread_t)-1;
    pthread_create(&tId, NULL, task_test1, NULL);
    sleep(5);
    setLogLevel(LOG_LEVEL_ERROR);
    sleep(5);
    setLogLevel(LOG_LEVEL_OFF);
    sleep(5);
    setLogLevel(LOG_LEVEL_CRITICAL);
    while(1);
    return 0;
}

