#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <regex.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <regex.h>
#include "packet.h"
#include "ftp_printf.h"

//#define FTP_PRINTF_USING_PRT_MOD

static int ftpprintf_dbg_enable = 0;

#ifdef FTP_PRINTF_USING_PRT_MOD
#include "prt_interface.h"
#define ERR(...) do{MOD_PRT_DBG(PRT_MOD_FTPPRINTF,  LEVEL_ERR,__VA_ARGS__)}while(0)
#define INFO(...) do{MOD_PRT_DBG(PRT_MOD_FTPPRINTF, LEVEL_INFO,__VA_ARGS__)}while(0)
#define WARN(...) do{MOD_PRT_DBG(PRT_MOD_FTPPRINTF, LEVEL_WARN,__VA_ARGS__)}while(0)
#define DBG(...) do{MOD_PRT_DBG(PRT_MOD_FTPPRINTF, LEVEL_HINT,__VA_ARGS__)}while(0)
#define OFTEN(...) do{MOD_PRT_DBG(PRT_MOD_FTPPRINTF, LEVEL_OFTEN,__VA_ARGS__)}while(0)
#define HEX(pBuf, iLen) do{MOD_PRT_HEX(PRT_MOD_FTPPRINTF, pBuf, iLen)}while(0)
#else
#define FTP_PRINTF_INFO
#ifdef FTP_PRINTF_INFO
#define INFO(fmt, args...) printf("I[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define INFO(fmt, args...)
#endif

#define FTP_PRINTF_WARN
#ifdef FTP_PRINTF_WARN
#define WARN(fmt, args...) printf("W[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define WARN(fmt, args...)
#endif

#define FTP_PRINTF_ERR
#ifdef FTP_PRINTF_ERR
#define ERR(fmt, args...) printf("\033[31mE[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)
#else
#define ERR(fmt, args...)
#endif

#define FTP_PRINTF_DBG
#ifdef FTP_PRINTF_DBG
#define DBG(fmt, args...) do { \
    if (ftpprintf_dbg_enable) { \
        printf("D[%s:%4d] " fmt, __FILE__, __LINE__, ##args); \
    } \
} while (0)
#else
#define DBG(fmt, args...)
#endif
#endif

#ifndef SAFE_STRNCPY
#define SAFE_STRNCPY(dst, src, n) do { \
    memset((char *)(dst), 0, (n)); \
    strncpy((dst), (src), n); \
    (dst)[n - 1] = '\0'; \
}while (0)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a[0])))
#endif

#ifndef SAFE_CLOSE
#define SAFE_CLOSE(fd) do { \
	if (-1 != (fd)) { \
		close((fd)); \
		(fd) = -1; \
	} \
} while (0)
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(p) do { \
	if (NULL != (p)) { \
		free((p)); \
		(p) = NULL; \
	} \
} while (0)
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct {
	int type;
	const char *strtype;
}FTP_PRINTF_TYPE_STR_T;

#define FTP_PRINTF_TYPE_STR_ITEM(type) {type, #type}

/* FTP PRINTF模块固有性能参数 */
/* 打印缓存最大尺寸 */
#define MAX_FTP_PRINTF_BUFF_SIZE (4 * 1024)
#define MAX_FTP_DATA_BUFF_SIZE (4 * 1024)

struct ftp_printf_t {
    pthread_mutex_t mutex;
    int inuse;

    struct ftp_printf_param_t param;

	int wakeupfds[2]; /* 0: 用于主线程内部, 1: 用于外部接口 */
    int chatfds[2]; /* 0: 用于主线程内部, 1: 用于外部接口 */

	char *printbuff; /* 指向打印缓存的指针 */

    pthread_t tid; /* 主线程tid */
	char threadname[16]; /* 主线程名 */

	enum ftp_printf_state state;
    int conti_state_count; /* 持续当前状态的次数 */

	int ctrlfd; /* ftp控制链路sockfd */
	int datafd; /* ftp数据链路sockfd */
	int packid; /* packet交互id */

	int sending; /* 正在发送数据 */

    char *databuff; /* 指向databuff的指针 */
    unsigned int databuff_size; /* databuff大小 */
    unsigned int datalen; /* 数据的实际长度 */

	time_t attachtime; /* multifile模式下, 是否在文件扩展名前加入当前时间, 仅适用于multifile模式 */

	int sync; /* 是否要做同步等待的操作 */
    
};

#define FTP_PRINTF_MAX_NUM 8

struct ftp_printfs_t {
    pthread_mutex_t mutex;
    int init;

    struct ftp_printf_t ftpprintf[FTP_PRINTF_MAX_NUM];
};

#define FTP_PRINTF_ERRNO_SET(err, e) do { \
    if (NULL != (err)) { \
        if (FTP_PRINTF_SUCC != (e)) { \
            DBG("SET ftp printf errno => %d(%s)\n", (int)(e), ftp_printf_strerr((e))); \
        } \
        *(err) = (e); \
    } \
} while (0)

#define FTP_PRINTF_STATE_SET(state, s) do { \
	DBG("SET ftpprintf state %s => %s\n", ftp_printf_strstate((state)), ftp_printf_strstate((s))); \
	(state) = (s); \
} while (0)

static FTP_PRINTF_TYPE_STR_T sg_ftpprintf_errstr_tab[] = {
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_SUCC),
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_INVALID_INPUT),
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_INVALID_PARAM),
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_NO_RESOURCES),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_SOCKETPAIR_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_INVALID_ID),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_BAD_ID),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_RESOURCES_NOT_INITED),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_PTHREAD_COND_WAIT_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_OUT_OF_MEMORY),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_CREATE_THREAD_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_IO_TIMEOUT),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_IO_WAKEUP),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_NO_ROUTINE_SPECIFIED),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_EXIT),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_SELECT_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_CREATE_SOCKET_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_GETSOCKOPT_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_IO_ERROR),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_CONNECT_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_PACKET_OPEN_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_PACKET_READ_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_SVR_NOT_READY),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_WRITE_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_UNKNOWN_STATE_CODE),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_STATE_CODE_MISMATCH),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_READ_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_PASSIVE_PARAM_PARSE_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_NO_SUCH_DIR),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_DIR_ALREADY_EXIST),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_NO_SUCH_FILE),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_QUIT_FAIL),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_PEER_SHUTDOWN),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_MAIN_THREAD_NOTEXIST),
	FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_WRITE_NOTCOMPLETE),
};

static FTP_PRINTF_TYPE_STR_T sg_ftpprintf_errtypestr_tab[] = {
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_ERRNO_TYPE_SUCC),
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_ERRNO_TYPE_TRY_AGAIN),
    FTP_PRINTF_TYPE_STR_ITEM(FTP_PRINTF_ERRNO_TYPE_ABORT),
};

extern const char * strtime(time_t t, char *pszTime, unsigned int uStrSize);

unsigned int hostGetByName(const char* name)
{
  struct addrinfo hints;
  struct addrinfo *result=NULL;
  int ret=0; 
  unsigned int  retVal=0;
  if( NULL == name)
  {
  	return 0;
  }
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;  /* any protocol */                                                                             
                      
  ret = getaddrinfo(name, NULL, &hints, &result);
  if ( 0 != ret)
  {
     printf("erro getaddrinfo: %s\n", gai_strerror(ret));
     return 0;
  }
  if(NULL != result)
  {
  	retVal = ((struct sockaddr_in *)(result->ai_addr))->sin_addr.s_addr;	
  	freeaddrinfo(result);	
  }
  
  return retVal;
  
}

int is_regex_match(const char *str, const char *pattern, int cflags)
{
	regex_t reg;
	int reg_errcode;
	char reg_errstr[128] = {0};
	int match_it = 0;

	if (NULL == str || '\0' == str[0] || NULL == pattern || '\0' == pattern[0]) {
		return 0;
	}

	reg_errcode = regcomp(&reg, pattern, cflags | REG_NOSUB);
	if (0 != reg_errcode) {
		regerror(reg_errcode, &reg, reg_errstr, sizeof (reg_errstr));
		printf("Fail to compile regex: \"%s\", %s\n", pattern, reg_errstr);
		match_it = 0;
	}
	else {
		if (0 == regexec(&reg, str, 0, NULL, 0)) {
			match_it = 1;
		}
		else {
			match_it = 0;
		}
		regfree(&reg);
	}

	return match_it;

}

static enum ftp_printf_errno_type ftp_printf_errno_type_get(enum ftp_printf_errno err)
{
    enum ftp_printf_errno_type type;

    switch ((int)err) {
        case 200 ... 299:
            type = FTP_PRINTF_ERRNO_TYPE_SUCC;
            break;
        case 300 ... 399:
            type = FTP_PRINTF_ERRNO_TYPE_TRY_AGAIN;
            break;
        case 400 ... 499:
            type = FTP_PRINTF_ERRNO_TYPE_ABORT;
            break;
        default:
            ERR("UNKNOWN ftp printf errno: %d\n", (int)err);
            type = FTP_PRINTF_ERRNO_TYPE_ABORT;
            break;
    }

    return type;

}

static FTP_PRINTF_TYPE_STR_T sg_ftpprintf_state_tab[] = {
	FTP_PRINTF_TYPE_STR_ITEM(STATE_INIT),
	FTP_PRINTF_TYPE_STR_ITEM(STATE_LOGIN),
	FTP_PRINTF_TYPE_STR_ITEM(STATE_SYST),
	FTP_PRINTF_TYPE_STR_ITEM(STATE_TYPE),
    FTP_PRINTF_TYPE_STR_ITEM(STATE_CWD),
    FTP_PRINTF_TYPE_STR_ITEM(STATE_DATA),
    FTP_PRINTF_TYPE_STR_ITEM(STATE_SEND),
	FTP_PRINTF_TYPE_STR_ITEM(STATE_EXIT),
};

static struct ftp_printfs_t sg_ftpprintfs = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .init = 0,
};

static enum ftp_printf_errno ftp_printf_state_do_tab_check(void);

static const char * ftp_printf_svrrsp_convert(char *svrrsp, unsigned int rsplen)
{
    int i;

    for (i = 0; i < rsplen; i++) {
        if ((unsigned char)svrrsp[i] >= 128) {
            svrrsp[i] = '.';
        }
    }

    return (const char *)svrrsp;

}

static int ftp_printf_pthread_id_verify(pthread_t tid)
{
	if ((pthread_t)-1 == tid || (pthread_t)0 == tid) {
		return -1;
	}

	if (0 == pthread_kill(tid, 0)) {
		return 0;
	}

	return -1;

}

static int ftp_printf_socket_noblock_set(int sockfd, int set)
{
	int flags;
	int ret = -1;

	if (-1 == sockfd) {
		ERR("INVALID input param, sockfd = %d\n", sockfd);
		return -1;
	}

	flags = fcntl(sockfd, F_GETFL, 0);
	if (-1 == flags) {
		ERR("FAIL to get sockfd flags, %d(%s)\n", errno, strerror(errno));
		return -1;
	}

	if (set) {
		ret = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	}
	else {
		ret = fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
	}
	if (-1 == ret) {
		ERR("FAIL to set sockfd flags, %d(%s)\n", errno, strerror(errno));
		return -1;
	}

	return 0;

}

