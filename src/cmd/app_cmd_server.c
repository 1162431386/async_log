/**
 * @file app_cmd.c
 * @brief 
 * @author wwk (1162431386@qq.com)
 * @version 1.0
 * @date 2023-07-06
 * @copyright Copyright (c) 2023  by  MOVE-X
 * 
 * @par 修改日志:
 * <table>
 * <tr><th>Date       <th>Version <th>Author  <th>Description
 * <tr><td>2023-07-06     <td>1.0     <td>wwk   <td>修改?
 * </table>
 */

#include <ctype.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <sys/un.h>
#include "app_cmd.h"


#define ERR(fmt, args...) printf("\033[31mE[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)

char  *g_pSendData = NULL;

void help_app_cmd_help(void);
int app_cmd_help(int argc, char *argv[]);

/**@brief  接受对应clint数据
 * @param  fd     套接字句柄
 * @param  pData  接收数据缓存
 * @param  uLen   接收数据缓存大小
 * @return 成功返回 0
 * @return 错误返回 -1
 * @see
 * @note
 */
int cmd_readn(int iFd, void *pData, unsigned int uLen)
{
    unsigned int uLeft = 0;
    unsigned int uRead = 0;
    int  iRet  = 0;
    unsigned int uTry  = 0;
    char   *pDst = NULL;
    fd_set rdFds;
    struct timeval tv= {0};

    uLeft = uLen;
    pDst  = pData;
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    while (uLeft > 0)
    {
        FD_ZERO(&rdFds);
        FD_SET(iFd, &rdFds);
        iRet = select(iFd + 1, &rdFds, NULL, NULL, &tv);
        if (iRet < 0)
        {
            ERR("Select error out.");
            return ERROR;
        }
        else if (iRet == 0)
        {
            uTry++;
            if (uTry > 10)
            {
                break;
            }
            ERR("Select time out.");
        }

        if (FD_ISSET(iFd, &rdFds))
        {
            if ((int)(uRead = recv(iFd, pDst, uLeft, 0)) <= 0)
            {
                break;
            }

            uLeft -= uRead;
            pDst  += uRead;
        }
    }

    return uLen - uLeft;
}

/**@brief  发送数据给对应clint端
 * @param  fd     套接字句柄
 * @param  pData  要发送的数据
 * @param  uLen   发送数据的长度
 * @return 成功返回 0
 * @return 错误返回 -1
 * @see
 * @note
 */
unsigned int cmd_writen(int iFd, void *pData, unsigned int uLen)
{
    unsigned int uLeft   = 0;
    int  iWriten = 0;
    char   *pSrc   = NULL;

    uLeft = uLen;
    pSrc  = pData;

    while (uLeft > 0)
    {
        if ((iWriten = send(iFd, pSrc, uLeft, 0)) < 0)
        {
            ERR("send %s.",strerror(errno));
            break;
        }

        uLeft -= iWriten;
        pSrc  += iWriten;
    }

    return uLen- uLeft;
}

CMD_CTRL_T g_stShellCmd[] =
{
    CMD_LIST
};



void help_app_cmd_help(void)
{
    int iCnt = 0;
    int i = 0;
    CMD_CTRL_T *pstShell = NULL;

    iCnt = sizeof(g_stShellCmd) / sizeof(g_stShellCmd[0]);
    printf("应用支持的命令个数 : %d \n", iCnt);
    pstShell = &g_stShellCmd[0];
    do
    {
        printf(" %-30s %-40s \n", pstShell[i].pName, pstShell[i].pHelpStr);
    }while (++i < iCnt);
    printf("\n");

    return;
}


