#include <stdio.h>
#include "app_cmd.h"

int main()
{
    if(0 != cmd_server_init())
    {
        printf("cmd server start failed!\n");
    }
    while(1);
    return 0;
}