static const char * ftp_printf_strtype(const FTP_PRINTF_TYPE_STR_T table[], unsigned int tabcount, int type)
{
	static char s_strunknown[32] = {0};
    int i;
	
    if (NULL == table || 0 == tabcount) {
        snprintf(s_strunknown, sizeof (s_strunknown), "NULLTAB_0x%x", type);
        return s_strunknown;
    }
    
    for (i = 0; i < tabcount; i++) {
        if (table[i].type == (int)type) {
			if (NULL == table[i].strtype) {
				snprintf(s_strunknown, sizeof (s_strunknown), "NULL_0x%x", type);
				return s_strunknown;
			}
            return table[i].strtype;
        }
    }
	
    snprintf(s_strunknown, sizeof (s_strunknown), "UNKNOWN_0x%x", type);
    return s_strunknown;

}


static int ftp_printf_to_id(const struct ftp_printf_t *ftpprintf)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sg_ftpprintfs.ftpprintf); i++) {
        if ((struct ftp_printf_t *)ftpprintf == &(sg_ftpprintfs.ftpprintf[i])) {
            return i;
        }
    }

    return -1;

}

static enum ftp_printf_errno id_to_ftp_printf(int id, struct ftp_printf_t **pp_ftpprintf)
{
    if (id < 0 || id > (ARRAY_SIZE(sg_ftpprintfs.ftpprintf) - 1)) {
        ERR("INVALID id: %d, valid range: [0, %d]\n", id, (int)(ARRAY_SIZE(sg_ftpprintfs.ftpprintf) - 1));
        return FTP_PRINTF_INVALID_ID;
    }

	/* 判断总资源是否已初始化 */
	if (!sg_ftpprintfs.init) {
		return FTP_PRINTF_RESOURCES_NOT_INITED;
	}

	if (NULL != pp_ftpprintf) {
		*pp_ftpprintf = &(sg_ftpprintfs.ftpprintf[id]);
	}

    return FTP_PRINTF_SUCC;

}

static void ftp_printf_param_print(const struct ftp_printf_param_t *param)
{
    if (NULL == param) {
        return;
    }

    printf("\n");
    printf("+++ FTP_addr: \"%s\"\n", param->addr);    
    printf("+++ FTP_port: %d\n", (int)param->port);
    printf("+++ FTP_user: \"%s\"\n", param->user);
    printf("+++ FTP_pass: \"%s\"\n", param->pass);
    printf("+++ FTP_dir: \"%s\"\n", param->dir);
    printf("+++ FTP_filename: \"%s\"\n", param->filename);

    return;

}

static enum ftp_printf_errno ftp_printf_check(const struct ftp_printf_param_t *param)
{
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;

    if (NULL == param) {
        return FTP_PRINTF_INVALID_INPUT;
    }

    if ('\0' == param->addr[0]) {
        DBG("EMPTY ftp addr: \"%s\"\n", param->addr);
        return FTP_PRINTF_INVALID_PARAM;
    }
    if (0 == param->port) {
        DBG("EMPTY ftp port: %d\n", param->port);
        return FTP_PRINTF_INVALID_PARAM;
    }
    if ('\0' == param->user[0]) {
        DBG("EMPTY ftp user: \"%s\"\n", param->user);
        return FTP_PRINTF_INVALID_PARAM;
    }
    if ('\0' == param->pass[0]) {
        DBG("EMPTY ftp pass: \"%s\"\n", param->pass);
        return FTP_PRINTF_INVALID_PARAM;
    }
    if ('\0' == param->filename[0]) {
        DBG("EMPTY ftp filename: \"%s\"\n", param->filename);
        return FTP_PRINTF_INVALID_PARAM;
    }

    err = ftp_printf_state_do_tab_check();
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    INFO("NEW ftp printf...\n");
    ftp_printf_param_print(param);

    return FTP_PRINTF_SUCC;
    
}

static inline void ftp_printfs_init(struct ftp_printfs_t *ftpprintfs)
{
	int i = 0;
	struct ftp_printf_t *ftpprintf = NULL;

	for (i = 0; i < ARRAY_SIZE(ftpprintfs->ftpprintf); i++) {
		ftpprintf = &(ftpprintfs->ftpprintf[i]);
		memset((char *)ftpprintf, 0, sizeof (*ftpprintf));
		pthread_mutex_init(&(ftpprintf->mutex), NULL);
		ftpprintf->inuse = 0;
		ftpprintf->wakeupfds[0] = -1;
		ftpprintf->wakeupfds[1] = -1;
		ftpprintf->chatfds[0] = -1;
		ftpprintf->chatfds[1] = -1;
		ftpprintf->printbuff = NULL;
		ftpprintf->tid = (pthread_t)-1;
		ftpprintf->state = STATE_INIT;
        ftpprintf->conti_state_count = 0;
	    ftpprintf->ctrlfd = -1;
		ftpprintf->datafd = -1;
		ftpprintf->packid = -1;
		ftpprintf->sending = 0;
        ftpprintf->databuff = NULL;
        ftpprintf->databuff_size = MAX_FTP_DATA_BUFF_SIZE;
        ftpprintf->datalen = 0;
		ftpprintf->attachtime = 0;
		ftpprintf->sync = 0;
	}
	
	return;

}

static enum ftp_printf_errno _ftp_printf_fini(struct ftp_printf_t *ftpprintf)
{
	int ret = -1;
    pthread_t tid = (pthread_t)-1;

    tid = ftpprintf->tid;
	if (0 == ftp_printf_pthread_id_verify(tid)) {
		do {
			/* 等待主线程退出 */
			DBG("WRITE \"exit\" to wakeup %s\n", ftpprintf->threadname);
			write(ftpprintf->wakeupfds[1], "exit", strlen("exit"));
			ret = pthread_join(tid, NULL);
			if (0 == ret) {
				DBG("SUCC to wait \"%s\"\n", ftpprintf->threadname);
				break;
			}
			ERR("FAIL to do pthread_join for \"%s\", %d(%s)\n", ftpprintf->threadname, ret, strerror(ret));
			break; /* ???此处是否需要根据错误号做一些特殊处理 */
		} while (0 == ftp_printf_pthread_id_verify(tid));
	}
    
	ftpprintf->sync = 0;
	ftpprintf->attachtime = 0;
    ftpprintf->databuff = NULL;
    ftpprintf->databuff_size = MAX_FTP_DATA_BUFF_SIZE;
    ftpprintf->datalen = 0;
	ftpprintf->sending = 0;
	if (-1 != ftpprintf->packid) {
		(void)packet_close(ftpprintf->packid, NULL);
		ftpprintf->packid = -1;
	}
	SAFE_CLOSE(ftpprintf->datafd);
	SAFE_CLOSE(ftpprintf->ctrlfd);
    ftpprintf->conti_state_count = 0;
	ftpprintf->state = STATE_INIT;
	memset(ftpprintf->threadname, 0, sizeof (ftpprintf->threadname));
	ftpprintf->tid = (pthread_t)-1;

	SAFE_FREE(ftpprintf->printbuff);

	SAFE_CLOSE(ftpprintf->chatfds[1]);
	SAFE_CLOSE(ftpprintf->chatfds[0]);

	SAFE_CLOSE(ftpprintf->wakeupfds[1]);
	SAFE_CLOSE(ftpprintf->wakeupfds[0]);

	memset((char *)&(ftpprintf->param), 0, sizeof (ftpprintf->param));

	return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno _ftp_printf_init(struct ftp_printf_t *ftpprintf, const struct ftp_printf_param_t *param)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;

	memcpy((char *)&(ftpprintf->param), (char *)param, sizeof (struct ftp_printf_param_t));
	if (-1 == socketpair(AF_LOCAL, SOCK_DGRAM, 0, ftpprintf->wakeupfds)) {
		ERR("FAIL to create socketpair for wakeupfds, %d(%s)\n", errno, strerror(errno));
		e = FTP_PRINTF_SOCKETPAIR_FAIL;
		goto err_exit;
	}
	if (-1 == socketpair(AF_LOCAL, SOCK_DGRAM, 0, ftpprintf->chatfds)) {
		ERR("FAIL to create socketpair for chatfds, %d(%s)\n", errno, strerror(errno));
		e = FTP_PRINTF_SOCKETPAIR_FAIL;
		goto err_exit;
	}
	if (-1 == ftp_printf_socket_noblock_set(ftpprintf->chatfds[1], 1)) {
		WARN("FAIL to set ftpprintf->chatfds[1]: %d NONBLOCK\n", ftpprintf->chatfds[1]);
	}

#if 0
	int rcvbuff_size = 32 * 1024;
	int sndbuff_size = 32 * 1024;
	socklen_t s1 = sizeof (rcvbuff_size);
	socklen_t s2 = sizeof (sndbuff_size);
	if (-1 == getsockopt(ftpprintf->chatfds[0], SOL_SOCKET, SO_RCVBUF, &rcvbuff_size, &s1)) {
		WARN("FAIL to get ftpprintf->chatfds[0]: %d SO_RCVBUF %d\n", ftpprintf->chatfds[0], rcvbuff_size);
	}
	ERR("++++++++++++++++0, rcv, %d\n", rcvbuff_size);
	if (-1 == getsockopt(ftpprintf->chatfds[1], SOL_SOCKET, SO_SNDBUF, &sndbuff_size, &s2)) {
		WARN("FAIL to get ftpprintf->chatfds[1]: %d SO_SNDBUF %d\n", ftpprintf->chatfds[1], sndbuff_size);
	}
	ERR("++++++++++++++++1, snd, %d\n", sndbuff_size);
	rcvbuff_size = 1024 * 1024;
	sndbuff_size = 1024 * 1024;
	if (-1 == setsockopt(ftpprintf->chatfds[0], SOL_SOCKET, SO_RCVBUF, &rcvbuff_size, sizeof (rcvbuff_size))) {
		WARN("FAIL to set ftpprintf->chatfds[0]: %d SO_RCVBUF %d\n", ftpprintf->chatfds[0], rcvbuff_size);
	}
	if (-1 == setsockopt(ftpprintf->chatfds[1], SOL_SOCKET, SO_SNDBUF, &sndbuff_size, sizeof (sndbuff_size))) {
		WARN("FAIL to set ftpprintf->chatfds[1]: %d SO_SNDBUF %d\n", ftpprintf->chatfds[1], sndbuff_size);
	}
#endif
	
	ftpprintf->printbuff = (char *)malloc(MAX_FTP_PRINTF_BUFF_SIZE);
	if (NULL == ftpprintf->printbuff) {
		ERR("OUT OF MEMORY!!!\n");
		e = FTP_PRINTF_OUT_OF_MEMORY;
		goto err_exit;
	}
	memset(ftpprintf->printbuff, 0, MAX_FTP_PRINTF_BUFF_SIZE);
	ftpprintf->tid = (pthread_t)-1;
	snprintf(ftpprintf->threadname, sizeof (ftpprintf->threadname), "ftpPrintf%d", ftp_printf_to_id(ftpprintf));
	ftpprintf->state = STATE_INIT;
    ftpprintf->conti_state_count = 0;
	ftpprintf->ctrlfd = -1;
	ftpprintf->datafd = -1;
	ftpprintf->packid = -1;
	ftpprintf->sending = 0;
    ftpprintf->databuff = NULL;
    ftpprintf->databuff_size = MAX_FTP_DATA_BUFF_SIZE;
    ftpprintf->datalen = 0;
	ftpprintf->attachtime = 0;
	ftpprintf->sync = ftpprintf->param.sync;