int app_cmd_help(int argc, char *argv[])
{
    int iCnt = 0;
    int i = 0;
    CMD_CTRL_T *pstShell = NULL;

    if (argc == 1)
    {
        iCnt = sizeof(g_stShellCmd) / sizeof(g_stShellCmd[0]);
        printf("应用支持的命令个数 : %d", iCnt);
        pstShell = &g_stShellCmd[0];
        do
        {
            if (i%2 == 0)
            {
                printf("\n");
            }
            printf(" %-30s", pstShell[i].pName);
        }while (++i < iCnt);
        printf("\n");

        return OK;
    }
    else if (argc == 2)
    {
        if (strncmp(argv[1], "-a", strlen(argv[1])) == 0)
        {
            help_app_cmd_help();
            return OK;
        }
    }

    return ERROR;
}


/**@brief  命令解析之后,调用对应的命令函数
 * @param  CMD_DATA_T 调试命令结构体指针
 * @return 成功返回 0
 * @return 错误返回 -1
 * @see
 * @note
 */
static unsigned int cmd_param_prase(CMD_DATA_T *pShellData)
{
    int  iRet = 0;
    int  argc = 0;
    unsigned int uCnt = 0;
    CMD_CTRL_T *pstShell = NULL;
    void * uData[MAX_SHELL_ARGC]= {0};

    if (pShellData == NULL)
    {
        return ERROR;
    }

    argc = pShellData->uArgc;
    for (uCnt = 0; uCnt < argc; uCnt++)
    {
        uData[uCnt] = (pShellData->aArgv[uCnt]);
    }

    uCnt     = 0;
    pstShell = &g_stShellCmd[0];
    do
    {
        if (strlen(pstShell[uCnt].pName) == strlen(pShellData->aArgv[0]) &&
            strncmp(pstShell[uCnt].pName, pShellData->aArgv[0], strlen(pShellData->aArgv[0])) == 0)
        {
            if (argc != 1 && strncmp(pShellData->aArgv[1], "--help", strlen(pShellData->aArgv[1])) == 0)
            {
                if (pstShell[uCnt].help)
                {
                    pstShell[uCnt].help();
                    return OK;
                }
                else
                {
                    printf("no help\n");
                }
            }

            /*每次命令输出先输出一个换行，比较美观*/
            printf("\r\n");
            iRet = pstShell[uCnt].entry(argc, (char **)&uData);
            if (iRet == ERROR)
            {
                printf("试用 %s --help 获取更多信息\n", pShellData->aArgv[0]);
            }
            

            return iRet;
        }
    }while (++uCnt < (sizeof(g_stShellCmd) / sizeof(g_stShellCmd[0])));

    printf("Unknow cmd : %s\n", pShellData->aArgv[0]);

    return ERROR;
}

int app_cmd_process(char* pCmdParam,int uCmdLen)
{
    if(NULL == pCmdParam || 0 == uCmdLen)
    {
        return ERROR;
    }
    
    CMD_DATA_T stCmdData;
    memset(&stCmdData,0,sizeof(stCmdData));
    stCmdData.uMagic = CMD_MAGIC_NUM;
    stCmdData.uArgc = 0;

    char cmdTmpParam[256] = {0};
    snprintf(cmdTmpParam,sizeof(cmdTmpParam),"%s ",pCmdParam);

    /*skip ' '*/
    char*  pStart = cmdTmpParam;
    char*  pEnd = pStart;
    unsigned int uCopy = 0;
    
    while(stCmdData.uArgc < MAX_SHELL_ARGC)
    {
        while(' ' == *pStart && '\0' != *pStart)
        {
            pStart++;
        }

        if('\0' == *pStart)
        {
            break;
        }
        
        pEnd = pStart + 1;
        while(' ' != *pEnd && '\0' != *pEnd)
        {
            pEnd++;
        }

        if(' ' != *pEnd || ' ' == *pStart)
        {
            break;
        }
       
        uCopy = pEnd - pStart;
        uCopy = MIN(uCopy,MAX_SHELL_LENGTH);
        memcpy(stCmdData.aArgv[stCmdData.uArgc],pStart,uCopy);
        stCmdData.uArgc++;    
        pStart = pEnd + 1;   
    }
    
    return cmd_param_prase(&stCmdData);
}


/**@brief soket编程,创建socket bind listen
 * @param
 * @return 成功返回 大于0值
 * @return 错误返回 -1
 * @see
 * @note
 */
