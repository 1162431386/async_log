
#ifndef _PACKET_H_
#define _PACKET_H_

typedef enum {
	PACK_SUCC = 0,
	/* û����ȷ������ */
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
	unsigned int uBuffSize; /* ���ݽ������������� */
	
	int iStreamFd; /* �����ݵ���ʽ���������� */
	
	int iWakeUpFd; /* IO����fd */

	/**@fn          PACK_ERR (*packet_ana)(unsigned char *pAnaBuff, unsigned int uAnaDataLen, unsigned int *puPacketIdx, unsigned int *puPacketLen, unsigned int *puCorrectLen)
	 * @brief       ���ݰ������ص�����
	 * @param[in]   unsigned char *pAnaBuff    - packet����û����������ݵĻ����ָ��
	 * @param[in]   unsigned int uAnaDataLen    - packet����û����������ݵĳ���
	 * @param[out]  unsigned int *puPacketIdx    - �û����ظ�packet����ȷ��һ��������pAnaBuff����ʼ����, ��Χ: 0 ~ (uAnaDataLen - 1)
	 * @param[out]  unsigned int *puPacketLen    - �û����ظ�packet����ȷ��һ�����ݵĳ���, ��Χ: 1 ~ uAnaDataLen
	 * @param[out]  unsigned int *puCorrectLen    - �û����ظ�packet�����ݰ�����ת��ʱ�������ĳ���
	 * @return      PACK_SUCC: �û�������pAnaBuff�д���һ����ȷ�����ݣ���ͨ��puPacketIdx��puPacketLen���ظ�packet�����ʼ�����Լ�������
	 * @return      PACK_DATA_ERROR: �û�������pAnaBuff��û���κ������ص�����, ��ʱpacket�����������������������
	 * @return      PACK_DATA_NOT_ENOUGH: �û�����pAnaBuff��û��һ������������, ��Ҫpacket���ȡ��������, ��ͨ��puPacketIdx��puPacketLen���ظ�packet�����������Ч������ʼ�����Լ�����
	 * @return      PACK_PACKET_ERROR: �û�������pAnaBuff�д���һ������, ���ð��Ǵ����(����У���)����ͨ��puPacketIdx��puPacketLen���ظ�packet�����ʼ������������, puCorrectLen��ʾ���ת��ʱ�����ĳ���
	 * @note          
	 */
	PACK_ERR (*packet_ana)(unsigned char *pAnaBuff, unsigned int uAnaDataLen, unsigned int *puPacketIdx, unsigned int *puPacketLen, unsigned int *puCorrectLen); /* ץ���ص����� */
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