	return FTP_PRINTF_SUCC;

err_exit:

	if (FTP_PRINTF_SUCC != _ftp_printf_fini(ftpprintf)) {
		WARN("FAIL to fini ftp_printf\n");
	}
	
	return e;

}

static enum ftp_printf_errno ftp_printf_init(const struct ftp_printf_param_t *param, struct ftp_printf_t **pp_ftpprintf)
{
	int i = 0;
	struct ftp_printf_t *ftpprintf = NULL;
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;

	pthread_mutex_lock(&sg_ftpprintfs.mutex);
	{
		if (!sg_ftpprintfs.init) {
			ftp_printfs_init(&sg_ftpprintfs);
			sg_ftpprintfs.init = 1;
		}
		for (i = 0; i < ARRAY_SIZE(sg_ftpprintfs.ftpprintf); i++) {
			ftpprintf = &(sg_ftpprintfs.ftpprintf[i]);
			if (ftpprintf->inuse) {
				continue;
			}
		    e = _ftp_printf_init(ftpprintf, param);
			if (FTP_PRINTF_SUCC == e) {
				ftpprintf->inuse = 1;
				*pp_ftpprintf = ftpprintf;
			}
			pthread_mutex_unlock(&sg_ftpprintfs.mutex);
			return e;
		}
	}
	pthread_mutex_unlock(&sg_ftpprintfs.mutex);

	return FTP_PRINTF_NO_RESOURCES;

}

struct ftp_printf_state_do_t {
    enum ftp_printf_state state;
    const char *state_desc;
    enum ftp_printf_errno (*state_do)(struct ftp_printf_t *ftpprintf);
};

enum ftp_printf_errno ftp_printf_tcp_connect(int iSockFd, int iWakeUpFd, const struct sockaddr_in *pstruDstAddr, int iWaitMs)
{
	int iRet;
	fd_set struRSet;
	fd_set struWSet;
	struct timeval struTimeOut = {5, 0};
	struct timeval *pstruTimeOut = NULL;
	int iNFds = -1;
	enum ftp_printf_errno err = FTP_PRINTF_SUCC;
	int iErr = 0;
	int iLen = sizeof (int);

	if (-1 == iSockFd || NULL == pstruDstAddr) {
		ERR("INVALID input param, iSockFd = %d, pstruDstAddr = %p\n", iSockFd, pstruDstAddr);
		return FTP_PRINTF_INVALID_INPUT;
	}

	if (iWaitMs < 0) {
		pstruTimeOut = NULL;
	}
	else {
		pstruTimeOut = &struTimeOut;
		pstruTimeOut->tv_sec = iWaitMs / 1000;
		pstruTimeOut->tv_usec = (iWaitMs % 1000) * 1000;
	}
 
	ftp_printf_socket_noblock_set(iSockFd, 1);

	iRet = connect(iSockFd, (struct sockaddr *)pstruDstAddr, sizeof (*pstruDstAddr));
	if (-1 == iRet) {
		if (EINPROGRESS == errno) {
			FD_ZERO(&struWSet);
			FD_SET(iSockFd, &struWSet);
			iNFds = iSockFd;
			if (-1 != iWakeUpFd) {
				FD_ZERO(&struRSet);
				FD_SET(iWakeUpFd, &struRSet);
				iNFds = max(iNFds, iWakeUpFd);
			}
			do {
				iRet = select(iNFds + 1, (-1 == iWakeUpFd) ? NULL : &struRSet, &struWSet, NULL, pstruTimeOut);
			} while (-1 == iRet && EINTR == errno);
			if (-1 == iRet) {
				ERR("FAIL to select, %s\n", strerror(errno));
				err = FTP_PRINTF_SELECT_FAIL;
			}
			else if (0 == iRet) {
				err = FTP_PRINTF_IO_TIMEOUT;
			}
			else {
				if (-1 != iWakeUpFd && FD_ISSET(iWakeUpFd, &struRSet)) {
					err = FTP_PRINTF_IO_WAKEUP;
				}
				else {
					if (-1 == getsockopt(iSockFd, SOL_SOCKET, SO_ERROR, &iErr, (socklen_t *)&iLen)) {
						err = FTP_PRINTF_GETSOCKOPT_FAIL;
					}
					else {
						if (0 == iErr) {
							err = FTP_PRINTF_SUCC;
						}
						else {
							DBG("iErr = %d\n", iErr);
							err = FTP_PRINTF_IO_ERROR;
						}
					}
				}
			}
		}
		else {
			err = FTP_PRINTF_CONNECT_FAIL;
		}
	}
	else {
		err = FTP_PRINTF_SUCC;
	}

	ftp_printf_socket_noblock_set(iSockFd, 0);

	return err;

}

enum ftp_printf_errno ftp_printf_tcp_write(int iFd, int iWakeUpFd, const void *pBuff, unsigned int uBuffLen, struct timeval *pstruTimeOut)
{
	fd_set struWSet;
	fd_set struRSet;
	int iNFds = -1;
	int iRet = -1;

	if (-1 == iFd || NULL == pBuff || 0 == uBuffLen) {
		ERR("INVALID input param, iFd = %d, pBuff = %p, uBuffLen = %u\n", iFd, pBuff, uBuffLen);
		return FTP_PRINTF_INVALID_INPUT;
	}

	FD_ZERO(&struWSet);
	FD_SET(iFd, &struWSet);
	iNFds = iFd;
	if (-1 != iWakeUpFd) {
		FD_ZERO(&struRSet);
		FD_SET(iWakeUpFd, &struRSet);
		iNFds = max(iNFds, iWakeUpFd);
	}

	do {
		iRet = select(iNFds + 1, (-1 == iWakeUpFd) ? NULL : &struRSet, &struWSet, NULL, pstruTimeOut);
	} while (-1 == iRet && EINTR == errno);
	if (-1 == iRet) {
		ERR("FAIL to select, %s\n", strerror(errno));
		return FTP_PRINTF_SELECT_FAIL;
	}
	else if (0 == iRet) {
		return FTP_PRINTF_IO_TIMEOUT;
	}
	
	if (-1 != iWakeUpFd && FD_ISSET(iWakeUpFd, &struRSet)) {
		return FTP_PRINTF_IO_WAKEUP;
	}

	if (FD_ISSET(iFd, &struWSet)) {
		do {
			iRet = send(iFd, pBuff, uBuffLen, 0);
		} while (-1 == iRet && EINTR == errno);
		if (-1 == iRet) {
			ERR("FAIL to write, %s\n", strerror(errno));
			return FTP_PRINTF_WRITE_FAIL;
		}

		if ((unsigned int)iRet != uBuffLen) {
			WARN("WRITE not complete\n");
			return FTP_PRINTF_WRITE_FAIL;
		}

		return FTP_PRINTF_SUCC;
	}

	return FTP_PRINTF_IO_ERROR;

}

enum ftp_printf_errno ftp_printf_tcp_write_v2(int iFd, int iWakeUpFd, const void *pBuff, unsigned int uBuffLen, int iWaitMs)
{
	struct timeval struTimeOut = {5, 0};
	struct timeval *pstruTimeOut = NULL;

	if (iWaitMs < 0) {
		pstruTimeOut = NULL;
	}
	else {
		pstruTimeOut = &struTimeOut;
		pstruTimeOut->tv_sec = iWaitMs / 1000;
		pstruTimeOut->tv_usec = (iWaitMs % 1000) * 1000;
	}

	return ftp_printf_tcp_write(iFd, iWakeUpFd, pBuff, uBuffLen, pstruTimeOut);

}

enum ftp_printf_errno ftp_printf_tcp_read(int iFd, int iWakeUpFd, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, struct timeval *pstruTimeOut)
{
	fd_set struRSet;
	int iNFds = -1;
	int iRet = -1;

	if (-1 == iFd) {
		ERR("INVALID input param, iFd = %d, pBuff = %p, uBuffSize = %u\n", iFd, pBuff, uBuffSize);
		return FTP_PRINTF_INVALID_INPUT;
	}

	FD_ZERO(&struRSet);
	FD_SET(iFd, &struRSet);
	iNFds = iFd;
	if (-1 != iWakeUpFd) {
		FD_SET(iWakeUpFd, &struRSet);
		iNFds = max(iNFds, iWakeUpFd);
	}

	do {
		iRet = select(iNFds + 1, &struRSet, NULL, NULL, pstruTimeOut);
	} while (-1 == iRet && EINTR == errno);
	if (-1 == iRet) {
		ERR("FAIL to select, %s\n", strerror(errno));
		return FTP_PRINTF_SELECT_FAIL;
	}
	else if (0 == iRet) {
		return FTP_PRINTF_IO_TIMEOUT;
	}

	if (-1 != iWakeUpFd && FD_ISSET(iWakeUpFd, &struRSet)) {
		return FTP_PRINTF_IO_WAKEUP;
	}

	

	if (FD_ISSET(iFd, &struRSet)) {
		if (NULL != pBuff && uBuffSize > 0) {
			do {
				iRet = recv(iFd, pBuff, uBuffSize, 0);
			} while (-1 == iRet && EINTR == errno);
			if (-1 == iRet) {
				ERR("FAIL to read, %s\n", strerror(errno));
				return FTP_PRINTF_READ_FAIL;
			}
			else if (0 == iRet) {
				WARN("PEER SHUTDOWN\n");
				return FTP_PRINTF_PEER_SHUTDOWN;
			}
			if (NULL != puReadLen) {
				*puReadLen = (unsigned int)iRet;
			}
		}
		else {
			/* 如果没有指定合适接收缓冲区, 则只做有数据的判断, 不把数据读出来 */
		}

		return FTP_PRINTF_SUCC;
	}

	return FTP_PRINTF_IO_ERROR;

}

enum ftp_printf_errno ftp_printf_tcp_read_v2(int iFd, int iWakeUpFd, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, int iWaitMs)
{
	struct timeval struTimeOut = {5, 0};
	struct timeval *pstruTimeOut = NULL;

	if (iWaitMs < 0) {
		pstruTimeOut = NULL;
	}
	else {
		pstruTimeOut = &struTimeOut;
		pstruTimeOut->tv_sec = iWaitMs / 1000;
		pstruTimeOut->tv_usec = (iWaitMs % 1000) * 1000;
	}

	return ftp_printf_tcp_read(iFd, iWakeUpFd, pBuff, uBuffSize, puReadLen, pstruTimeOut);

}

PACK_ERR ftp_printf_ctrlfd_packet_ana(unsigned char *pAnaBuff, unsigned int uAnaDataLen, unsigned int *puPacketIdx, unsigned int *puPacketLen, unsigned int *puCorrectLen)
{
    int i;
    int start_idx = 0;
    int end_idx = 0;

    /* 以0x0d, 0x0a结尾的字符串 */

    if (uAnaDataLen < 2) {
        return PACK_DATA_NOT_ENOUGH;
    }

    i = 0;
    do {
        for (end_idx = start_idx = i; i < (uAnaDataLen - 1); i++) {
            if ((0x0d == pAnaBuff[i]) && (0x0a == pAnaBuff[i + 1])) {
                end_idx = i;
                break;
            }
        }
        if (i >= (uAnaDataLen - 1)) {
            return PACK_DATA_NOT_ENOUGH;
        }
        if (end_idx > start_idx) {
            break;
        }
        i += 2;
    } while (1);
  
    *puPacketIdx = start_idx;
    *puPacketLen = end_idx - start_idx;
    *puCorrectLen = 2; /* 跳过结尾的0x0d, 0x0a */

    return PACK_SUCC;

}

