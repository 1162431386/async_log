#include <stdio.h>
#include "app_sys_cmd.h"

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