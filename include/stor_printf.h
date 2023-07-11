
/**@file stor_printf.h
 * @note HangZhou Hikvision System Technology Co., Ltd. All Right Reserved.
 * @brief  通过预分配文件结合存储组件实现记录打印信息的接口描述
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

/* stor_printf模块参数 */
struct stor_printf_param_t {
    char filename[32]; /* 用与存储数据的sp文件总名称 */

    unsigned int sp_count; /* 单个存储器分区sp文件个数 */
    unsigned int sp_datasize; /* 单个sp文件数据区大小, 单位: 字节 */   
    int sp_prealloc; /* sp文件是否预分配 */
    int sp_head_sync_inter; /* sp文件头同步磁盘时间间隔, 单位: 秒, -1: 不启用超时同步, 0: 写入数据时立即同步, > 0: 超过指定时间间隔则同步, 建议默认值30秒 */

    int wr_waitms; /* stor_printf接口写入超时时间, 单位: 毫秒 */
    int diskpart_min_keepsize; /* 创建每个分区创建sp文件时, 最小保留空间, 单位: 字节, 默认32*1024*1024, 该参数为0时, 取默认值32*1024*1024, -1表示不做判断 */
};

/* stor_printf错误码 */
enum STOR_PRINTF_ERRNO {
    STOR_PRINTF_SUCC = 200, /* 成功 */
    STOR_PRINTF_SEARCH_COMPLETE = 201, /* 检索完成 */

    STOR_PRINTF_NO_RESOURCES = 300, /* 资源暂时不可用 */
    STOR_PRINTF_RESOURCES_UNINITILISED = 301, /* 资源还未初始化 */
    STOR_PRINTF_IO_WAKEUP = 302,    
    STOR_PRINTF_WRITE_PARTLY = 303, /* 文件部分写入 */
    STOR_PRINTF_READ_PARTLY = 304, /* 文件部分读取 */   
    STOR_PRINTF_TIMEOUT = 305, /* 操作超时 */
    STOR_PRINTF_NO_AVAILABLE_SP_FILE = 306, /* 没有可用的sp文件 */
    STOR_PRINTF_SP_FILE_FULL = 307, /* sp文件满 */
    STOR_PRINTF_CREATE_DIR_FAIL = 308, /* 创建目录失败 */
    STOR_PRINTF_SPACE_NOT_ENOUGH = 309, /* 磁盘存储空间不足 */
    STOR_PRINTF_SP_FILELENGTH_MISMATCH = 310, /* sp文件长度与用户设定的不一致 */

    STOR_PRINTF_INVALID_INPUT = 400, /* 非法输入参数 */
    STOR_PRINTF_INVALID_ARGS = 401, /* 非法参数 */
    STOR_PRINTF_PTHREAD_CREATE_FAIL = 402, /* 创建线程失败 */
    STOR_PRINTF_INVALID_ID = 403, /* 错误的id */
    STOR_PRINTF_BAD_ID = 404, /* 无效的id */
    STOR_PRINTF_OUT_OF_MEMORY = 405, /* 内存不足, 申请内存失败 */
    STOR_PRINTF_SOCKETPAIR_FAIL = 406, /* 创建socketpait失败 */
    STOR_PRINTF_SELECT_FAIL = 407, /* select系统调用返回失败 */
    STOR_PRINTF_FILE_CREAT_FAIL = 408, /* 创建文件失败 */
    STOR_PRINTF_FILE_LSEEK_FAIL = 409, /* 文件lseek失败 */
    STOR_PRINTF_WRITE_FAIL = 410, /* 文件写操作失败 */
    STOR_PRINTF_READ_FAIL = 411, /* 文件读操作失败 */
    STOR_PRINTF_CHECKSUM_FAIL = 412, /* 检验和检查失败 */
    STOR_PRINTF_INVALID_MAGIC = 413, /* sp文件magic字段不符合 */
    STOR_PRINTF_SP_BOTH_HEAD_ERROR = 414, /* sp文件两个头都错误 */
    STOR_PRINTF_IO_ERROR = 415, /* 未知IO错误 */
    STOR_PRINTF_FILE_OPEN_FAIL = 416, /* 打开文件失败 */
    STOR_PRINTF_FILE_WRITE_FAIL = 417, /* 写文件失败 */
    STOR_PRINTF_BAD_SEARCH_HANDLE = 418, /* 非法搜索句柄 */
    STOR_PRINTF_SEARCH_TAGS_CONVERT_FAIL = 419, /* 检索用的tags字符串转换失败 */
    STOR_PRINTF_PTHREAD_COND_WAIT_FAIL = 420, /* pthread_cond_wait系统调用失败 */
    STOR_PRINTF_SP_FILE_ERROR = 421, /* sp文件写入错误, 需要忽略 */
    STOR_PRINTF_SETMNTENT_FAIL = 422, /* 设置mount entry失败 */
    STOR_PRINTF_NO_MATCH_ENTRY = 423, /* 没有匹配的mount entry */
    STOR_PRINTF_STATFS_FAIL = 424, /* 执行statfs系统调用失败 */
    STOR_PRINTF_FGETS_FAIL = 425, /* 执行fgets失败 */
};