static inline void ftp_printf_filename_attachtime(const char *filename, time_t attachtime, char *filename_attachtime, unsigned int size)
{
	const char *suffix_p = strrchr(filename, '.');
	char curtime[32] = {0};
	int len;

	if (NULL == suffix_p) {
		snprintf(filename_attachtime, size, "%s_%s", filename, strtime(attachtime, curtime, sizeof (curtime)));
	}
	else {
		len = suffix_p - filename;
		memcpy(filename_attachtime, filename, len);
		snprintf(filename_attachtime + len, size - len, "_%s%s", strtime(attachtime, curtime, sizeof (curtime)), suffix_p);
	}
	
	return;

}

static inline int ftp_chat_passive_param_parse(const char *passive_param, char *addr, unsigned int addr_size, unsigned short *port)
{
    const char *p1 = NULL;
    const char *p2 = NULL;
    int addr1 = 0;
    int addr2 = 0;
    int addr3 = 0;
    int addr4 = 0;
    int port1 = 0;
    int port2 = 0;

    p1 = strchr(passive_param, '(');
    if (NULL == p1) {
        ERR("FAIL to seek \'(\' in \"%s\"\n", passive_param);
        return -1;
    }
    p2 = strchr(p1 + 1, ')');
    if (NULL == p2) {
        ERR("FAIL to seek \')\' in \"%s\"\n", passive_param);
        return -1;
    }
    if (p2 <= p1) {
        ERR("\')\' before \'(\' in \"%s\"\n", passive_param);
        return -1;
    }

    sscanf(p1 + 1, "%d,%d,%d,%d,%d,%d", &addr1, &addr2, &addr3, &addr4, &port1, &port2);
    snprintf(addr, addr_size, "%d.%d.%d.%d", addr1, addr2, addr3, addr4);
    *port = port1 * 256 + port2;

    return 0;

}

static enum ftp_printf_errno ftp_chat_ex(struct ftp_printf_t *ftpprintf, const char *cmd, char *rsp, unsigned int rspsize, unsigned int *rsplen, int disable_wakeup, int waitms)
{
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    PACK_ERR packerr = PACK_SUCC;
	int wakeupfd = -1;
	int go_on = 0;

    if ((NULL != cmd) && ('\0' != cmd[0])) {
        DBG(">> %s", cmd);
        ERR("############################\n");
        err = ftp_printf_tcp_write_v2(ftpprintf->ctrlfd, disable_wakeup ? -1 : ftpprintf->wakeupfds[0], cmd, strlen(cmd), waitms);
        if (FTP_PRINTF_SUCC != err) {
            ERR("FAIL to send ftp cmd: \"%s\", %s\n", cmd, ftp_printf_strerr(err));
            return err;
        }
    }

