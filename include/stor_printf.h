
/**@file stor_printf.h
 * @note HangZhou Hikvision System Technology Co., Ltd. All Right Reserved.
 * @brief  ͨ��Ԥ�����ļ���ϴ洢���ʵ�ּ�¼��ӡ��Ϣ�Ľӿ�����
 * 
 * @author   xujian
 * @date     2019-11-27
 * @version  V1.0.0
 * 
 * @note ///Description here 
 * @note History:        
 * @note     <author>   <time>    <version >   <desc>
 * @note  
 * @warning  
 */

#ifndef _STOR_PRINTF_H_
#define _STOR_PRINTF_H_

/* stor_printfģ����� */
struct stor_printf_param_t {
    char filename[32]; /* ����洢���ݵ�sp�ļ������� */

    unsigned int sp_count; /* �����洢������sp�ļ����� */
    unsigned int sp_datasize; /* ����sp�ļ���������С, ��λ: �ֽ� */   
    int sp_prealloc; /* sp�ļ��Ƿ�Ԥ���� */
    int sp_head_sync_inter; /* sp�ļ�ͷͬ������ʱ����, ��λ: ��, -1: �����ó�ʱͬ��, 0: д������ʱ����ͬ��, > 0: ����ָ��ʱ������ͬ��, ����Ĭ��ֵ30�� */

    int wr_waitms; /* stor_printf�ӿ�д�볬ʱʱ��, ��λ: ���� */
    int diskpart_min_keepsize; /* ����ÿ����������sp�ļ�ʱ, ��С�����ռ�, ��λ: �ֽ�, Ĭ��32*1024*1024, �ò���Ϊ0ʱ, ȡĬ��ֵ32*1024*1024, -1��ʾ�����ж� */
};

/* stor_printf������ */
enum STOR_PRINTF_ERRNO {
    STOR_PRINTF_SUCC = 200, /* �ɹ� */
    STOR_PRINTF_SEARCH_COMPLETE = 201, /* ������� */

    STOR_PRINTF_NO_RESOURCES = 300, /* ��Դ��ʱ������ */
    STOR_PRINTF_RESOURCES_UNINITILISED = 301, /* ��Դ��δ��ʼ�� */
    STOR_PRINTF_IO_WAKEUP = 302,    
    STOR_PRINTF_WRITE_PARTLY = 303, /* �ļ�����д�� */
    STOR_PRINTF_READ_PARTLY = 304, /* �ļ����ֶ�ȡ */   
    STOR_PRINTF_TIMEOUT = 305, /* ������ʱ */
    STOR_PRINTF_NO_AVAILABLE_SP_FILE = 306, /* û�п��õ�sp�ļ� */
    STOR_PRINTF_SP_FILE_FULL = 307, /* sp�ļ��� */
    STOR_PRINTF_CREATE_DIR_FAIL = 308, /* ����Ŀ¼ʧ�� */
    STOR_PRINTF_SPACE_NOT_ENOUGH = 309, /* ���̴洢�ռ䲻�� */
    STOR_PRINTF_SP_FILELENGTH_MISMATCH = 310, /* sp�ļ��������û��趨�Ĳ�һ�� */