/* stor_printf数据搜索句柄 */
typedef void * STOR_PRINTF_SE_HANDLE;

/* stor_printf数据搜索参数 */
struct stor_printf_search_param_t {
    char tags[256]; /* 搜索标签, 可以指定单个也可以多个(多个时使用#分隔), 为空时表示不做过滤,搜索所有, eg: dial, gps#dial */
    time_t starttime; /* 开始时间 */
    time_t endtime; /* 结束时间 */
};


struct stor_printf_spfile_t {
    char path[128]; /* sp文件的路径 */
    unsigned int file_len; /* sp文件长度 */

    /* 该sp文件起始时间被调整过: 导致sp文件中时间不均匀, 存在跳变的情况。
     * 检索时只能按序从前往后检索, 不能采用二分法查找 
     */
    int starttime_adjust; 

    unsigned int data_size; /* sp文件数据区大小, 单位: 字节 */
    unsigned int data_offset; /* sp文件当前写入数据的偏移量, 范围[0, data_size - 1] */
    
    unsigned int start_time; /* sp文件数据的开始时间 */
    unsigned int end_time; /* sp文件数据的截止时间 */
};

/* sp文件打印控制开关 */
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
 * @brief       创建一个stor_printf模块实例, 并返回操作id
 * @param[in]   const struct stor_printf_param_t *param    - 指向stor_printf模块参数结构的指针
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_open(const struct stor_printf_param_t *param, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       添加一个分区到指定id的stor_printf模块中, 并在该分区中创建(如不存在)并打开对应的sp文件工stor_printf主任务记录用户数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *partpath    - 分区挂载或目录路径, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_part_add(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf(int id, const char *tag, const char *format, ...)
 * @brief       像指定id的stor_printf模块中以类似于printf函数的格式记录数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *tag    - 记录在用户数据前的标签(注: 标签后面默认带有时间, 时间格式(YYYYMMDD_HHMMSS), 举例20191127_214848). 该参数为NULL或空时,不加标签及时间,只存入用户的原始数据
 * @param[in]   const char *format    - 类似于printf的格式化字符串
 * @param[out]  void
 * @return      > 0: 表示成功记录的数据长度, 0: 写入超时, -1: 失败, 通过输出参数err获取具体的错误号
 * @warning     该接口最多允许记录4096字节用户数据, 超过部分将被截断      
 */
int stor_printf(int id, const char *tag, const char *format, ...);

/**@fn          STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err)
 * @brief       开启一个stor_printf搜索实例, 该接口会返回一个搜索句柄, 用户循环调用stor_printf_search_next接口每次获取一个搜索结果
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const struct stor_printf_search_param_t *param    - 搜索参数, 包含要搜索的标签, 以及搜索时间范围
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      != NULL: 搜索实例句柄, NULL: 表示开启搜索失败, 通过输出参数err获取具体的错误号
 * @note
 */
STOR_PRINTF_SE_HANDLE stor_printf_search_start(int id, const struct stor_printf_search_param_t *param, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的用户数据
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  void *buff    - 指向存放搜索结果缓存的指针
 * @param[in]   unsigned int size    - 存放搜索结果缓存的最大尺寸
 * @param[in]   int waitms    - 单次检索等待超时时间, 单位: 毫秒, -1 表示一直等待, 0 表示不等待,没有数据则立即返回
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      > 0: 表示搜索的数据实际长度, 0: 表示检索超时, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_next(int id, STOR_PRINTF_SE_HANDLE h, void *buff, unsigned int size, int waitms, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       关闭指定id的stor_printf模块的指定搜索句柄h的搜索实例
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄, 为NULL时表示关闭所有搜索实例
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 表示成功, -1: 表示失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_stop(int id, STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id的stor_printf模块中删除指定分区
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   const char *partpath    - 分区或目录路径, eg: /mnt/sda1, /mnt/isda1
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_part_remove(int id, const char *partpath, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err)
 * @brief       打印指定id的stor_printf模块中所有sp文件信息
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   unsigned int prtmask    - sp文件信息打印掩码, 详见enum STOR_PRINTF_SPFILE_PRTMASK
 * @param[in]   char *prtbuff    - 存放打印的缓存, 该参数为NULL时表示将打印输出到标准输出中
 * @param[in]   unsigned int size    - 存放打印缓存的最大空间大小, 该参数为0时表示将打印输出到标准输出中
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_spfiles_print(int id, unsigned int prtmask, char *prtbuff, unsigned int size, enum STOR_PRINTF_ERRNO *err);

/**@fn          int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err)
 * @brief       创建一个stor_printf模块实例, 并返回操作id
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 成功, -1: 失败, 通过输出参数err获取具体的错误号
 * @note          
 */
int stor_printf_close(int id, enum STOR_PRINTF_ERRNO *err);

/**@fn          const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err)
 * @brief       将指定stor_printf模块的错误号转换成对应字符串的接口
 * @param[in]   enum STOR_PRINTF_ERRNO err    - stor_printf模块错误号
 * @param[out]  void
 * @return      描述stor_printf模块错误号的字符串
 * @note          
 */
const char * stor_printf_strerr(enum STOR_PRINTF_ERRNO err);

/**@fn          void stor_printf_dbg_set(int enable_dbg)
 * @brief       开启stor_printf模块调试打印信息
 * @param[in]   int enable_dbg    - 是否使能调试打印
 * @param[out]  void
 * @return      void
 * @note          
 */
void stor_printf_dbg_set(int enable_dbg);


/**@fn          int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的sp文件信息
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  struct stor_printf_spfile_t *spfile    - 指向sp文件信息结构的指针
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      0: 表示成功, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_spfile_next(int id, STOR_PRINTF_SE_HANDLE h, struct stor_printf_spfile_t *spfile, enum STOR_PRINTF_ERRNO *err);



/**@fn          int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err)
 * @brief       从指定id以及指定搜索句柄h中获取一个满足搜索要求的sp文件个数
 * @param[in]   int id    - 描述stor_printf模块的id
 * @param[in]   STOR_PRINTF_SE_HANDLE h    - stor_printf检索实例句柄
 * @param[out]  enum STOR_PRINTF_ERRNO *err    - 指向错误号的指针
 * @return      >= 0: 表示sp文件个数, -1: 表示检索失败, 通过输出参数err获取具体的错误号
 * @note
 */
int stor_printf_search_spfile_count(int id, const STOR_PRINTF_SE_HANDLE h, enum STOR_PRINTF_ERRNO *err);
#ifdef __cplusplus
}
#endif

#endif