    if (NULL != rsp && rspsize > 0) {
		do {
			if (disable_wakeup) {
				wakeupfd = -1;
				if (-1 == packet_cmd(ftpprintf->packid, PACK_CMD_WAKEUPFD_SET, &wakeupfd, sizeof (wakeupfd), NULL, 0, NULL, waitms, &packerr)) {
					WARN("FAIL to do packet_cmd, PACK_CMD_WAKEUPFD_SET, %s\n", packet_strerr(packerr));
				}
			}
			
	        if (-1 == packet_read(ftpprintf->packid, rsp, rspsize, rsplen, waitms, &packerr)) {
	            ERR("FAIL to do packet read, %s\n", packet_strerr(packerr));
	            return FTP_PRINTF_PACKET_READ_FAIL;
	        }
	        rsp[rspsize - 1] = '\0';
			go_on = is_regex_match(rsp, "^[0-9]+\\-.*", REG_EXTENDED | REG_ICASE) ? 1 : 0;

			if (disable_wakeup) {
				wakeupfd = ftpprintf->wakeupfds[0];
				if (-1 == packet_cmd(ftpprintf->packid, PACK_CMD_WAKEUPFD_SET, &wakeupfd, sizeof (wakeupfd), NULL, 0, NULL, waitms, &packerr)) {
					WARN("FAIL to do packet_cmd, PACK_CMD_WAKEUPFD_SET, %s\n", packet_strerr(packerr));
				}
			}

	        DBG("<< %s\n", ftp_printf_svrrsp_convert(rsp, strlen(rsp)));
		}while (go_on);
    }
    
    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_chat(struct ftp_printf_t *ftpprintf, const char *cmd, char *rsp, unsigned int rspsize, unsigned int *rsplen, int waitms)
{
	return ftp_chat_ex(ftpprintf, cmd, rsp, rspsize, rsplen, 0, waitms);

}

static enum ftp_printf_errno ftp_chat_USER(struct ftp_printf_t *ftpprintf, const char *user, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;

    snprintf(cmd, sizeof (cmd), "USER %s\r\n", user);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 331/* 用户名正确, 需要密码 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;
    
}

static enum ftp_printf_errno ftp_chat_PASS(struct ftp_printf_t *ftpprintf, const char *user, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;

    snprintf(cmd, sizeof (cmd), "PASS %s\r\n", user);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 230/* 用户登录成功, 继续进行 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_SYST(struct ftp_printf_t *ftpprintf, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;

    snprintf(cmd, sizeof (cmd), "SYST\r\n");
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 215/* NAME系统类型, 其中, NAME是Assigned Numbers文档中所列的正式系统名称 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            /* NAME失败, 只给出一个警告, 后面继续按正常流程处理 */
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_SUCC;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_TYPE(struct ftp_printf_t *ftpprintf, const char *type, int *rspcode, int waitms)
{
	char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;

    snprintf(cmd, sizeof (cmd), "TYPE %s\r\n", type);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 200/* 表示切换传输类型成功 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            /* 类型切换失败, 只给出一个警告, 后面继续按正常流程处理 */
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_SUCC;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_MKD(struct ftp_printf_t *ftpprintf, const char *dir, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    int code = 0;

    snprintf(cmd, sizeof (cmd), "MKD %s\r\n", dir);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 250/* 成功创建该目录 */:
            err = FTP_PRINTF_SUCC;
            break;
        case 257/* 此目录已创建 */:
        case 521:
        case 550:
            err = FTP_PRINTF_DIR_ALREADY_EXIST;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_CWD(struct ftp_printf_t *ftpprintf, const char *dir, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    int code = 0;

    snprintf(cmd, sizeof (cmd), "CWD %s\r\n", dir);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd: %s, %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 250/* 成功 */:
            err = FTP_PRINTF_SUCC;
            break;
        case 450/* 未执行请求的文件操作, 文件不可用(例如, 文件繁忙) */:
            /* CWD请求: xlight ftp返回450表示没有该目录 xujian 20181208 */
            /* CWD请求: vsftpd返回450表示TODO.......... */
            err = FTP_PRINTF_NO_SUCH_DIR;
            break;
        case 550/* 目录不存在 */:
            err = FTP_PRINTF_NO_SUCH_DIR;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_PASV(struct ftp_printf_t *ftpprintf, int *rspcode, char *addr, unsigned int addr_size, unsigned short *port, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;

    snprintf(cmd, sizeof (cmd), "PASV\r\n");
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 227/* 进入被动模式 */:
            /* 解析PASV模式下服务器IP以及端口 */
            if (-1 == ftp_chat_passive_param_parse(rsp, addr, addr_size, port)) {
                ERR("FAIL to parse passive param, \"%s\"\n", rsp);
                err = FTP_PRINTF_PASSIVE_PARAM_PARSE_FAIL;
            }
            else {
                err = FTP_PRINTF_SUCC;
            }
            break;
		case 421/* FTP服务器连接太多, 无可用端口资源, 暂时无法响应用户的请求, 需要稍后再试 */:
			err = FTP_PRINTF_NO_RESOURCES;
			break;
        default:
            /* NAME失败, 只给出一个警告, 后面继续按正常流程处理 */
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break; 
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_NLST(struct ftp_printf_t *ftpprintf, const char *filename, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;
    
    snprintf(cmd, sizeof (cmd), "NLST %s\r\n", filename);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
		case 125/* Data connection already open; Transfer starting. */:
        case 150/* 服务器开通数据链路, 此时可以从服务器接收数据 */:
            err = FTP_PRINTF_SUCC;
            break;
        case 550/* 服务器反馈文件不存在, 可以继续接收 */:
            err = FTP_PRINTF_NO_SUCH_FILE;
            break;
        default:
            /* NAME失败, 只给出一个警告, 后面继续按正常流程处理 */
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break; 
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_STOR(struct ftp_printf_t *ftpprintf, const char *filename, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;
    
    snprintf(cmd, sizeof (cmd), "STOR %s\r\n", filename);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
		case 125/* Data connection already open; Transfer starting. */:
        case 150/* 服务器开通数据链路, 此时可以从服务器发送数据 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break; 
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_APPE(struct ftp_printf_t *ftpprintf, const char *filename, int *rspcode, int waitms)
{
    char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;
    
    snprintf(cmd, sizeof (cmd), "APPE %s\r\n", filename);
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
		case 125/* Data connection already open; Transfer starting. */:
        case 150/* 服务器开通数据链路, 此时可以从服务器发送数据 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break; 
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_QUIT(struct ftp_printf_t *ftpprintf, int *rspcode, int waitms)
{
	char cmd[128] = {0};
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    enum ftp_printf_errno err;
    int code;   
    snprintf(cmd, sizeof (cmd), "QUIT\r\n");
    err = ftp_chat(ftpprintf, cmd, rsp, sizeof (rsp), &rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"%s\", %s\n", cmd, ftp_printf_strerr(err));
        if (NULL != rspcode) {
            *rspcode = 499; /* ftp协议中不存在, 该模块私有定义的一个状态码, 用于表示FTP交互失败 */
        }
        return err;
    }

    code = atoi(rsp);
    if (NULL != rspcode) {
        *rspcode = code;
    }
    switch (code) {
        case 221/* 注销成功 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            WARN("UNKNOWN state code: %d\n", code);
            err = FTP_PRINTF_QUIT_FAIL;
            break; 
    }

    return err;

}

static enum ftp_printf_errno ftp_chat_login(struct ftp_printf_t *ftpprintf, const char *user, const char *pass, int waitms)
{
    enum ftp_printf_errno err;
    int rspcode;

    err = ftp_chat_USER(ftpprintf, user, &rspcode, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_USER, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return err;
    }

    err = ftp_chat_PASS(ftpprintf, pass, &rspcode, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_PASS, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return err;
    }

    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_chat_mkdir_cd(struct ftp_printf_t *ftpprintf, const char *dir, int waitms)
{
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    int rspcode = 250;

    err = ftp_chat_MKD(ftpprintf, dir, &rspcode, waitms);
    switch (err) {
        case FTP_PRINTF_SUCC:
        case FTP_PRINTF_DIR_ALREADY_EXIST:
            /* 执行cd命令时, 如果目录不存在, 则创建该目录, 如果存在, 表示可以进行CWD操作 */
            break;
        default:
            return err;
    }

    err = ftp_chat_CWD(ftpprintf, dir, &rspcode, waitms);
    switch (err) {
        case FTP_PRINTF_SUCC:
            break;
        default:
            return err;
    }

    return FTP_PRINTF_SUCC;
    
}

static enum ftp_printf_errno ftp_chat_mkdir_cd_recusive(struct ftp_printf_t *ftpprintf, const char *dirs, int waitms)
{
    char *p = NULL;
    char *p_cp = NULL;
    char *dir = NULL;
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;

    if (NULL == ftpprintf || NULL == dirs || '\0' == dirs[0]) {
        ERR("INVALID inpur param, ftpprintf = %p, dirs = %p\n", ftpprintf, dirs);
        return FTP_PRINTF_INVALID_INPUT;
    }

    p_cp = p = strdup(dirs);
    if (NULL == p_cp) {
        ERR("FAIL to strdup \"%s\", OUT OF MEMORY!!!\n", dirs);
        return FTP_PRINTF_OUT_OF_MEMORY;
    }

    while (NULL != (dir = strsep(&p, "/"))) {
        if ('\0' == dir[0]) {
            continue;
        }
        err = ftp_chat_mkdir_cd(ftpprintf, dir, waitms);
        if (FTP_PRINTF_SUCC != err) {
            goto exit;
        }
    }

    err = FTP_PRINTF_SUCC;

exit:

    SAFE_FREE(p_cp);
    return err;

}

static enum ftp_printf_errno ftp_chat_passive_connect(struct ftp_printf_t *ftpprintf, int waitms)
{
    enum ftp_printf_errno err;
    int rspcode;
    char addr[32] = {0};
    unsigned short port;    
    struct sockaddr_in dstaddr;

    err = ftp_chat_PASV(ftpprintf, &rspcode, addr, sizeof (addr), &port, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_PASV, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return err;
    }

    SAFE_CLOSE(ftpprintf->datafd);
    ftpprintf->datafd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == ftpprintf->datafd) {
        ERR("FAIL to create tcp socket for data link, %s\n", strerror(errno));
        return FTP_PRINTF_CREATE_SOCKET_FAIL;
    }

    /* 建立数据链路TCP连接 */
    DBG("START to connect data link server: %s:%d\n", addr, (int)port);

    memset((char *)&dstaddr, 0, sizeof (dstaddr));
    dstaddr.sin_family = AF_INET;
    dstaddr.sin_addr.s_addr = inet_addr(addr);
    dstaddr.sin_port = htons(port);
    err = ftp_printf_tcp_connect(ftpprintf->datafd, ftpprintf->wakeupfds[0], &dstaddr, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to connect data link server: %s:%d, %s\n", addr, (int)port, ftp_printf_strerr(err));
        SAFE_CLOSE(ftpprintf->datafd);
        return err;
    }
	
	ftp_printf_socket_noblock_set(ftpprintf->datafd, 1);

    DBG(" SUCC to connect data link server: %s:%d\n", addr, (int)port);
    
    return FTP_PRINTF_SUCC;
    
}

static enum ftp_printf_errno ftp_chat_nlist(struct ftp_printf_t *ftpprintf, const char *filename, char *rsp, unsigned int rspsize, unsigned int *rsplen, int waitms)
{
    enum ftp_printf_errno err;
    int rspcode;
    char tmprsp[128] = {0};
    unsigned int tmprsp_len;

    err = ftp_chat_passive_connect(ftpprintf, waitms);
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    err = ftp_chat_NLST(ftpprintf, filename, &rspcode, waitms);
    if (FTP_PRINTF_SUCC != err) {
        SAFE_CLOSE(ftpprintf->datafd);
        if (FTP_PRINTF_NO_SUCH_FILE == err) {            
            return FTP_PRINTF_SUCC;
        }        
        ERR("FAIL to do ftp_chat_NLST, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return FTP_PRINTF_SUCC;
    }

    /* 读取一段数据 */
    err = ftp_printf_tcp_read_v2(ftpprintf->datafd, ftpprintf->wakeupfds[0], rsp, rspsize, rsplen, waitms);
    if (FTP_PRINTF_SUCC != err) {
		if (FTP_PRINTF_PEER_SHUTDOWN == err) {
			/* 如果NLST的文件不存在, 部分FTP服务器会采用不在data链路发任何数据直接关闭链路的方式方式, 此时read返回FTP_PRINTF_PEER_SHUTDOWN */
			memset(rsp, 0, rspsize);
		}
		else {
	        ERR("FAIL to read data from datafd(%d), %s\n", ftpprintf->datafd, ftp_printf_strerr(err));
	        SAFE_CLOSE(ftpprintf->datafd);
	        return err;
		}
    }
    SAFE_CLOSE(ftpprintf->datafd);

    err = ftp_chat(ftpprintf, NULL, tmprsp, sizeof (tmprsp), &tmprsp_len, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"\", %s\n", ftp_printf_strerr(err));
        return err;
    }
    rspcode = atoi(tmprsp);
    switch (rspcode) {
        case 226/* 数据传输完成 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", rspcode);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;
    
}

static enum ftp_printf_errno ftp_chat_put(struct ftp_printf_t *ftpprintf, const char *filename, int waitms)
{
    enum ftp_printf_errno err;
    int rspcode;
	int wakeup;
    char tmprsp[128] = {0};
    unsigned int tmprsp_len;

    err = ftp_chat_passive_connect(ftpprintf, waitms);
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    err = ftp_chat_STOR(ftpprintf, filename, &rspcode, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_STOR, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        SAFE_CLOSE(ftpprintf->datafd);
        return err;
    }

	/* 通知外界主任务ftp printf链接建立成功 */
	if (ftpprintf->sync) {
		err = FTP_PRINTF_SUCC;
		write(ftpprintf->chatfds[0], &err, sizeof (err));
		ftpprintf->sync = 0;
	}

    /* 发送指定数据 */
	ftpprintf->sending = 1;
	wakeup = 0;
	do {
		err = ftp_printf_tcp_read_v2(ftpprintf->chatfds[0], ftpprintf->wakeupfds[0], ftpprintf->databuff, ftpprintf->databuff_size, &ftpprintf->datalen, wakeup ? 0 : (waitms / 2/* 读用户数据的超时时间改成等IO超时时间的一半 */));
		if (FTP_PRINTF_IO_WAKEUP == err) {
			DBG("read chatfds[0](%d) wake up by wakeupfds[0](%d), wait left user data\n", ftpprintf->chatfds[0], ftpprintf->wakeupfds[0]);
			recv(ftpprintf->wakeupfds[0], tmprsp, sizeof (tmprsp), 0);
			wakeup = 1;
			continue;
		}
		if (FTP_PRINTF_SUCC != err) {
			DBG("FAIL to read user data, %s\n", ftp_printf_strerr(err));
			break;
		}

data_send:
	
	    err = ftp_printf_tcp_write_v2(ftpprintf->datafd, ftpprintf->wakeupfds[0], ftpprintf->databuff, ftpprintf->datalen, wakeup ? ftpprintf->param.wr_waitms : 16 * 1000);
		if (FTP_PRINTF_IO_WAKEUP == err) {
			DBG("write datafd(%d) wake up by wakeupfds[0](%d), write left user data\n", ftpprintf->datafd, ftpprintf->wakeupfds[0]);
			recv(ftpprintf->wakeupfds[0], tmprsp, sizeof (tmprsp), 0);
			wakeup = 1;
			goto data_send;
		}
		if (FTP_PRINTF_SUCC != err) {
	        ERR("FAIL to write data to datafd(%d), %s\n", ftpprintf->datafd, ftp_printf_strerr(err));
			break;
	    }
	} while (1);
	ftpprintf->sending = 0;	
    SAFE_CLOSE(ftpprintf->datafd);
	if (wakeup) {
		send(ftpprintf->wakeupfds[1], "wakeup", sizeof ("wakeup"), 0);
	}

	memset(tmprsp, 0, sizeof (tmprsp));
    err = ftp_chat(ftpprintf, NULL, tmprsp, sizeof (tmprsp), &tmprsp_len, (FTP_PRINTF_IO_TIMEOUT == err) ? ftpprintf->param.rd_waitms : waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"\", %s\n", ftp_printf_strerr(err));
        return err;
    }
    rspcode = atoi(tmprsp);
    switch (rspcode) {
        case 226/* 数据传输完成 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", rspcode);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_append(struct ftp_printf_t *ftpprintf, const char *filename, int waitms)
{
    enum ftp_printf_errno err;
    int rspcode;
	int wakeup = 0;
    char tmprsp[128] = {0};
    unsigned int tmprsp_len;

    err = ftp_chat_passive_connect(ftpprintf, waitms);
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    err = ftp_chat_APPE(ftpprintf, filename, &rspcode, waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_APPE, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        SAFE_CLOSE(ftpprintf->datafd);
        return err;
    }

	/* 通知外界主任务ftp printf链接建立成功 */
	if (ftpprintf->sync) {
		err = FTP_PRINTF_SUCC;
		write(ftpprintf->chatfds[0], &err, sizeof (err));
		ftpprintf->sync = 0;
	}

    /* 发送指定数据 */	
	ftpprintf->sending = 1;
	wakeup = 0;
	do {
		err = ftp_printf_tcp_read_v2(ftpprintf->chatfds[0], ftpprintf->wakeupfds[0], ftpprintf->databuff, ftpprintf->databuff_size, &ftpprintf->datalen, wakeup ? 0 : (waitms / 2/* 读用户数据的超时时间改成等IO超时时间的一半 */));
		if (FTP_PRINTF_IO_WAKEUP == err) {
			DBG("read chatfds[0](%d) wake up by wakeupfds[0](%d), wait left user data\n", ftpprintf->chatfds[0], ftpprintf->wakeupfds[0]);
			recv(ftpprintf->wakeupfds[0], tmprsp, sizeof (tmprsp), 0);
			wakeup = 1;
			continue;
		}
		if (FTP_PRINTF_SUCC != err) {
			DBG("FAIL to read user data, %s\n", ftp_printf_strerr(err));
			break;
		}

data_send:
	
	    err = ftp_printf_tcp_write_v2(ftpprintf->datafd, ftpprintf->wakeupfds[0], ftpprintf->databuff, ftpprintf->datalen, wakeup ? ftpprintf->param.wr_waitms : 16 * 1000);
		if (FTP_PRINTF_IO_WAKEUP == err) {
			DBG("write datafd(%d) wake up by wakeupfds[0](%d), write left user data\n", ftpprintf->datafd, ftpprintf->wakeupfds[0]);
			recv(ftpprintf->wakeupfds[0], tmprsp, sizeof (tmprsp), 0);
			wakeup = 1;
			goto data_send;
		}		
		if (FTP_PRINTF_SUCC != err) {
	        ERR("FAIL to write data to datafd(%d), %s\n", ftpprintf->datafd, ftp_printf_strerr(err));
			break;
	    }
	} while (1);
	ftpprintf->sending = 0;
    SAFE_CLOSE(ftpprintf->datafd);
	if (wakeup) {
		send(ftpprintf->wakeupfds[1], "wakeup", sizeof ("wakeup"), 0);
	}

    err = ftp_chat(ftpprintf, NULL, tmprsp, sizeof (tmprsp), &tmprsp_len, (FTP_PRINTF_IO_TIMEOUT == err) ? ftpprintf->param.rd_waitms : waitms);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, cmd \"\", %s\n", ftp_printf_strerr(err));
        return err;
    }
    rspcode = atoi(tmprsp);
    switch (rspcode) {
        case 226/* 数据传输完成 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            ERR("UNKNOWN state code: %d\n", rspcode);
            err = FTP_PRINTF_UNKNOWN_STATE_CODE;
            break;
    }
    
    return err;

}

static enum ftp_printf_errno ftp_chat_quit(struct ftp_printf_t *ftpprintf, int waitms)
{
	enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    int rspcode = 221;

    err = ftp_chat_QUIT(ftpprintf, &rspcode, waitms);
    switch (err) {
        case FTP_PRINTF_SUCC:
        case FTP_PRINTF_QUIT_FAIL:
            /* 如果执行QUIT失败, 可以直接执行关闭ctrl socket的操作 */
            break;
        default:
            return err;
    }

	return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_STATE_INIT_do(struct ftp_printf_t *ftpprintf)
{
    struct sockaddr_in dstaddr;
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;
    PACKET_PARAM_T packparam;
    PACK_ERR packerr = PACK_SUCC;
    char rsp[128] = {0};
    unsigned int rsplen = 0;
    int code;

    SAFE_CLOSE(ftpprintf->datafd);
	if (-1 != ftpprintf->ctrlfd) {
		ftp_chat_quit(ftpprintf, 6 * 1000);
		close(ftpprintf->ctrlfd);
		ftpprintf->ctrlfd = -1;
	}

	if (-1 != ftpprintf->packid) {
        if (-1 == packet_close(ftpprintf->packid, &packerr)) {
            WARN("FAIL to close packet, packid = %d, %s\n", ftpprintf->packid, packet_strerr(packerr));
        }
        ftpprintf->packid = -1;
    }

    /* 创建TCP连接 */
    ftpprintf->ctrlfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == ftpprintf->ctrlfd) {
        ERR("FAIL to create tcp socket for ctrlfd, %s\n", strerror(errno));
        err = FTP_PRINTF_CREATE_SOCKET_FAIL;
        goto err_exit;
    }

    /* 打开抓包分析模块 */
    memset((char *)&packparam, 0, sizeof (packparam));
    packparam.uBuffSize = 4 * 1024;
    packparam.iStreamFd = ftpprintf->ctrlfd;
    packparam.iWakeUpFd = ftpprintf->wakeupfds[0];
    packparam.packet_ana = ftp_printf_ctrlfd_packet_ana;
    ftpprintf->packid = packet_open(&packparam, &packerr);
    if (-1 == ftpprintf->packid) {
        ERR("FAIL to open packet for ftp printf, %s\n", packet_strerr(packerr));
        err = FTP_PRINTF_PACKET_OPEN_FAIL;
        FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_EXIT);
        goto err_exit;
    }

    /* 与ftp服务器建立控制链路 */
    memset((char *)&dstaddr, 0, sizeof (dstaddr));
    dstaddr.sin_family = AF_INET;
    dstaddr.sin_addr.s_addr = hostGetByName(ftpprintf->param.addr); /* inet_addr 使用类似hostGetByName兼容域名 */
    dstaddr.sin_port = htons(ftpprintf->param.port);
    err = ftp_printf_tcp_connect(ftpprintf->ctrlfd, ftpprintf->wakeupfds[0], &dstaddr, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        DBG("FAIL to connect ftp server: %s:%d, %s\n", ftpprintf->param.addr, ftpprintf->param.port, ftp_printf_strerr(err));
        goto err_exit;
    }

    /* 等待FTP服务器回应220(服务就绪) */
    err = ftp_chat(ftpprintf, NULL, rsp, sizeof (rsp), &rsplen, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat, %s\n", ftp_printf_strerr(err));
        goto err_exit;
    }

    /* 对ftp服务器的回应进行分析 */
    code = atoi(rsp);
    switch (code) {
        case 220/* FTP服务就绪 */:
            err = FTP_PRINTF_SUCC;
            break;
        default:
            WARN("UNKNOWN ftp state code: %d, SET err => %s\n", code, ftp_printf_strerr(FTP_PRINTF_SVR_NOT_READY));
            err = FTP_PRINTF_SVR_NOT_READY;
            goto err_exit;
    }

	ftpprintf->attachtime = 0;

    FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_LOGIN);
    ftpprintf->conti_state_count = 0;
    return FTP_PRINTF_SUCC;

err_exit:

    if (STATE_INIT == ftpprintf->state) {
        ftpprintf->conti_state_count++;
    }
    else {
        ftpprintf->conti_state_count = 0;
    }

    if (-1 != ftpprintf->packid) {
        if (-1 == packet_close(ftpprintf->packid, &packerr)) {
            WARN("FAIL to close packet for ftp printf, %s\n", packet_strerr(packerr));
        }
        ftpprintf->packid = -1;
    }

    SAFE_CLOSE(ftpprintf->ctrlfd);
    
    return err;

}

static enum ftp_printf_errno ftp_printf_STATE_LOGIN_do(struct ftp_printf_t *ftpprintf)
{
    enum ftp_printf_errno err;