    STOR_PRINTF_INVALID_INPUT = 400, /* �Ƿ�������� */
    STOR_PRINTF_INVALID_ARGS = 401, /* �Ƿ����� */
    STOR_PRINTF_PTHREAD_CREATE_FAIL = 402, /* �����߳�ʧ�� */
    STOR_PRINTF_INVALID_ID = 403, /* �����id */
    STOR_PRINTF_BAD_ID = 404, /* ��Ч��id */
    STOR_PRINTF_OUT_OF_MEMORY = 405, /* �ڴ治��, �����ڴ�ʧ�� */
    STOR_PRINTF_SOCKETPAIR_FAIL = 406, /* ����socketpaitʧ�� */
    STOR_PRINTF_SELECT_FAIL = 407, /* selectϵͳ���÷���ʧ�� */
    STOR_PRINTF_FILE_CREAT_FAIL = 408, /* �����ļ�ʧ�� */
    STOR_PRINTF_FILE_LSEEK_FAIL = 409, /* �ļ�lseekʧ�� */
    STOR_PRINTF_WRITE_FAIL = 410, /* �ļ�д����ʧ�� */
    STOR_PRINTF_READ_FAIL = 411, /* �ļ�������ʧ�� */
    STOR_PRINTF_CHECKSUM_FAIL = 412, /* ����ͼ��ʧ�� */
    STOR_PRINTF_INVALID_MAGIC = 413, /* sp�ļ�magic�ֶβ����� */
    STOR_PRINTF_SP_BOTH_HEAD_ERROR = 414, /* sp�ļ�����ͷ������ */
    STOR_PRINTF_IO_ERROR = 415, /* δ֪IO���� */
    STOR_PRINTF_FILE_OPEN_FAIL = 416, /* ���ļ�ʧ�� */
    STOR_PRINTF_FILE_WRITE_FAIL = 417, /* д�ļ�ʧ�� */
    STOR_PRINTF_BAD_SEARCH_HANDLE = 418, /* �Ƿ�������� */
    STOR_PRINTF_SEARCH_TAGS_CONVERT_FAIL = 419, /* �����õ�tags�ַ���ת��ʧ�� */
    STOR_PRINTF_PTHREAD_COND_WAIT_FAIL = 420, /* pthread_cond_waitϵͳ����ʧ�� */
    STOR_PRINTF_SP_FILE_ERROR = 421, /* sp�ļ�д�����, ��Ҫ���� */
    STOR_PRINTF_SETMNTENT_FAIL = 422, /* ����mount entryʧ�� */
    STOR_PRINTF_NO_MATCH_ENTRY = 423, /* û��ƥ���mount entry */
    STOR_PRINTF_STATFS_FAIL = 424, /* ִ��statfsϵͳ����ʧ�� */
    STOR_PRINTF_FGETS_FAIL = 425, /* ִ��fgetsʧ�� */
};

/* stor_printf����������� */
typedef void * STOR_PRINTF_SE_HANDLE;

/* stor_printf������������ */
struct stor_printf_search_param_t {
    char tags[256]; /* ������ǩ, ����ָ������Ҳ���Զ��(���ʱʹ��#�ָ�), Ϊ��ʱ��ʾ��������,��������, eg: dial, gps#dial */
    time_t starttime; /* ��ʼʱ�� */
    time_t endtime; /* ����ʱ�� */
};


struct stor_printf_spfile_t {
    char path[128]; /* sp�ļ���·�� */
    unsigned int file_len; /* sp�ļ����� */

    /* ��sp�ļ���ʼʱ�䱻������: ����sp�ļ���ʱ�䲻����, ��������������
     * ����ʱֻ�ܰ����ǰ�������, ���ܲ��ö��ַ����� 
     */
    int starttime_adjust; 

    unsigned int data_size; /* sp�ļ���������С, ��λ: �ֽ� */
    unsigned int data_offset; /* sp�ļ���ǰд�����ݵ�ƫ����, ��Χ[0, data_size - 1] */
    
    unsigned int start_time; /* sp�ļ����ݵĿ�ʼʱ�� */
    unsigned int end_time; /* sp�ļ����ݵĽ�ֹʱ�� */
};

/* sp�ļ���ӡ���ƿ��� */
enum STOR_PRINTF_SPFILE_PRTMASK {
	SPFILE_PRTMASK_EMPTY = 0x01,
	SPFILE_PRTMASK_INUSE = 0x02,
	SPFILE_PRTMASK_FULL = 0x04,
    SPFILE_PRTMASK_OLDEST = 0x08,
    SPFILE_PRTMASK_NEWEST = 0x10,
};

