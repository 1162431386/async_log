#include <stdio.h>
#include <unistd.h>
#include "app_sys_cmd.h"
#include "async_log.h"
#include "common_utils.h"




void help_cmd_app_test(void)
{
    printf("�÷�: app_test \n");
    printf("����: \n");
    printf("��ʾ: \n");
    printf("����: app_test \n");
}

int cmd_app_test(int argc, char *argv[])
{
    if (argc != 1)
    {
        help_cmd_app_test();
        return -1;
    }
    printf("This is test cmd\n");
    printf("����һ����������\n");
    return 0;
}




void help_cmd_app_storprt(void)
{
    printf(" �÷�: app_storprt [-s <��ʼʱ��>] [-e <����ʱ��>] [-U <ftp URL> ] [ -p [sp�ļ�����] ] [ -S ] [ -h ]\n");
    printf("����1: -s ������ʼʱ��, ��ʽ: YYMMDD_hhmmss, ����: 230328_080000\n");
	printf("����2: -e ��������ʱ��, ��ʽ: YYMMDD_hhmmss, ����: 230328_100000\n");
    printf("����3: -U ��������ϴ�ftp��URL, ����: ftp://upg:upg@122.114.126.16:21/stor_log\n");
    printf("����5: -S ֹͣ������������\n");
    printf("����6: -F ֱ���ϴ�sp�ļ�����\n");
    printf("����4: -Z ѹ���ϴ���sp�ļ�����Ҫ��-Fһ��ʹ��\n");
    printf("����7: -h ��ʾ������Ϣ\n");
    printf(" ����: app_storprt -s 230328_080000 -e 230328_100000 -U ftp://hik:hiklinux@122.114.12.58/moveX\n");
    printf(" ����: app_storprt -p 7\n");
    printf(" ����: app_storprt -S\n");
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