    err = ftp_chat_login(ftpprintf, ftpprintf->param.user, ftpprintf->param.pass, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_SYST);
    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_STATE_SYST_do(struct ftp_printf_t *ftpprintf)
{
    enum ftp_printf_errno err;
    int rspcode;

    err = ftp_chat_SYST(ftpprintf, &rspcode, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_SYST, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return err;
    }

    FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_TYPE);
    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_STATE_TYPE_do(struct ftp_printf_t *ftpprintf)
{
	enum ftp_printf_errno err;
    int rspcode;

    err = ftp_chat_TYPE(ftpprintf, "I", &rspcode, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        ERR("FAIL to do ftp_chat_TYPE, rspcode = %d, %s\n", rspcode, ftp_printf_strerr(err));
        return err;
    }

    FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_CWD);
    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_STATE_CWD_do(struct ftp_printf_t *ftpprintf)
{
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;

    if ('\0' != ftpprintf->param.dir[0]) {
        err = ftp_chat_mkdir_cd_recusive(ftpprintf, ftpprintf->param.dir, 16 * 1000);
        if (FTP_PRINTF_SUCC != err) {
            return err;
        }
    }

	FTP_PRINTF_STATE_SET(ftpprintf->state, ftpprintf->sync ? STATE_SEND : STATE_DATA);
    
    return err;

}

static enum ftp_printf_errno ftp_printf_STATE_DATA_do(struct ftp_printf_t *ftpprintf)
{
    enum ftp_printf_errno err = FTP_PRINTF_SUCC;

    /* 从chatfd获取一段用户要上传的数据 */
    err = ftp_printf_tcp_read_v2(ftpprintf->chatfds[0], ftpprintf->wakeupfds[0], NULL, 0, &ftpprintf->datalen, 3000);
    switch (err) {
        case FTP_PRINTF_SUCC:
            FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_SEND);
            break;
        case FTP_PRINTF_IO_TIMEOUT:
            /* TIMEOUT表示没有用户数据发过来, 此时应该继续等待 */
            return FTP_PRINTF_SUCC;
        default:
            break;
    }

    return err;

}

static enum ftp_printf_errno ftp_printf_STATE_SEND_do(struct ftp_printf_t *ftpprintf)
{
	char filename[128] = {0};
    enum ftp_printf_errno err;
    char rsp[256] = {0};
    unsigned int rsplen;

	/* 如果需要在文件名与后缀名之前添加当前时间信息, 添加完之后将标志位清0 */
	if (ftpprintf->param.multifile) {
		if (0 == ftpprintf->attachtime) {
			ftpprintf->attachtime = time(NULL);
		}
		ftp_printf_filename_attachtime(ftpprintf->param.filename, ftpprintf->attachtime, filename, sizeof (filename));
	}
	else {
		SAFE_STRNCPY(filename, ftpprintf->param.filename, sizeof (filename));
	}

    err = ftp_chat_nlist(ftpprintf, filename, rsp, sizeof (rsp), &rsplen, 16 * 1000);
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

    if (NULL == strstr(rsp, filename)) {
        err = ftp_chat_put(ftpprintf, filename, 120 * 1000/* 部分ftp服务器对文件占用时间较长, STOR回应会较慢, 这里写成2分钟 */);
    }
    else {
        err = ftp_chat_append(ftpprintf, filename, 120 * 1000/* 部分ftp服务器对文件占用时间较长, APPE回应会较慢, 这里写成2分钟 */);
    }
    if (FTP_PRINTF_SUCC != err) {
        return err;
    }

	if (!ftpprintf->sync) {
		FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_DATA);
	}

    return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_STATE_EXIT_do(struct ftp_printf_t *ftpprintf)
{
    SAFE_CLOSE(ftpprintf->datafd);

	ftp_chat_quit(ftpprintf, 6 * 1000);
	
    SAFE_CLOSE(ftpprintf->ctrlfd);

    return FTP_PRINTF_EXIT;

}

static enum ftp_printf_errno ftp_printf_state_last_do(struct ftp_printf_t *ftpprintf)
{
    ERR("UNKNOWN state: %d, PLEASE CHECK YOUR CODE!!!\n", ftpprintf->state);

    FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_EXIT);
    return FTP_PRINTF_SUCC;

}

static struct ftp_printf_state_do_t sg_ftpprintf_state_do_tab[] = {
    {STATE_INIT, "[INIT]初始状态, 为控制链路建立TCP连接", ftp_printf_STATE_INIT_do},
    {STATE_LOGIN, "[LOGIN]登录FTP服务器", ftp_printf_STATE_LOGIN_do},
    {STATE_SYST, "[SYST]获取FTP服务器操作系统类型", ftp_printf_STATE_SYST_do},
    {STATE_TYPE, "[TYPE]切换数据传输模式", ftp_printf_STATE_TYPE_do},
    {STATE_CWD, "[CWD]进入指定目录", ftp_printf_STATE_CWD_do},
    {STATE_DATA, "[DATA]获取待发送的数据", ftp_printf_STATE_DATA_do},
    {STATE_SEND, "[SEND]发送数据", ftp_printf_STATE_SEND_do},
    {STATE_EXIT, "[EXIT]关闭控制链路, 退出FTP服务器", ftp_printf_STATE_EXIT_do},
    {state_last, "[last]默认处理逻辑", ftp_printf_state_last_do},
};

static enum ftp_printf_errno ftp_printf_state_do_tab_check(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sg_ftpprintf_state_do_tab); i++) {
        if (NULL == sg_ftpprintf_state_do_tab[i].state_do) {
            return FTP_PRINTF_NO_ROUTINE_SPECIFIED;
        }
    }

    return FTP_PRINTF_SUCC;

}