#ifdef __cplusplus
extern "C" {
#endif

/**@fn          int stor_printf_open(const struct stor_printf_param_t *param, enum STOR_PRINTF_ERRNO *err)
 * @brief       ����һ��stor_printfģ��ʵ��, �����ز���id
 * @param[in]   const struct stor_printf_param_t *param    - ָ��stor_printfģ������ṹ��ָ��
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: �ɹ�, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note          
 */
int stor_printf_open(const struct stor_printf_param_t *param, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       ���һ��������ָ��id��stor_printfģ����, ���ڸ÷����д���(�粻����)���򿪶�Ӧ��sp�ļ���stor_printf�������¼�û�����
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   const char *partpath    - �������ػ�Ŀ¼·��, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: �ɹ�, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note          
 */
int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf(int id, const char *tag, const char *format, ...)
 * @brief       ��ָ��id��stor_printfģ������������printf�����ĸ�ʽ��¼����
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   const char *tag    - ��¼���û�����ǰ�ı�ǩ(ע: ��ǩ����Ĭ�ϴ���ʱ��, ʱ���ʽ(YYYYMMDD_HHMMSS), ����20191127_214848). �ò���ΪNULL���ʱ,���ӱ�ǩ��ʱ��,ֻ�����û���ԭʼ����
 * @param[in]   const char *format    - ������printf�ĸ�ʽ���ַ���
 * @param[out]  void
 * @return      > 0: ��ʾ�ɹ���¼�����ݳ���, 0: д�볬ʱ, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @warning     �ýӿ���������¼4096�ֽ��û�����, �������ֽ����ض�      
 */
int stor_printf(int id, const char *tag, const char *format, ...);

/**@fn          STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err)
 * @brief       ����һ��stor_printf����ʵ��, �ýӿڻ᷵��һ���������, �û�ѭ������stor_printf_search_next�ӿ�ÿ�λ�ȡһ���������
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   const struct stor_printf_search_param_t *param    - ��������, ����Ҫ�����ı�ǩ, �Լ�����ʱ�䷶Χ
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      != NULL: ����ʵ�����, NULL: ��ʾ��������ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note
 */
STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err)
 * @brief       ��ָ��id�Լ�ָ���������h�л�ȡһ����������Ҫ����û�����
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf����ʵ�����
 * @param[out]  void *buff    - ָ����������������ָ��
 * @param[in]   unsigned int size    - ������������������ߴ�
 * @param[in]   int waitms    - ���μ����ȴ���ʱʱ��, ��λ: ����, -1 ��ʾһֱ�ȴ�, 0 ��ʾ���ȴ�,û����������������
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      > 0: ��ʾ����������ʵ�ʳ���, 0: ��ʾ������ʱ, -1: ��ʾ����ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note
 */
int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       �ر�ָ��id��stor_printfģ���ָ���������h������ʵ��
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf����ʵ�����, ΪNULLʱ��ʾ�ر���������ʵ��
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: ��ʾ�ɹ�, -1: ��ʾʧ��, ͨ���������err��ȡ����Ĵ����
 * @note
 */
int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       ��ָ��id��stor_printfģ����ɾ��ָ������
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   const char *partpath    - ������Ŀ¼·��, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: �ɹ�, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note          
 */
int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err)
 * @brief       ��ӡָ��id��stor_printfģ��������sp�ļ���Ϣ
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   unsigned int prtmask    - sp�ļ���Ϣ��ӡ����, ���enum STOR_PRINTF_SPFILE_PRTMASK
 * @param[in]   char *prtbuff    - ��Ŵ�ӡ�Ļ���, �ò���ΪNULLʱ��ʾ����ӡ�������׼�����
 * @param[in]   unsigned int size    - ��Ŵ�ӡ��������ռ��С, �ò���Ϊ0ʱ��ʾ����ӡ�������׼�����
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: �ɹ�, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note          
 */
int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err)
 * @brief       ����һ��stor_printfģ��ʵ��, �����ز���id
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: �ɹ�, -1: ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note          
 */
int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err);

/**@fn          const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err)
 * @brief       ��ָ��stor_printfģ��Ĵ����ת���ɶ�Ӧ�ַ����Ľӿ�
 * @param[in]   enum STOR_PRINTF_ERRNO err    - stor_printfģ������
 * @param[out]  void
 * @return      ����stor_printfģ�����ŵ��ַ���
 * @note          
 */
const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err);

/**@fn          void stor_printf_dbg_set(int enable_dbg)
 * @brief       ����stor_printfģ����Դ�ӡ��Ϣ
 * @param[in]   int enable_dbg    - �Ƿ�ʹ�ܵ��Դ�ӡ
 * @param[out]  void
 * @return      void
 * @note          
 */
void stor_printf_dbg_set(int enable_dbg);


/**@fn          int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err)
 * @brief       ��ָ��id�Լ�ָ���������h�л�ȡһ����������Ҫ���sp�ļ���Ϣ
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf����ʵ�����
 * @param[out]  struct stor_printf_spfile_t *spfile    - ָ��sp�ļ���Ϣ�ṹ��ָ��
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      0: ��ʾ�ɹ�, -1: ��ʾ����ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note
 */
int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err);



/**@fn          int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       ��ָ��id�Լ�ָ���������h�л�ȡһ����������Ҫ���sp�ļ�����
 * @param[in]   int id    - ����stor_printfģ���id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf����ʵ�����
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - ָ�����ŵ�ָ��
 * @return      >= 0: ��ʾsp�ļ�����, -1: ��ʾ����ʧ��, ͨ���������err��ȡ����Ĵ����
 * @note
 */
int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err);
#ifdef __cplusplus
}
#endif

#endif

