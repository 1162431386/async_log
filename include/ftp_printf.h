#ifndef _FTP_PRINTF_H_
#define _FTP_PRINTF_H_

struct ftp_printf_param_t {
    char addr[32]; /* FTP服务器地址或域名 */
    unsigned short port; /* FTP服务器端口, 默认21 */
    char user[32]; /* FTP服务器用户名 */
    char pass[32]; /* FTP服务器密码 */
    char dir[128]; /* 待上传的日志文件所要上传的那个目录, 举例: /a/b/c/d/e/f 表示在指定用户根目录下先进入或创建/a/b/c/d/e/f 该参数为""时表示上传到指定用户根目录 */
    char filename[128]; /* 待上传的日志文件名 */
	int multifile; /* 网络断开重连时是否需要生成新文件, 1: 生成新文件, 通过在文件扩展名前加时间区分, 0: 继续使用原始文件, 根据不同ftp服务器特性, 重连需要等待的时间较长, 默认0 */

	/* 以下是性能参数 */
	int wr_waitms; /* 写超时等待时间, 单位: 毫秒, -1: 一直等待, 0: 不等待, 建议值3000 */
	int rd_waitms; /* 读超时等待时间, 单位: 毫秒, -1: 一直等待, 0: 不等待, 建议值3000 */

	/* 以下是同步等待ftp交互成功相关参数 */
	int sync; /* ftp_printf_open接口是否同步等待ftp可以上传数据 */
	int sync_waitms; /* 同步等待的超时时间, 单位: 毫秒, -1: 一直等待, 0: 不等待, 建议值3000 */
};

/* 200 ~ 299 之间的错误码表示成功, succ */
/* 300 ~ 399 之间的错误码表示失败, 但该失败只是当前这次失败, 可以再次尝试, you can try again */
/* 400 ~ 499 之间的错误码表示彻底失败, 已没有再尝试的必要, 需要放弃, you have to abort */

enum ftp_printf_errno {
    FTP_PRINTF_SUCC = 200,
    FTP_PRINTF_INVALID_INPUT = 400,
    FTP_PRINTF_INVALID_PARAM = 401,
    FTP_PRINTF_NO_RESOURCES = 300,
    FTP_PRINTF_SOCKETPAIR_FAIL = 301,
    FTP_PRINTF_INVALID_ID = 402,
    FTP_PRINTF_BAD_ID = 302,
    FTP_PRINTF_RESOURCES_NOT_INITED = 303,
    FTP_PRINTF_PTHREAD_COND_WAIT_FAIL = 304,
    FTP_PRINTF_OUT_OF_MEMORY = 403,
    FTP_PRINTF_CREATE_THREAD_FAIL = 404,
    FTP_PRINTF_IO_TIMEOUT = 305,
    FTP_PRINTF_IO_WAKEUP = 405, /* 该错误码一般发生在用户主动断开, 是主动放弃, 需要abort */
    FTP_PRINTF_NO_ROUTINE_SPECIFIED = 406,
    FTP_PRINTF_EXIT = 407,
    FTP_PRINTF_SELECT_FAIL = 306,
    FTP_PRINTF_CREATE_SOCKET_FAIL = 307,
    FTP_PRINTF_GETSOCKOPT_FAIL = 308,
    FTP_PRINTF_IO_ERROR = 309,
    FTP_PRINTF_CONNECT_FAIL = 310,
    FTP_PRINTF_PACKET_OPEN_FAIL = 311,
    FTP_PRINTF_PACKET_READ_FAIL = 312,
    FTP_PRINTF_SVR_NOT_READY = 313,
    FTP_PRINTF_WRITE_FAIL = 314,
    FTP_PRINTF_UNKNOWN_STATE_CODE = 408, /* 对于代码中未能识别的错误码, 需要停止而不是继续, 因为继续可能引发不可预知的错误 */
    FTP_PRINTF_STATE_CODE_MISMATCH = 315,
    FTP_PRINTF_READ_FAIL = 316,
    FTP_PRINTF_PASSIVE_PARAM_PARSE_FAIL = 409,
    FTP_PRINTF_NO_SUCH_DIR = 317,
    FTP_PRINTF_DIR_ALREADY_EXIST = 318,
    FTP_PRINTF_NO_SUCH_FILE = 319,
    FTP_PRINTF_QUIT_FAIL = 320,
    FTP_PRINTF_PEER_SHUTDOWN = 321,
    FTP_PRINTF_MAIN_THREAD_NOTEXIST = 409,
    FTP_PRINTF_WRITE_NOTCOMPLETE = 322,
    FTP_PRINTF_OPEN_FILE_FAILED = 323,
    FTP_PRINTF_LSEEK_FILE_FAILED = 324,
    FTP_PRINTF_READ_FILE_FAILED = 325,
};

enum ftp_printf_errno_type {
    FTP_PRINTF_ERRNO_TYPE_SUCC = 0, /* 对应2xx错误码 */
    FTP_PRINTF_ERRNO_TYPE_TRY_AGAIN = 1, /* 对应3xx错误码 */
    FTP_PRINTF_ERRNO_TYPE_ABORT = 2, /* 对应4xx错误码 */
};

enum ftp_printf_state {
	STATE_INIT =0,
	STATE_LOGIN = 1, /* 登录服务器 */
	STATE_SYST = 2, /* 获取ftp服务器操作系统类型 */
	STATE_TYPE = 3, /* 设置传输模式 */
	STATE_CWD = 4, /* 进入指定目录 */
	STATE_DATA = 5, /* 获取数据 */
	STATE_SEND = 6, /* 发送数据 */
	STATE_EXIT = 7,

    state_last = 0xffff, /* 该状态必须放在最后 */
};

struct ftp_printf_status_t {
	enum ftp_printf_state state;
	int ctrlfd; /* ftp控制链路sockfd */
	int datafd; /* ftp数据链路sockfd */ 
	int sending; /* 是否正在发数据 */
};

#ifdef __cplusplus
extern "C" {
#endif

int ftp_printf_open(const struct ftp_printf_param_t *param, enum ftp_printf_errno *err);

int ftp_printf(int id, const char *format, ...);

int ftp_printf_bin_write(int id, const void *buff, unsigned int len, enum ftp_printf_errno *err);

int ftp_printf_file_upload(int id, const char *path, enum ftp_printf_errno *err);

int ftp_printf_status(int id, struct ftp_printf_status_t *status, enum ftp_printf_errno *err);

int ftp_printf_close(int id, enum ftp_printf_errno *err);

const char * ftp_printf_strerr(enum ftp_printf_errno err);

const char * ftp_printf_strerrtype(enum ftp_printf_errno err);

const char * ftp_printf_strstate(enum ftp_printf_state state);

void ftp_printf_dbg_set(int enable);

#ifdef __cplusplus
}
#endif

#endif