static inline enum ftp_printf_errno ftp_printf_sleepms(struct ftp_printf_t *ftpprintf, int sleepms)
{
    fd_set rset;
    int ret;
    struct timeval timeout;
    struct timeval *ptimeout = NULL;

    FD_ZERO(&rset);
    FD_SET(ftpprintf->wakeupfds[0], &rset);

    if (sleepms < 0) {
        ptimeout = NULL;
    }
    else if (0 == sleepms) {
        return FTP_PRINTF_SUCC;
    }
    else {
        ptimeout = &timeout;
        ptimeout->tv_sec = sleepms / 1000;
        ptimeout->tv_usec = (sleepms % 1000) * 1000;
    }

    do {
        ret = select(ftpprintf->wakeupfds[0] + 1, &rset, NULL, NULL, ptimeout);
    } while (-1 == ret && EINTR == errno);
    if (-1 == ret) {
        return FTP_PRINTF_SELECT_FAIL;
    }
    if (0 == ret) {
        /* 成功延时sleepms的时间, 故此处返回成功 */
        return FTP_PRINTF_SUCC;
    }

    if (FD_ISSET(ftpprintf->wakeupfds[0], &rset)) {
        return FTP_PRINTF_IO_WAKEUP;
    }

    return FTP_PRINTF_SELECT_FAIL;

}

static void * ftp_printf_main_thread(void *arg)
{
	struct ftp_printf_t *ftpprintf = (struct ftp_printf_t *)arg;
	enum ftp_printf_errno err = FTP_PRINTF_SUCC;
	int sleepms = 0;
    char databuff[MAX_FTP_DATA_BUFF_SIZE] = {0};
    int i;

	prctl(PR_SET_NAME, ftpprintf->threadname);

    ftpprintf->databuff = databuff;
    ftpprintf->databuff_size = sizeof (databuff);
    ftpprintf->datalen = 0;

	/* 建立FTP控制链路, 并从chat通道中获取待发送的数据发往FTP服务器 */
	for (; ;) {
		for (i = 0; i < (ARRAY_SIZE(sg_ftpprintf_state_do_tab) - 1); i++) {
            if (ftpprintf->state == sg_ftpprintf_state_do_tab[i].state) {
                break;
            }
        }
        err = sg_ftpprintf_state_do_tab[i].state_do(ftpprintf);
        switch (err) {
            case FTP_PRINTF_EXIT:
                INFO("THREAD \"%s\" exit\n", ftpprintf->threadname);
                goto exit;
            /* 这里可以增对对不同错误码不同状态下的特殊处理 */
            default:
                /* 默认通用处理 */
                switch (ftp_printf_errno_type_get(err)) {
                    case FTP_PRINTF_ERRNO_TYPE_SUCC:
                        sleepms = 0;
                        break;
                    case FTP_PRINTF_ERRNO_TYPE_TRY_AGAIN:
                        sleepms = 3000;
                        FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_INIT);
                        break;
                    case FTP_PRINTF_ERRNO_TYPE_ABORT:
                        sleepms = 0;
                        FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_EXIT);
                        break;
                    default:
                        ERR("UNKNOWN ftp printf errno type: %d\n", ftp_printf_errno_type_get(err));
                        sleepms = 0;
                        FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_EXIT);
                        break;
                }
                break;
        }     
        
        /* 延时一段时间 */
        if (FTP_PRINTF_SUCC != ftp_printf_sleepms(ftpprintf, sleepms)) {
            FTP_PRINTF_STATE_SET(ftpprintf->state, STATE_EXIT);
        }
	}

exit:
    ftpprintf->tid = -1;
    
	return NULL;

}

static enum ftp_printf_errno ftp_printf_sync_wait(struct ftp_printf_t *ftpprintf, int wait_ms)
{
	fd_set rset;
	int ret = -1;
	struct timeval timewait = {16, 0};
	struct timeval *p_timewait = NULL;
	enum ftp_printf_errno err = FTP_PRINTF_SUCC;

	FD_ZERO(&rset);
	FD_SET(ftpprintf->chatfds[1], &rset);
	if (wait_ms < 0) {
		p_timewait = NULL;
	}
	else {
		p_timewait = &timewait;
		p_timewait->tv_sec = wait_ms / 1000;
		p_timewait->tv_usec = (wait_ms % 1000) * 1000;
	}

	do {
		ret = select(ftpprintf->chatfds[1] + 1, &rset, NULL, NULL, p_timewait);
	} while ((-1 == ret) && (EINTR == errno));
	if (-1 == ret) {
		ERR("FAIL to select for read, %s\n", strerror(errno));
		return FTP_PRINTF_SELECT_FAIL;
	}
	else if (0 == ret) {
		return FTP_PRINTF_IO_TIMEOUT;
	}

	if (!FD_ISSET(ftpprintf->chatfds[1], &rset)) {
		ERR("FAIL to FD_ISSET for read, %s\n", strerror(errno));
		return FTP_PRINTF_SELECT_FAIL;
	}

	do {
		ret = read(ftpprintf->chatfds[1], &err, sizeof (err));
	} while (-1 == ret && EINTR == errno);
	if (-1 == ret) {
		ERR("FAIL to read, %s\n", strerror(errno));
		return FTP_PRINTF_READ_FAIL;
	}
	
	return err;

}

static enum ftp_printf_errno ftp_printf_main(struct ftp_printf_t *ftpprintf)
{
	int ret = 0;

	if (-1 == ftp_printf_pthread_id_verify(ftpprintf->tid)) {
		ret = pthread_create(&(ftpprintf->tid), NULL, ftp_printf_main_thread, ftpprintf);
		if (0 != ret) {
			ERR("FAIL to create thread \"ftp_printf_main_thread\", ret = %d(%s)\n", ret, strerror(ret));
			return FTP_PRINTF_CREATE_THREAD_FAIL;
		}
	}

	if (ftpprintf->param.sync) {
		if (0 == ftpprintf->param.sync_waitms) {
			return FTP_PRINTF_SUCC;
		}
		return ftp_printf_sync_wait(ftpprintf, ftpprintf->param.sync_waitms);
	}

	return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno _ftp_printf_status(struct ftp_printf_t *ftpprintf, struct ftp_printf_status_t *status)
{
	status->state = ftpprintf->state;
	status->ctrlfd = ftpprintf->ctrlfd;
	status->datafd = ftpprintf->datafd;
	status->sending = ftpprintf->sending;

	return FTP_PRINTF_SUCC;

}

static enum ftp_printf_errno ftp_printf_fini(struct ftp_printf_t * ftpprintf)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;

	if (NULL == ftpprintf) {
		ERR("INVALID input param, ftpprintf = %p\n", ftpprintf);
		return FTP_PRINTF_INVALID_INPUT;
	}

	e = _ftp_printf_fini(ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		ERR("FAIL to do _ftp_printf_fini, %s\n", ftp_printf_strerr(e));
		return e;
	}

	pthread_mutex_lock(&(sg_ftpprintfs.mutex));
	{
		ftpprintf->inuse = 0;
	}
	pthread_mutex_unlock(&(sg_ftpprintfs.mutex));

	return e;

}

#ifdef __cplusplus
extern "C" {
#endif

int ftp_printf_open(const struct ftp_printf_param_t *param, enum ftp_printf_errno *err)
{
    enum ftp_printf_errno e = FTP_PRINTF_SUCC;
	struct ftp_printf_t *ftpprintf = NULL;

    e = ftp_printf_check(param);
    if (FTP_PRINTF_SUCC != e) {
        FTP_PRINTF_ERRNO_SET(err, e);
        return -1;
    }
	
    e = ftp_printf_init(param, &ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		FTP_PRINTF_ERRNO_SET(err, e);
        return -1;
	}

	e = ftp_printf_main(ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		FTP_PRINTF_ERRNO_SET(err, e);
        goto err_exit;	
	}

	FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_SUCC);
	return ftp_printf_to_id(ftpprintf);

err_exit:

	if (NULL != ftpprintf) {
		(void)ftp_printf_fini(ftpprintf);
		ftpprintf = NULL;
	}
	
    return -1;

}

int ftp_printf(int id, const char *format, ...)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;
	struct ftp_printf_t *ftpprintf = NULL;
	va_list args;
	int ret = -1;
	int len = -1;
	fd_set wset;
	fd_set rset;
	int nfds = -1;
	struct timeval timeout;
	struct timeval *ptimeout;

    if (-1 == id) {
        return -1;
    }

	e = id_to_ftp_printf(id, &ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		ERR("FAIL to do id_to_ftp_printf, %s\n", ftp_printf_strerr(e));
		return -1;
	}
	if (NULL == format) {
		ERR("INVALID input param, format = %p\n", format);
		return -1;
	}

	pthread_mutex_lock(&(ftpprintf->mutex));
	{
		if (!ftpprintf->inuse) {
			ERR("BAD ftp printf id: %d, %s\n", id, ftp_printf_strerr(FTP_PRINTF_BAD_ID));
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
		}
        if (-1 == ftp_printf_pthread_id_verify(ftpprintf->tid)) {
            DBG("ftp printf main thread not exist, id: %d\n", id);
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
        }

		va_start(args, format);
		do {
			FD_ZERO(&wset);
			FD_SET(ftpprintf->chatfds[1], &wset);
			FD_ZERO(&rset);
			FD_SET(ftpprintf->wakeupfds[0], &rset);
			nfds = max(ftpprintf->chatfds[1], ftpprintf->wakeupfds[0]);
			if (ftpprintf->param.wr_waitms < 0) {
				ptimeout = NULL;
			}
			else {
				ptimeout = &timeout;
				ptimeout->tv_sec = ftpprintf->param.wr_waitms / 1000;
				ptimeout->tv_usec = (ftpprintf->param.wr_waitms % 1000) * 1000;
			}
			do {
				ret = select(nfds + 1, &rset, &wset, NULL, ptimeout);
			} while (-1 == ret && EINTR == errno);
			if (-1 == ret) {
				ERR("FAIL to select for write, %s\n", strerror(errno));
				ret = -1;
				break;
			}
			else if (0 == ret) {
				DBG("TIMEOUT to write ftp printf data\n");
				ret = 0;
				break;
			}

			if (FD_ISSET(ftpprintf->wakeupfds[0], &rset)) {
				WARN("FTP PRINTF wakeup by wakeupfds[0]: %d\n", ftpprintf->wakeupfds[0]);
				ret = -1;
				break;
			}
			if (FD_ISSET(ftpprintf->chatfds[1], &wset)) {
				//memset(ftpprintf->printbuff, 0, MAX_FTP_PRINTF_BUFF_SIZE);
				len = vsnprintf(ftpprintf->printbuff, MAX_FTP_PRINTF_BUFF_SIZE, format, args);
                if (-1 == len) {
                    len = MAX_FTP_PRINTF_BUFF_SIZE;
                }
                else {
                    len = min(len, MAX_FTP_PRINTF_BUFF_SIZE);
                }
				do {
					ret = write(ftpprintf->chatfds[1], ftpprintf->printbuff, len);
				} while (-1 == ret && EINTR == errno);
				if (-1 == ret) {
					ERR("FAIL to write ftp printf data, %s\n", strerror(errno));
					break;
				}
			}
		} while (0);
		va_end(args);
	}
	pthread_mutex_unlock(&(ftpprintf->mutex));
	
    return ret;
    
}