int cmd_serv_listen(const char *name)
{
    socklen_t iLen    = 0;
    int iSockFd = 0;
    struct sockaddr_un un;

    iSockFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iSockFd == -1)
    {
        ERR("Socket %s\n", strerror(errno));
        return ERROR;
    }

    unlink(name);
    ERR("name %s\n", name);
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, name, sizeof(un.sun_path) - 1);
    iLen = sizeof(struct sockaddr_un);

    do
    {
        if (bind(iSockFd, (struct sockaddr *)&un, iLen) < 0)
        {
            ERR("Bind fd:%d failed %s", iSockFd, strerror(errno));
            break;
        }

        if (listen(iSockFd, 10) < 0)
        {
            ERR("Listen fd:%d failed %s", iSockFd, strerror(errno));
            break;
        }

        return iSockFd;
    } while(0);

    close(iSockFd);
    return ERROR;
}

/**@brief 命令调试服务线程,接受串口发送的命令
 * @param
 * @return
 * @see
 * @note
 */
void *cmd_server_task(void *arg)
{
    int iSockFd   = 0;
    int iCliFd    = 0;
    int iWriten   = 0;
    int iReadn    = 0;
    char  *pReadBuf = NULL;
    CMD_DATA_T *pstCmdData = NULL;
    socklen_t scokLen = 0;
    struct sockaddr_un un;

    iSockFd = cmd_serv_listen(SOCKED_CMD_PATH);
    if (iSockFd < 0)
    {
        ERR("Listen %s.", strerror(errno));
        return NULL;
    }

    pReadBuf = (char *)malloc(MAX_BUFF_LEN);
    if (pReadBuf == NULL)
    {
        ERR("Malloc failed.");
        close(iSockFd);
        return NULL;
    }

    g_pSendData = (char *)malloc(MAX_BUFF_LEN);
    if (g_pSendData == NULL)
    {
        ERR("Malloc failed.");
        SAFE_FREE(pReadBuf);
        close(iSockFd);
        return NULL;
    }

    while (1)
    {
        memset(g_pSendData, 0, MAX_BUFF_LEN);
        memset(pReadBuf, 0, MAX_BUFF_LEN);
        iCliFd = accept(iSockFd, (struct sockaddr *)&un, &scokLen);
        if (iCliFd < 0)
        {
            ERR("Accept failed.");
            continue;
        }

        if ((iReadn = cmd_readn(iCliFd, pReadBuf, sizeof(CMD_DATA_T))) != sizeof(CMD_DATA_T))
        {
            iWriten = snprintf(g_pSendData, MAX_BUFF_LEN, "readn failed read = %d\n", iReadn);
            cmd_writen(iCliFd, g_pSendData, iWriten);
            close(iCliFd);
            continue;
        }

        pstCmdData = (CMD_DATA_T *)pReadBuf;
        if (pstCmdData->uMagic != CMD_MAGIC_NUM)
        {
            iWriten = snprintf(g_pSendData, MAX_BUFF_LEN, "readn data magic err = %#x.\n", pstCmdData->uMagic);
            cmd_writen(iCliFd, g_pSendData, iWriten);
            close(iCliFd);
            continue;
        }
        cmd_param_prase(pstCmdData);
        close(iCliFd);
    }

    SAFE_FREE(g_pSendData);
    SAFE_FREE(pReadBuf);
    close(iSockFd);
    unlink(SOCKED_CMD_PATH);
}


/**@brief 命令调试初始化函数
 * @param
 * @return 成功返回  0
 * @return 错误返回  !0
 * @see
 * @note
 */
int cmd_server_init(void)
{
    unsigned int uRet = 0;
    pthread_t thread = (pthread_t)-1;

    uRet = pthread_create(&thread, NULL, cmd_server_task, NULL);
    if (uRet != OK)
    {
        ERR("Create task cmd_server failed %s.\n", strerror(errno));
    }
    pthread_detach(thread);  /*线程分离*/
    return uRet;
}


