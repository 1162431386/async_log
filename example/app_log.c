#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <unistd.h>
#include <unistd.h>
#include <errno.h>
#include "async_log.h"
#include "stor_printf.h"
#include "app_cmd.h"



void *task_test1(void *arg)
{
    char Name[16] = {0};
    int  i = *(int *)arg;
    int cnt = 0;
    snprintf(Name, sizeof(Name), "DEMO_%d", i);
    
    prctl(PR_SET_NAME, Name);
    // while(1)
    {
        //printf("hello, this is test demo! thread num : %d   cnt : %d \n", i, cnt);
        //printf("hello, this is test demo! thread num : %d   cnt : %d \n", i, cnt);
        //printf("hello, this is test demo! thread num : %d   cnt : %d \n", i, cnt);
        //printf("hello, this is test demo! thread num : %d   cnt : %d \n", i, cnt);
        //printf("hello, this is test demo! thread num : %d   cnt : %d \n", i, cnt);


        LOGI("hello, this is test demo! thread num\n");
        //LOGD("hello, this is test demo! thread num\n");
        //LOGE("hello, this is test demo! thread num\n");
        //LOGW("hello, this is test demo! thread num\n");
    }
    return NULL;
}




int main(int argc, char *argv[])
{
    #define NUM_THREADS  20
    pthread_t threads[NUM_THREADS];
    int thread_args[NUM_THREADS];
    log_stor_init();
    setLogLevel(LOG_LEVEL_INFO);
    set_log_show_thread_name(1);
    int i = 0;
    
    for (i = 0; i < NUM_THREADS; i++) 
    {
        thread_args[i] = i;
        if( 0!= pthread_create(&threads[i], NULL, task_test1, (void *)&thread_args[i]))
        {
            printf("+++++++++++++++++++++++++%s\n", strerror(errno));
        }
    }
    
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
   
    if(0 != cmd_server_init())
    {
        printf("cmd server start failed!\n");
    }
    while(1);
    return 0;
}

