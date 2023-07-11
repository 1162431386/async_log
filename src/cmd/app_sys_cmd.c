#include <stdio.h>
#include <unistd.h>
#include "app_sys_cmd.h"
#include "async_log.h"
#include "common_utils.h"




void help_cmd_app_test(void)
{
    printf("用法: app_test \n");
    printf("参数: \n");
    printf("提示: \n");
    printf("举例: app_test \n");
}

int cmd_app_test(int argc, char *argv[])
{
    if (argc != 1)
    {
        help_cmd_app_test();
        return -1;
    }
    printf("This is test cmd\n");
    printf("这是一个测试命令\n");
    return 0;
}




void help_cmd_app_storprt(void)
{
    printf(" 用法: app_storprt [-s <开始时间>] [-e <结束时间>] [-U <ftp URL> ] [ -p [sp文件类型] ] [ -S ] [ -h ]\n");
    printf("参数1: -s 检索开始时间, 格式: YYMMDD_hhmmss, 举例: 230328_080000\n");
	printf("参数2: -e 检索结束时间, 格式: YYMMDD_hhmmss, 举例: 230328_100000\n");
    printf("参数3: -U 检索结果上传ftp的URL, 举例: ftp://upg:upg@122.114.126.16:21/stor_log\n");
    printf("参数5: -S 停止所有搜索任务\n");
    printf("参数6: -F 直接上传sp文件内容\n");
    printf("参数4: -Z 压缩上传的sp文件，需要和-F一起使用\n");
    printf("参数7: -h 显示帮助信息\n");
    printf(" 举例: app_storprt -s 230328_080000 -e 230328_100000 -U ftp://hik:hiklinux@122.114.12.58/moveX\n");
    printf(" 举例: app_storprt -p 7\n");
    printf(" 举例: app_storprt -S\n");
    return;

}


int cmd_app_storprt(int argc, char *argv[])
{
    int iOpt = -1;
    time_t tStart = 0;
    time_t tEnd = 0;
    time_t tCurr = time(NULL);
    char szFtpURL[256] = {0};
    char szStartTime[32] = {0};
    char szEndTime[32] = {0};
    char szPrtBuff[512] = {0};
    char *pszPrtBuff = NULL;
    int i = 0;
    BOOL bStopAllSe = FALSE;
    BOOL bUploadSp = FALSE;
    BOOL bCompress = FALSE;
    int optind = 1;
    while (-1 != (iOpt = getopt(argc, argv, "s:e:U:hSFZ"))) {
        switch (iOpt) {
            case 's':
                tStart = time_str_to_time_t(optarg);
                break;
            case 'e':
                tEnd = time_str_to_time_t(optarg);
                break;
            case 'U':
                SAFE_STRNCPY(szFtpURL, optarg, sizeof (szFtpURL));
                break;
            case 'S':
                bStopAllSe = TRUE;
                break;
            case 'F':
                bUploadSp = TRUE;
                break;
            case 'Z':
                bCompress = TRUE;
                break;
            case 'h':
                help_cmd_app_storprt();
                break;
            default:
                help_cmd_app_storprt();
                return -1;
        }
    }

    if (bStopAllSe) {
        LOGW("STOP ALL stor_printf search instances...\n");
        return app_stor_printf_search_stopall();
    }
    if (0 == tStart) {
        
        if (0 == tEnd) {
            tStart = tCurr - 1 * 60;
            tEnd = tCurr + 1 * 60;
        }
        else {
            tStart = tEnd - 1 * 60;
        }
    }
    else {
        if (0 == tEnd) {
            tEnd = tStart + 1 * 60;
        }
        else {
        }
    } 
    LOGI("STOR_PRT.search: %s => %s\n", strtime(tStart, szStartTime, sizeof (szStartTime)), strtime(tEnd, szEndTime, sizeof (szEndTime)));
    LOGI("STOR_PRT.ftpURL: \"%s\"\n", szFtpURL);
    LOGI("STOR_PRT.uploadSp: \"%s\"\n", bUploadSp ? "TRUE" : "FALSE");
    return app_stor_printf_search_async(tStart, tEnd, szFtpURL, bUploadSp, bCompress);
}
