#ifndef _FTP_PRINTF_H_
#define _FTP_PRINTF_H_

struct ftp_printf_param_t {
    char addr[32]; /* FTP��������ַ������ */
    unsigned short port; /* FTP�������˿�, Ĭ��21 */
    char user[32]; /* FTP�������û��� */
    char pass[32]; /* FTP���������� */
    char dir[128]; /* ���ϴ�����־�ļ���Ҫ�ϴ����Ǹ�Ŀ¼, ����: /a/b/c/d/e/f ��ʾ��ָ���û���Ŀ¼���Ƚ���򴴽�/a/b/c/d/e/f �ò���Ϊ""ʱ��ʾ�ϴ���ָ���û���Ŀ¼ */
    char filename[128]; /* ���ϴ�����־�ļ��� */
	int multifile; /* ����Ͽ�����ʱ�Ƿ���Ҫ�������ļ�, 1: �������ļ�, ͨ�����ļ���չ��ǰ��ʱ������, 0: ����ʹ��ԭʼ�ļ�, ���ݲ�ͬftp����������, ������Ҫ�ȴ���ʱ��ϳ�, Ĭ��0 */

	/* ���������ܲ��� */
	int wr_waitms; /* д��ʱ�ȴ�ʱ��, ��λ: ����, -1: һֱ�ȴ�, 0: ���ȴ�, ����ֵ3000 */
	int rd_waitms; /* ����ʱ�ȴ�ʱ��, ��λ: ����, -1: һֱ�ȴ�, 0: ���ȴ�, ����ֵ3000 */

	/* ������ͬ���ȴ�ftp�����ɹ���ز��� */
	int sync; /* ftp_printf_open�ӿ��Ƿ�ͬ���ȴ�ftp�����ϴ����� */
	int sync_waitms; /* ͬ���ȴ��ĳ�ʱʱ��, ��λ: ����, -1: һֱ�ȴ�, 0: ���ȴ�, ����ֵ3000 */
};

/* 200 ~ 299 ֮��Ĵ������ʾ�ɹ�, succ */
/* 300 ~ 399 ֮��Ĵ������ʾʧ��, ����ʧ��ֻ�ǵ�ǰ���ʧ��, �����ٴγ���, you can try again */
/* 400 ~ 499 ֮��Ĵ������ʾ����ʧ��, ��û���ٳ��Եı�Ҫ, ��Ҫ����, you have to abort */

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
    FTP_PRINTF_IO_WAKEUP = 405, /* �ô�����һ�㷢�����û������Ͽ�, ����������, ��Ҫabort */
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
    FTP_PRINTF_UNKNOWN_STATE_CODE = 408, /* ���ڴ�����δ��ʶ��Ĵ�����, ��Ҫֹͣ�����Ǽ���, ��Ϊ����������������Ԥ֪�Ĵ��� */
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
    FTP_PRINTF_ERRNO_TYPE_SUCC = 0, /* ��Ӧ2xx������ */
    FTP_PRINTF_ERRNO_TYPE_TRY_AGAIN = 1, /* ��Ӧ3xx������ */
    FTP_PRINTF_ERRNO_TYPE_ABORT = 2, /* ��Ӧ4xx������ */
};

enum ftp_printf_state {
	STATE_INIT =0,
	STATE_LOGIN = 1, /* ��¼������ */
	STATE_SYST = 2, /* ��ȡftp����������ϵͳ���� */
	STATE_TYPE = 3, /* ���ô���ģʽ */
	STATE_CWD = 4, /* ����ָ��Ŀ¼ */
	STATE_DATA = 5, /* ��ȡ���� */
	STATE_SEND = 6, /* �������� */
	STATE_EXIT = 7,

    state_last = 0xffff, /* ��״̬���������� */
};

struct ftp_printf_status_t {
	enum ftp_printf_state state;
	int ctrlfd; /* ftp������·sockfd */
	int datafd; /* ftp������·sockfd */ 
	int sending; /* �Ƿ����ڷ����� */
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


