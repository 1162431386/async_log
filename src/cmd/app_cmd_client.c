
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>

#define SOCKED_CMD_PATH             "/var/usrCmd.socket"
#define CLI_CMD_ARGC                16
#define CLI_CMD_LEN                 64

#define CLI_BUFF_LEN                4096
#define CLI_MAGIC_NUM               0x484B4145

#define offsetof(TYPE, MEMBER)      ((intptr_t)&((TYPE *)0)->MEMBER)

#define CLI_ERROR                   printf

typedef unsigned int    UINT32;
typedef unsigned char   UINT8;
typedef char            CHAR;
typedef int             INT32;
typedef void            VOID;

typedef struct
{
    UINT32      uMagic;
    UINT32      uArgc;
    CHAR        aArgv[CLI_CMD_ARGC][CLI_CMD_LEN];
}CLI_DATA_T;

static INT32 client_readn(INT32 iFd, VOID *pData, UINT32 uLen)
{
    UINT32 uLeft = 0;
    INT32  uRead = 0;
    INT32  uRet  = 0;
    UINT32 uTry  = 0;
    CHAR   *pDst = NULL;
    struct timeval tv = {0};
    fd_set rdFds;

    uLeft = uLen;
    pDst  = pData;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    while (uLeft > 0)
    {
        FD_ZERO(&rdFds);
        FD_SET(iFd, &rdFds);
        uRet = select(iFd + 1, &rdFds, NULL, NULL, &tv);
        if (uRet < 0)
        {
            CLI_ERROR("Select error out.");
            return -1;
        }
        else if (uRet == 0)
        {
            uTry++;
            if (uTry > 10)
            {
                break;
            }
            CLI_ERROR("Select time out try (%d) again.", uTry);
        }

        if(FD_ISSET(iFd, &rdFds))
        {
            if ((uRead = recv(iFd, pDst, uLeft, 0)) <= 0)
            {
                CLI_ERROR("Recv %s uRead:%d\n", strerror(errno), uRead);
                break;
            }
            uLeft -= uRead;
            pDst  += uRead;
        }
    }

    return uLen - uLeft;
}

static INT32 client_writen(INT32 iFd, VOID *pData, UINT32 uLen)
{
    UINT32 uLeft   = 0;
    INT32  uWriten = 0;
    CHAR   *pSrc   = NULL;

    uLeft = uLen;
    pSrc  = pData;

    while (uLeft > 0)
    {
        if ((uWriten = send(iFd, pSrc, uLeft, 0)) < 0)
        {
            CLI_ERROR("Send %s\n", strerror(errno));
            break;
        }

        uLeft -= uWriten;
        pSrc  += uWriten;
    }

    return uLen- uLeft;
}


static INT32 client_conn(const char *name)
{
    INT32 iSockFd = 0;
    INT32 iRet    = 0;
    INT32 iLen    = 0;
    struct sockaddr_un un = {0};

    iSockFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (iSockFd == -1)
    {
        CLI_ERROR("Socket %s\n", strerror(errno));
        return -1;
    }

    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, name);

    iLen = (INT32)(offsetof(struct sockaddr_un, sun_path) + strlen(name));
    iRet = connect(iSockFd, (struct sockaddr *)&un, iLen);
    if (iRet < 0)
    {
        CLI_ERROR("Connect %s\n", strerror(errno));
        close(iSockFd);
        return -1;
    }

    return iSockFd;
}

INT32 main(int argc, char *argv[])
{
    INT32 i       = 0;
    INT32 iRet    = 0;
    INT32 iSockFd = 0;
    CLI_DATA_T stCmdData = {0};

    if (argc > CLI_CMD_ARGC)
    {
        CLI_ERROR("argc num too large argc : %d.\n", argc);
        return -1;
    }

    iSockFd = client_conn(SOCKED_CMD_PATH);
    if (iSockFd < 0)
    {
         CLI_ERROR("Connect %s %s\n", SOCKED_CMD_PATH, strerror(errno));
         return -1;
    }

    stCmdData.uMagic = CLI_MAGIC_NUM;
    stCmdData.uArgc = argc;

    for (i = 0; i < argc; i++)
    {
        strncpy(stCmdData.aArgv[i], argv[i], strlen(argv[i]));
    }

    iRet = client_writen(iSockFd, &stCmdData, sizeof(stCmdData));
    if (iRet != sizeof(stCmdData))
    {
        CLI_ERROR("Write %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


