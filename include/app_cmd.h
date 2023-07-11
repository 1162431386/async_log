#ifndef __APP_CMD_H_
#define __APP_CMD_H_
#include "app_cmd_list.h"


#ifndef ERROR
#define ERROR               -1
#endif

#ifndef OK
#define OK                  0
#endif

#define MAX_SHELL_ARGC                      16
#define MAX_SHELL_LENGTH                    64
#define MAX_BUFF_LEN                        4096
#define CMD_MAGIC_NUM                       0x484B4145
#define SOCKED_CMD_PATH                     "/var/usrCmd.socket"

#define CMD_ENTRY(command) \
         {.pName = #command, .entry = cmd_##command}

#define CMD_ENTRY_FUNC(command, func, helpStr) \
         {.pName = #command, .entry = func, .help = help_##func, .pHelpStr = helpStr}

#define MAX_SHELL_ARGC                      16

typedef  int (*CMD_ENTRY_FUNC)(int argc, char *argv[]);
typedef void (*CMD_HELP_FUNC)(void);


/**
 * @brief 求最大值
*/
#ifndef MAX
#define MAX(a,b)        ((a) > (b) ? (a) : (b))
#endif


/**
 * @brief 求最小值
*/
#ifndef MIN
#define MIN(a,b)        ((a) > (b) ? (b) : (a))
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

typedef struct
{
    char            *pName;
    CMD_ENTRY_FUNC  entry;
    CMD_HELP_FUNC   help;
    char            *pHelpStr;
}CMD_CTRL_T;

typedef struct
{
    unsigned int  uMagic;
    unsigned int  uArgc;
    char    aArgv[MAX_SHELL_ARGC][MAX_SHELL_LENGTH];
}CMD_DATA_T;



int cmd_server_init(void);

#endif