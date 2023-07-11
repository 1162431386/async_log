
#ifndef _PACKET_H_
#define _PACKET_H_

typedef enum {
	PACK_SUCC = 0,
	/* 没有正确的数据 */
	PACK_DATA_ERROR = 1, 
	PACK_DATA_NOT_ENOUGH = 2,
	PACK_PACKET_ERROR = 3,
	PACK_INVALID_ID = 4,
	PACK_BAD_ID = 5,
	PACK_INVALID_INPUT = 6,
	PACK_PTHREAD_COND_WAIT_FAIL = 7,
	PACK_INVALID_PARAM = 8,
	PACK_NO_MEMORY = 9,
	PACK_NO_RESOURCES = 10,
	PACK_NO_ANA_CALLBACK = 11,
	PACK_ANA_OUTPUT_ERROR = 12,
	PACK_SPACE_NOT_ENOUGH = 13,
	PACK_BUFF_TOO_SMALL = 14,
	PACK_INVALID_ANA_STATE = 15,
	PACK_SELECT_FAIL = 16,
	PACK_TIMEOUT = 17,
	PACK_IO_WAKEUP = 18,
	PACK_READ_FAIL = 19,
	PACK_IO_ERROR = 20,
	PACK_PEER_SHUTDOWN = 21,
	PACK_UNKNOWN_CMD = 22,
	PACK_END_OF_FILE = 23,
}PACK_ERR;

typedef struct {
	unsigned int uBuffSize; /* 数据解析缓冲区长度 */
	
	int iStreamFd; /* 读数据的流式数据描述符 */
	
	int iWakeUpFd; /* IO唤醒fd */

	/**@fn          PACK_ERR (*packet_ana)(unsigned char *pAnaBuff, unsigned int uAnaDataLen, unsigned int *puPacketIdx, unsigned int *puPacketLen, unsigned int *puCorrectLen)
	 * @brief       数据包分析回调函数
	 * @param[in]   unsigned char *pAnaBuff    - packet库给用户分析的数据的缓存的指针
	 * @param[in]   unsigned int uAnaDataLen    - packet库给用户分析的数据的长度
	 * @param[out]  unsigned int *puPacketIdx    - 用户返回给packet库正确的一包数据在pAnaBuff的起始索引, 范围: 0 ~ (uAnaDataLen - 1)
	 * @param[out]  unsigned int *puPacketLen    - 用户返回给packet库正确的一包数据的长度, 范围: 1 ~ uAnaDataLen
	 * @param[out]  unsigned int *puCorrectLen    - 用户返回给packet库数据包在做转义时纠正过的长度
	 * @return      PACK_SUCC: 用户分析出pAnaBuff中存在一包正确的数据，并通过puPacketIdx，puPacketLen返回给packet库包起始索引以及包长度
	 * @return      PACK_DATA_ERROR: 用户分析出pAnaBuff中没有任何与包相关的数据, 此时packet库会跳过待分析的所有数据
	 * @return      PACK_DATA_NOT_ENOUGH: 用户发现pAnaBuff中没有一包完整的数据, 需要packet库读取更多数据, 并通过puPacketIdx，puPacketLen返回给packet库需调过的无效数据起始索引以及长度
	 * @return      PACK_PACKET_ERROR: 用户分析出pAnaBuff中存在一包数据, 但该包是错误的(比如校验错)，并通过puPacketIdx，puPacketLen返回给packet库包起始索引及包长度, puCorrectLen表示封包转义时纠正的长度
	 * @note          
	 */
	PACK_ERR (*packet_ana)(unsigned char *pAnaBuff, unsigned int uAnaDataLen, unsigned int *puPacketIdx, unsigned int *puPacketLen, unsigned int *puCorrectLen); /* 抓包回调函数 */
}PACKET_PARAM_T;

typedef enum {
	PACK_CMD_WAKEUPFD_SET = 0,
}PACKET_CMD;

#ifdef __cplusplus
extern "C" {
#endif

int packet_open(const PACKET_PARAM_T *pstruParam, PACK_ERR *pErr);

int packet_read(int iId, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, int iWaitMs, PACK_ERR *pErr);

int packet_cmd(int iId, PACKET_CMD cmd, void *pCmdBuff, unsigned int uCmdLen, void *pRspBuff, unsigned int uRspSize, unsigned int *puRspLen, int iWaitMs, PACK_ERR *pErr);

int packet_close(int iId, PACK_ERR *pErr);

const char * packet_strerr(PACK_ERR err);

void packet_dbg_set(int bEnablePackDbg);

#ifdef __cplusplus
}
#endif

#endif