int ftp_printf_bin_write(int id, const void *buff, unsigned int len, enum ftp_printf_errno *err)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;
	struct ftp_printf_t *ftpprintf = NULL;
	int ret = -1;
	fd_set wset;
	fd_set rset;
	int nfds = -1;
	struct timeval timeout;
	struct timeval *ptimeout;

	e = id_to_ftp_printf(id, &ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		ERR("FAIL to do id_to_ftp_printf, %s\n", ftp_printf_strerr(e));
		FTP_PRINTF_ERRNO_SET(err, e);
		return -1;
	}
	if (NULL == buff || 0 == len) {
		ERR("INVALID input param, buff = %p, len = %u\n", buff, len);
		FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_INVALID_INPUT);
		return -1;
	}

	pthread_mutex_lock(&(ftpprintf->mutex));
	{
		if (!ftpprintf->inuse) {
			ERR("BAD ftp printf id: %d, %s\n", id, ftp_printf_strerr(FTP_PRINTF_BAD_ID));
			FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_BAD_ID);
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
		}
        if (-1 == ftp_printf_pthread_id_verify(ftpprintf->tid)) {
            DBG("ftp printf main thread not exist, id: %d\n", id);
			FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_MAIN_THREAD_NOTEXIST);
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
        }

		do {
			FD_ZERO(&wset);
			FD_SET(ftpprintf->chatfds[1], &wset);
			FD_ZERO(&rset);
			FD_SET(ftpprintf->wakeupfds[0], &rset);
			nfds = max(ftpprintf->chatfds[1], ftpprintf->wakeupfds[0]);
			if (ftpprintf->param.wr_waitms < 0) {
				ptimeout = NULL;
			}
			else {
				ptimeout = &timeout;
				ptimeout->tv_sec = ftpprintf->param.wr_waitms / 1000;
				ptimeout->tv_usec = (ftpprintf->param.wr_waitms % 1000) * 1000;
			}
			do {
				ret = select(nfds + 1, &rset, &wset, NULL, ptimeout);
			} while (-1 == ret && EINTR == errno);
			if (-1 == ret) {
				ERR("FAIL to select for write, %s\n", strerror(errno));
				FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_SELECT_FAIL);
				ret = -1;
				break;
			}
			else if (0 == ret) {
				DBG("TIMEOUT to write ftp printf data\n");
				FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_IO_TIMEOUT);
				ret = 0;
				break;
			}

			if (FD_ISSET(ftpprintf->wakeupfds[0], &rset)) {
				WARN("FTP PRINTF wakeup by wakeupfds[0]: %d\n", ftpprintf->wakeupfds[0]);
				FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_IO_WAKEUP);
				ret = -1;
				break;
			}
			if (FD_ISSET(ftpprintf->chatfds[1], &wset)) {
				do {
					ret = write(ftpprintf->chatfds[1], buff, len);
				} while (-1 == ret && EINTR == errno);
				if (-1 == ret) {
					ERR("FAIL to write ftp printf data, %s\n", strerror(errno));
					FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_WRITE_FAIL);
					break;
				}
				if (len != ret) {
					WARN("WRITE data not complete, ret = %d, len = %u\n", ret, len);
					FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_WRITE_NOTCOMPLETE);
					break;
				}
				FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_SUCC);
			}
		} while (0);
	}
	pthread_mutex_unlock(&(ftpprintf->mutex));
	
    return ret;

}

int ftp_printf_file_upload(int id, const char *path, enum ftp_printf_errno *err)
{
    int ret = -1;
	int fd = -1;
	int total_len = 0;
	int total_sent_len = 0;
	char sendbuf[1024] = {0};
	int to_read_len = 0;
	int read_len = 0;
	int sent_len = 0;

	if (-1 == id || NULL == path || '\0' == path[0]) {
		ERR("INVALID input param, path = %p\n", path);
        FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_INVALID_INPUT);
		return -1;
	}

	/* 拷贝文件, 并上传 */
	fd = open(path, O_RDONLY, 0666);
	if (-1 == fd) {
		ERR("FAIL to open %s, %s\n", path, strerror(errno));
        FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_OPEN_FILE_FAILED);
        ret = -1;
		goto exit;	
	}

	total_len = lseek(fd, 0, SEEK_END);
	if (-1 == total_len) {
		ERR("FAIL to lseek %s, %s\n", path, strerror(errno));
        FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_LSEEK_FILE_FAILED);
        ret = -1;
		goto exit;
	}
	lseek(fd, 0, SEEK_SET);

	DBG("filesizeof (%s) = %d\n", path, total_len);

	total_sent_len = 0;
	while (total_sent_len < total_len) {
		to_read_len = min(total_len - total_sent_len, sizeof (sendbuf));
		do {
			read_len = read(fd, sendbuf, to_read_len);
		} while (-1 == read_len && EINTR == errno);
		if (-1 == read_len) {
			ERR("FAIL to read %s, read_len = %d, to_read_len = %d, %s\n", path, read_len, to_read_len, strerror(errno));
            FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_READ_FILE_FAILED);
            ret = -1;
			goto exit;
		}
		else if (0 == read_len) {
			DBG("READ %d length from %s, %s\n", read_len, path, strerror(errno));
            ret = 0;
			goto exit;
		}
		sent_len = ftp_printf_bin_write(id, sendbuf, read_len, err);
		if (-1 == sent_len) {
			ERR("FAIL to ftp bin write %d length file, sent_len = %d\n", read_len, sent_len);
            ret = -1;
			goto exit;
		}
		DBG("SUCC to sent %d data => ftp\n", sent_len);
		total_sent_len += sent_len;
	}
    ret = 0;  
exit:

	SAFE_CLOSE(fd);
	if (-1 == ftp_printf_close(id, NULL)) {
		WARN("FAIL to close ftp_printf, id = %d\n", id);
	}
	id = -1;

	return ret;

	
} 

int ftp_printf_status(int id, struct ftp_printf_status_t *status, enum ftp_printf_errno *err)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;
	struct ftp_printf_t *ftpprintf = NULL;

	e = id_to_ftp_printf(id, &ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		FTP_PRINTF_ERRNO_SET(err, e);
		return -1;
	}

	if (NULL == status) {
		FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_INVALID_INPUT);
		return -1;
	}

	pthread_mutex_lock(&(ftpprintf->mutex));
	{
		if (!ftpprintf->inuse) {
			ERR("BAD ftp printf id: %d\n", id);
			FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_BAD_ID);
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
		}
		e = _ftp_printf_status(ftpprintf, status);
	}
	pthread_mutex_unlock(&(ftpprintf->mutex));

	FTP_PRINTF_ERRNO_SET(err, e);

	return (FTP_PRINTF_SUCC == e) ? 0 : -1;

}

int ftp_printf_close(int id, enum ftp_printf_errno *err)
{
	enum ftp_printf_errno e = FTP_PRINTF_SUCC;
	struct ftp_printf_t *ftpprintf = NULL;

	e = id_to_ftp_printf(id, &ftpprintf);
	if (FTP_PRINTF_SUCC != e) {
		FTP_PRINTF_ERRNO_SET(err, e);
		return -1;
	}

	pthread_mutex_lock(&(ftpprintf->mutex));
	{
		if (!ftpprintf->inuse) {
			ERR("BAD ftp printf id: %d\n", id);
			FTP_PRINTF_ERRNO_SET(err, FTP_PRINTF_BAD_ID);
			pthread_mutex_unlock(&(ftpprintf->mutex));
			return -1;
		}
		e = ftp_printf_fini(ftpprintf);
	}
	pthread_mutex_unlock(&(ftpprintf->mutex));

	FTP_PRINTF_ERRNO_SET(err, e);

    return (FTP_PRINTF_SUCC == e) ? 0 : -1;

}

const char * ftp_printf_strerr(enum ftp_printf_errno err)
{
    return ftp_printf_strtype(sg_ftpprintf_errstr_tab, ARRAY_SIZE(sg_ftpprintf_errstr_tab), (int)err);

}

const char * ftp_printf_strerrtype(enum ftp_printf_errno err)
{
    return ftp_printf_strtype(sg_ftpprintf_errtypestr_tab, ARRAY_SIZE(sg_ftpprintf_errtypestr_tab), (int)err);

}

const char * ftp_printf_strstate(enum ftp_printf_state state)
{
	return ftp_printf_strtype(sg_ftpprintf_state_tab, ARRAY_SIZE(sg_ftpprintf_state_tab), (int)state);
	
}

void ftp_printf_dbg_set(int enable)
{
    ftpprintf_dbg_enable = enable;
    return;

}

void TEST_ftp_printf_bin_write(const char *path)
{
	struct ftp_printf_param_t param;
	char timestr[32] = {0};
	enum ftp_printf_errno err = FTP_PRINTF_SUCC;
	int id = -1;
	int fd = -1;
	int total_len = 0;
	int total_sent_len = 0;
	char sendbuf[1024] = {0};
	int to_read_len = 0;
	int read_len = 0;
	int sent_len = 0;

	if (NULL == path || '\0' == path[0]) {
		ERR("INVALID input param, path = %p\n", path);
		return;
	}

	memset((char *)&param, 0, sizeof (param));
	SAFE_STRNCPY(param.addr, "10.10.96.35", sizeof (param.addr));
	param.port = 21;
	SAFE_STRNCPY(param.user, "target", sizeof (param.user));
	SAFE_STRNCPY(param.pass, "target", sizeof (param.pass));
	SAFE_STRNCPY(param.dir, "bin", sizeof (param.dir));
	snprintf(param.filename, sizeof (param.filename), "bin_data_%s", strtime(time(NULL), timestr, sizeof (timestr)));
	param.multifile = 0;
	param.wr_waitms = 3000;
	param.rd_waitms = 3000;
	param.sync = 1;
	param.sync_waitms = 16 * 1000;

	id = ftp_printf_open(&param, &err);
	if (-1 == id) {
		ERR("FAIL to do ftp_printf open, %s\n", ftp_printf_strerr(err));
		return;
	}

	/* 拷贝文件, 并上传 */
	fd = open(path, O_RDONLY, 0666);
	if (-1 == fd) {
		ERR("FAIL to open %s, %s\n", path, strerror(errno));
		goto exit;	
	}

	total_len = lseek(fd, 0, SEEK_END);
	if (-1 == total_len) {
		ERR("FAIL to lseek %s, %s\n", path, strerror(errno));
		goto exit;
	}
	lseek(fd, 0, SEEK_SET);

	DBG("filesizeof (%s) = %d\n", path, total_len);

	total_sent_len = 0;
	while (total_sent_len < total_len) {
		to_read_len = min(total_len - total_sent_len, sizeof (sendbuf));
		do {
			read_len = read(fd, sendbuf, to_read_len);
		} while (-1 == read_len && EINTR == errno);
		if (-1 == read_len) {
			ERR("FAIL to read %s, read_len = %d, to_read_len = %d, %s\n", path, read_len, to_read_len, strerror(errno));
			goto exit;
		}
		else if (0 == read_len) {
			DBG("READ %d length from %s, %s\n", read_len, path, strerror(errno));
			goto exit;
		}
		sent_len = ftp_printf_bin_write(id, sendbuf, read_len, &err);
		if (-1 == sent_len) {
			ERR("FAIL to ftp bin write %d length file, sent_len = %d, %s\n", read_len, sent_len, ftp_printf_strerr(err));
			goto exit;
		}
		DBG("SUCC to sent %d data => ftp://%s:%d/%s/%s\n", sent_len, param.addr, param.port, param.dir, param.filename);
		total_sent_len += sent_len;
	}

exit:

	SAFE_CLOSE(fd);
	
	if (-1 == ftp_printf_close(id, &err)) {
		WARN("FAIL to close ftp_printf, id = %d, %s\n", id, ftp_printf_strerr(err));
	}
	id = -1;

	return;

	
} 

#ifdef __cplusplus
}
#endif

