
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "packet.h"

#define PACK_INFO_print
#ifdef PACK_INFO_print
#define INFO(fmt, args...) printf("I[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define INFO(fmt, args...)
#endif

#define PACK_WARN_print
#ifdef PACK_WARN_print
#define WARN(fmt, args...) printf("W[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define WARN(fmt, args...)
#endif

#define PACK_ERR_print
#ifdef PACK_ERR_print
#define ERR(fmt, args...) printf("\033[31mE[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)
#else
#define ERR(fmt, args...)
#endif

static int sg_bEnablePacketDbg = 0;

#define PACK_DBG_print
#ifdef PACK_DBG_print
#define DBG(fmt, args...) do { \
	if (sg_bEnablePacketDbg) { \
		printf("\033[32mD[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args); \
	} \
} while (0)
#else
#define DBG(fmt, args...)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a[0])))
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(ptr) do { \
	if (NULL != (ptr)) { \
		free((ptr)); \
		(ptr) = NULL; \
	} \
} while (0)
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define PACK_ERR_SET(pErr, err) do { \
	if (NULL != (pErr)) { \
		*(pErr) = (err); \
	} \
} while (0)

/* int型数据转换成字符串的结构体 */
typedef struct {
	int iType;
	const char *pszType;
}PACK_TYPE_STR_T;

#define PACK_TYPE_STR_ITEM(type) {type, #type}

typedef enum {
	STREAMFD_TYPE_FILE = 0x00,
    STREAMFD_TYPE_PIPE = 0x01,
    STREAMFD_TYPE_SOCKET = 0x02,
}STREAMFD_TYPE;

typedef enum {
	DATA_GET = 0x00, /* 获取待分析的数据 */
	DATA_ANA = 0x01, /* 分析数据 */
	DATA_NOT_ENOUGH = 0x02, /* 数据不足 */
}ANA_STATE;

typedef struct {
	pthread_mutex_t struMSem;
	int bInuse;

	pthread_cond_t struCond;
	int iRefCount;

	PACKET_PARAM_T struParam;

    STREAMFD_TYPE streamFdType;

	char *pBuff;
	unsigned int uRdIdx;
	unsigned int uWrIdx;

	unsigned char *pAnaBuff;
	unsigned int uAnaBuffSize;
	ANA_STATE anaState;
	unsigned int uAnaDataLen;
}PACKET_T;

#define PACKET_MAX_NUM 32

typedef struct {
	pthread_mutex_t struMSem;
	int bInit;

	PACKET_T astruPacket[PACKET_MAX_NUM];
}PACKETS_T;

static PACKETS_T sg_struPackets = {
	.struMSem = PTHREAD_MUTEX_INITIALIZER,
	.bInit = 0,
};

static PACK_TYPE_STR_T sg_astruPacketStreamFdTypeTab[] = {
	PACK_TYPE_STR_ITEM(STREAMFD_TYPE_SOCKET),
	PACK_TYPE_STR_ITEM(STREAMFD_TYPE_PIPE),
	PACK_TYPE_STR_ITEM(STREAMFD_TYPE_FILE),
};

static PACK_TYPE_STR_T sg_astruPacketAnaStateTab[] = {
	PACK_TYPE_STR_ITEM(DATA_GET),
	PACK_TYPE_STR_ITEM(DATA_ANA),
	PACK_TYPE_STR_ITEM(DATA_NOT_ENOUGH),
};

static PACK_TYPE_STR_T sg_astruPacketErrNoTab[] = {
	PACK_TYPE_STR_ITEM(PACK_SUCC),
	PACK_TYPE_STR_ITEM(PACK_DATA_ERROR),
	PACK_TYPE_STR_ITEM(PACK_DATA_NOT_ENOUGH),
	PACK_TYPE_STR_ITEM(PACK_PACKET_ERROR),
	PACK_TYPE_STR_ITEM(PACK_INVALID_ID),
	PACK_TYPE_STR_ITEM(PACK_BAD_ID),
	PACK_TYPE_STR_ITEM(PACK_INVALID_INPUT),
	PACK_TYPE_STR_ITEM(PACK_PTHREAD_COND_WAIT_FAIL),
	PACK_TYPE_STR_ITEM(PACK_INVALID_PARAM),
	PACK_TYPE_STR_ITEM(PACK_NO_MEMORY),
	PACK_TYPE_STR_ITEM(PACK_NO_RESOURCES),
	PACK_TYPE_STR_ITEM(PACK_NO_ANA_CALLBACK),
	PACK_TYPE_STR_ITEM(PACK_ANA_OUTPUT_ERROR),
	PACK_TYPE_STR_ITEM(PACK_SPACE_NOT_ENOUGH),
	PACK_TYPE_STR_ITEM(PACK_BUFF_TOO_SMALL),
	PACK_TYPE_STR_ITEM(PACK_INVALID_ANA_STATE),
	PACK_TYPE_STR_ITEM(PACK_SELECT_FAIL),
	PACK_TYPE_STR_ITEM(PACK_TIMEOUT),
	PACK_TYPE_STR_ITEM(PACK_IO_WAKEUP),
	PACK_TYPE_STR_ITEM(PACK_READ_FAIL),
	PACK_TYPE_STR_ITEM(PACK_IO_ERROR),
	PACK_TYPE_STR_ITEM(PACK_PEER_SHUTDOWN),
	PACK_TYPE_STR_ITEM(PACK_UNKNOWN_CMD),
	PACK_TYPE_STR_ITEM(PACK_END_OF_FILE),
};

const char * packet_strtype(const PACK_TYPE_STR_T astruTable[], unsigned int uArraySize, int iType)
{
	static char s_szUnknown[32] = {0};
    int i;
	
    if (NULL == astruTable || 0 == uArraySize) {
        snprintf(s_szUnknown, sizeof (s_szUnknown), "NULLTAB_0x%x", iType);
        return s_szUnknown;
    }
    
    for (i = 0; i < uArraySize; i++) {
        if (astruTable[i].iType == (int)iType) {
			if (NULL == astruTable[i].pszType) {
				snprintf(s_szUnknown, sizeof (s_szUnknown), "NULL_0x%x", iType);
				return s_szUnknown;
			}
            return astruTable[i].pszType;
        }
    }
	
    snprintf(s_szUnknown, sizeof (s_szUnknown), "UNKNOWN_0x%x", iType);
    return s_szUnknown;

}

const char * packet_strstreamfdtype(STREAMFD_TYPE streamFdType)
{
	return packet_strtype(sg_astruPacketStreamFdTypeTab, ARRAY_SIZE(sg_astruPacketStreamFdTypeTab), (int)streamFdType);

}

const char * packet_stranastate(ANA_STATE anaState)
{
	return packet_strtype(sg_astruPacketAnaStateTab, ARRAY_SIZE(sg_astruPacketAnaStateTab), (int)anaState);

}

PACK_ERR packet_pack_read(int iFd, STREAMFD_TYPE streamFdType, int iWakeUpFd, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, struct timeval *pstruTimeOut)
{
	fd_set struRSet;
	int iNFds = -1;
	int iRet = -1;

	if (-1 == iFd || NULL == pBuff || 0 == uBuffSize) {
		ERR("INVALID input param, iFd = %d, pBuff = %p, uBuffSize = %u\n", iFd, pBuff, uBuffSize);
		return PACK_INVALID_INPUT;
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
		return PACK_SELECT_FAIL;
	}
	else if (0 == iRet) {
		return PACK_TIMEOUT;
	}

	if (-1 != iWakeUpFd && FD_ISSET(iWakeUpFd, &struRSet)) {
		return PACK_IO_WAKEUP;
	}

	if (FD_ISSET(iFd, &struRSet)) {
		if (STREAMFD_TYPE_SOCKET == streamFdType) {
			do {
				iRet = recv(iFd, pBuff, uBuffSize, 0);
			} while (-1 == iRet && EINTR == errno);
			if (-1 == iRet) {
				ERR("FAIL to read, %s\n", strerror(errno));
				return PACK_READ_FAIL;
			}
	        else if (0 == iRet) {
	            ERR("PEER shutdown\n");
	            return PACK_PEER_SHUTDOWN;
	        }
		}
		else {
			do {
				iRet = read(iFd, pBuff, uBuffSize);
			} while (-1 == iRet && EINTR == errno);
			if (-1 == iRet) {
				ERR("FAIL to read, %s\n", strerror(errno));
				return PACK_READ_FAIL;
			}
	        else if (0 == iRet) {
	           // INFO("END of file\n");
	            return PACK_END_OF_FILE;
	        }
		}

		if (NULL != puReadLen) {
			*puReadLen = (unsigned int)iRet;
		}

		return PACK_SUCC;
	}

	return PACK_IO_ERROR;

}

static int packet_2_id(const PACKET_T *pstruPacket)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sg_struPackets.astruPacket); i++) {
		if (&(sg_struPackets.astruPacket[i]) == (PACKET_T *)pstruPacket) {
			return i;
		}
	}

	return -1;

}

static PACKET_T * id_2_packet(int iId)
{
	if (iId < 0 || iId > (ARRAY_SIZE(sg_struPackets.astruPacket) - 1)) {
		return NULL;
	}

	return &(sg_struPackets.astruPacket[iId]);

}

static PACK_ERR packet_check(const PACKET_PARAM_T *pstruParam)
{
	if (NULL == pstruParam) {
		return PACK_INVALID_INPUT;
	}

	if (0 == pstruParam->uBuffSize) {
		ERR("INVALID uBuffSize: %u\n", pstruParam->uBuffSize);
		return PACK_INVALID_PARAM;
	}

	if (-1 == pstruParam->iStreamFd) {
		ERR("INVALID iStreamFd: %d\n", pstruParam->iStreamFd);
		return PACK_INVALID_PARAM;
	}

	if (NULL == pstruParam->packet_ana) {
		ERR("INVALID packet_ana callback\n");
		return PACK_INVALID_PARAM;
	}

	return PACK_SUCC;

}

static inline PACK_ERR _packet_fini(PACKET_T *pstruPacket)
{
	pstruPacket->uAnaDataLen = 0;
	pstruPacket->anaState = DATA_GET;
	pstruPacket->uAnaBuffSize = 0;
	SAFE_FREE(pstruPacket->pAnaBuff);

	pstruPacket->uWrIdx = 0;
	pstruPacket->uRdIdx = 0;
	SAFE_FREE(pstruPacket->pBuff);

    pstruPacket->streamFdType = STREAMFD_TYPE_FILE;
    
	memset((char *)&pstruPacket->struParam, 0, sizeof (pstruPacket->struParam));

	pstruPacket->iRefCount = 0;

	return PACK_SUCC;

}

static PACK_ERR packet_fini(PACKET_T *pstruPacket)
{
	PACK_ERR err;

	if (NULL == pstruPacket) {
		return PACK_INVALID_INPUT;
	}

	err = _packet_fini(pstruPacket);

	pthread_mutex_lock(&sg_struPackets.struMSem);
	{
		pstruPacket->bInuse = 0;
	}
	pthread_mutex_unlock(&sg_struPackets.struMSem);

	return err;

}

static inline void packets_init(PACKETS_T *pstruPackets)
{
	int i;
	PACKET_T *pstruPacket;

	for (i = 0; i < ARRAY_SIZE(sg_struPackets.astruPacket); i++) {
		pstruPacket = &(sg_struPackets.astruPacket[i]);
		memset((char *)pstruPacket, 0, sizeof (*pstruPacket));

		pthread_mutex_init(&pstruPacket->struMSem, NULL);
		pstruPacket->bInuse = 0;

		pthread_cond_init(&pstruPacket->struCond, NULL);
		pstruPacket->iRefCount = 0;

        pstruPacket->streamFdType = STREAMFD_TYPE_FILE;
        
		pstruPacket->pBuff = NULL;
		pstruPacket->uRdIdx = 0;
		pstruPacket->uWrIdx = 0;

		pstruPacket->pAnaBuff = NULL;
		pstruPacket->uAnaBuffSize = 0;
		pstruPacket->anaState = DATA_GET;
		pstruPacket->uAnaDataLen = 0;
	}

	return;

}

static inline STREAMFD_TYPE packet_streamfd_type_get(int iStreamFd)
{
	char szFdPath[128] = {0};
	char szLinkBuff[128] = {0};
	STREAMFD_TYPE streamFdType = STREAMFD_TYPE_FILE;

	snprintf(szFdPath, sizeof (szFdPath), "/proc/%d/fd/%d", (int)getpid(), iStreamFd);
	if (-1 == readlink(szFdPath, szLinkBuff, sizeof (szLinkBuff))) {
		WARN("FAIL to readlink %s, %s, ret default STREAMFD_TYPE_FILE\n", szFdPath, strerror(errno));
		return STREAMFD_TYPE_FILE;
	}
    szLinkBuff[sizeof (szLinkBuff) - 1] = '\0';

	if (NULL != strstr(szLinkBuff, "socket")) {
		streamFdType = STREAMFD_TYPE_SOCKET;
	}
	else if (NULL != strstr(szLinkBuff, "pipe")) {
		streamFdType = STREAMFD_TYPE_PIPE;
	}
	else {
		streamFdType = STREAMFD_TYPE_FILE;
	}

	return streamFdType;

}

static PACK_ERR _packet_init(PACKET_T *pstruPacket, const PACKET_PARAM_T *pstruParam)
{
    PACK_ERR err = PACK_SUCC;

	if (NULL == pstruPacket) {
		return PACK_INVALID_INPUT;
	}

	pstruPacket->iRefCount = 0;

	memcpy((char *)&pstruPacket->struParam, (char *)pstruParam, sizeof (PACKET_PARAM_T));

	pstruPacket->streamFdType = packet_streamfd_type_get(pstruPacket->struParam.iStreamFd);

	pstruPacket->pBuff = (char *)malloc(pstruPacket->struParam.uBuffSize);
	if (NULL == pstruPacket->pBuff) {
		err = PACK_NO_MEMORY;
		goto errExit;
	}
	memset(pstruPacket->pBuff, 0, pstruPacket->struParam.uBuffSize);
	pstruPacket->uRdIdx = 0;
	pstruPacket->uWrIdx = 0;

	pstruPacket->pAnaBuff = NULL;
	pstruPacket->uAnaBuffSize = 0;
	pstruPacket->anaState = DATA_GET;
	pstruPacket->uAnaDataLen = 0;

	return PACK_SUCC;

errExit:

	_packet_fini(pstruPacket);

	return err;

}

static PACK_ERR packet_init(const PACKET_PARAM_T *pstruParam, PACKET_T **ppstruPacket)
{
	int i;
	PACKET_T *pstruPacket = NULL;
	PACK_ERR err;

	if (NULL == ppstruPacket) {
		return PACK_INVALID_INPUT;
	}

	pthread_mutex_lock(&sg_struPackets.struMSem);
	{
		if (!sg_struPackets.bInit) {
			packets_init(&sg_struPackets);
			sg_struPackets.bInit = 1;
		}

		for (i = 0; i < ARRAY_SIZE(sg_struPackets.astruPacket); i++) {
			pstruPacket = &(sg_struPackets.astruPacket[i]);
			if (!pstruPacket->bInuse) {
				err = _packet_init(pstruPacket, pstruParam);
				if (PACK_SUCC == err) {
					pstruPacket->bInuse = 1;
					*ppstruPacket = pstruPacket;
				}
				pthread_mutex_unlock(&sg_struPackets.struMSem);
				return err;
			}
		}
	}
	pthread_mutex_unlock(&sg_struPackets.struMSem);

	return PACK_NO_RESOURCES;

}

static PACK_ERR packet_main(PACKET_T *pstruPacket)
{
	return PACK_SUCC;

}

static inline PACK_ERR _packet_read_DATA_GET(PACKET_T *pstruPacket)
{
	unsigned int uAnaLen = 0;
	char *pAddr1 = NULL;
	unsigned int uLen1 = 0;
	char *pAddr2 = NULL;
	unsigned int uLen2 = 0;

	uAnaLen = (pstruPacket->uWrIdx + pstruPacket->struParam.uBuffSize - pstruPacket->uRdIdx) % pstruPacket->struParam.uBuffSize;
	if (0 == uAnaLen) {
		return PACK_DATA_NOT_ENOUGH;
	}
	if (uAnaLen > pstruPacket->struParam.uBuffSize) {
		WARN("BAD uAnaLen: %u, uBuffSize = %u, we cut it to %u, THIS SHOULD NEVER HAPPEN, PLEASE CHECK YOUR CODE!!!\n", \
		    uAnaLen, pstruPacket->struParam.uBuffSize, pstruPacket->struParam.uBuffSize);
		uAnaLen = pstruPacket->struParam.uBuffSize;
	}

	if (pstruPacket->uAnaBuffSize < uAnaLen) {
		pstruPacket->uAnaBuffSize = uAnaLen;
		pstruPacket->pAnaBuff = (unsigned char *)realloc(pstruPacket->pAnaBuff, pstruPacket->uAnaBuffSize);
		if (NULL == pstruPacket->pAnaBuff) {
			ERR("FAIL to realloc %u byte(s)\n", pstruPacket->uAnaBuffSize);
			return PACK_NO_MEMORY;
		}
	}
	memset(pstruPacket->pAnaBuff, 0, pstruPacket->uAnaBuffSize);

	if (pstruPacket->uWrIdx > pstruPacket->uRdIdx) {
		pAddr1 = pstruPacket->pBuff + pstruPacket->uRdIdx;
		uLen1 = pstruPacket->uWrIdx - pstruPacket->uRdIdx;
		pAddr2 = NULL;
		uLen2 = 0;
	}
	else if (pstruPacket->uWrIdx == pstruPacket->uRdIdx) {
		return PACK_DATA_NOT_ENOUGH;
	}
	else {
		pAddr1 = pstruPacket->pBuff + pstruPacket->uRdIdx;
		uLen1 = pstruPacket->struParam.uBuffSize - pstruPacket->uRdIdx;
		pAddr2 = pstruPacket->pBuff;
		uLen2 = pstruPacket->uWrIdx;
	}

	pstruPacket->uAnaDataLen = 0;
	if (NULL != pAddr1 && uLen1 > 0) {
		memcpy(pstruPacket->pAnaBuff + pstruPacket->uAnaDataLen, pAddr1, uLen1);
		pstruPacket->uAnaDataLen += uLen1;
	}

	if (NULL != pAddr2 && uLen2 > 0) {
		memcpy(pstruPacket->pAnaBuff + pstruPacket->uAnaDataLen, pAddr2, uLen2);
		pstruPacket->uAnaDataLen += uLen2;
	}

	return PACK_SUCC;

}

static inline PACK_ERR _packet_read_DATA_ANA(PACKET_T *pstruPacket, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen)
{
	PACK_ERR err;
	unsigned int uPacketIdx = 0;
	unsigned int uPacketLen = 0;
	unsigned int uCorrectLen = 0;

	if (NULL == pstruPacket->struParam.packet_ana) {
		return PACK_NO_ANA_CALLBACK;
	}

	DBG("uRdIdx = %u, uWrIdx = %u, uBuffSize = %u, pAnaBuff = %p, uAnaDataLen = %u\n", \
		pstruPacket->uRdIdx, pstruPacket->uWrIdx, pstruPacket->struParam.uBuffSize, pstruPacket->pAnaBuff, pstruPacket->uAnaDataLen);

	err = pstruPacket->struParam.packet_ana(pstruPacket->pAnaBuff, \
		pstruPacket->uAnaDataLen, &uPacketIdx, &uPacketLen, &uCorrectLen);
	switch (err) {
		case PACK_SUCC:
		case PACK_PACKET_ERROR:
			if (uPacketIdx > (pstruPacket->uAnaDataLen - 1)) {
				ERR("USER packet_ana callback output error packetIdx: %u, valid range [0, %u]\n", \
					uPacketIdx, pstruPacket->uAnaDataLen - 1);
				return PACK_ANA_OUTPUT_ERROR;
			}
			if (uPacketLen > pstruPacket->uAnaDataLen) {
				ERR("USER packet_ana callback output error packetLen: %u, anaDataLen: %u\n", \
					uPacketLen, pstruPacket->uAnaDataLen);
				return PACK_ANA_OUTPUT_ERROR;
			}
			
			if (PACK_SUCC == err) {
			    if (uBuffSize < uPacketLen) {
					WARN("USER buff space %u, but packetLen: %u, expand buff space\n", uBuffSize, uPacketLen);
					return PACK_SPACE_NOT_ENOUGH;
			    }

				memcpy(pBuff, pstruPacket->pAnaBuff + uPacketIdx, uPacketLen);
				if (NULL != puReadLen) {
					*puReadLen = uPacketLen;
				}
			}

			pstruPacket->uRdIdx = (pstruPacket->uRdIdx + uPacketIdx + uPacketLen + uCorrectLen + pstruPacket->struParam.uBuffSize) % pstruPacket->struParam.uBuffSize;
			return err;

		case PACK_DATA_ERROR:
			/* 待分析缓存中没有有效数据, 需要将数据待分析缓存数据全部清空 */
			pstruPacket->uRdIdx = (pstruPacket->uRdIdx + pstruPacket->uAnaDataLen + pstruPacket->struParam.uBuffSize) % pstruPacket->struParam.uBuffSize;
			break;

		case PACK_DATA_NOT_ENOUGH:
			if (uPacketIdx > 0) {
				if (uPacketIdx > (pstruPacket->uAnaDataLen - 1)) {
					ERR("USER packet_ana callback output error packetIdx: %u, valid range [0, %u]\n", uPacketIdx, pstruPacket->uAnaDataLen - 1);
					return PACK_ANA_OUTPUT_ERROR;
				}
				pstruPacket->uRdIdx = (pstruPacket->uRdIdx + uPacketIdx + pstruPacket->struParam.uBuffSize) % pstruPacket->struParam.uBuffSize;
			}
			break;

		default:
			break;
	}

	return err;

}

#define BUFF_NEXT_IDX(uIdx, uBuffSize) (((uIdx) + 1 + (uBuffSize)) % (uBuffSize))

static inline PACK_ERR _packet_read_DATA_NOT_ENOUGH(PACKET_T *pstruPacket, struct timeval *pstruTimeOut)
{
	char *pAddr1 = NULL;
	unsigned int uLen1 = 0;
	char *pAddr2 = NULL;
	unsigned int uLen2 = 0;
	unsigned int uRdLen = 0;
	PACK_ERR err = PACK_BUFF_TOO_SMALL;

	if (pstruPacket->uWrIdx >= pstruPacket->uRdIdx) {
		if (0 == pstruPacket->uRdIdx) {
			pAddr1 = pstruPacket->pBuff + pstruPacket->uWrIdx;
			uLen1 = pstruPacket->struParam.uBuffSize - 1;
		}
		else {
			pAddr1 = pstruPacket->pBuff + pstruPacket->uWrIdx;
			uLen1 = pstruPacket->struParam.uBuffSize - pstruPacket->uWrIdx;
			pAddr2 = pstruPacket->pBuff;
			uLen2 = pstruPacket->uRdIdx - 1;
		}
	}
	else {
		if (BUFF_NEXT_IDX(pstruPacket->uWrIdx, pstruPacket->struParam.uBuffSize) == pstruPacket->uRdIdx) {
			/* full */
			WARN("BUFF(size = %u) full, but no packet, buff too small, please expand your buffSize\n", pstruPacket->struParam.uBuffSize);
			return PACK_BUFF_TOO_SMALL;
		}
		else {
			pAddr1 = pstruPacket->pBuff + pstruPacket->uWrIdx;
			uLen1 = pstruPacket->uRdIdx - pstruPacket->uWrIdx - 1;
		}
	}

	DBG("pAddr1 = %p, uLen1 = %u, pAddr2 = %p, uLen2 = %u\n", pAddr1, uLen1, pAddr2, uLen2);

	if (NULL != pAddr1 && uLen1 > 0) {
		err = packet_pack_read(pstruPacket->struParam.iStreamFd, pstruPacket->streamFdType, pstruPacket->struParam.iWakeUpFd, \
			pAddr1, uLen1, &uRdLen, pstruTimeOut);
		DBG("err: %s, uRdLen = %u, uRdIdx = %u, uWrIdx = %u, uBuffSize = %u\n", packet_strerr(err), uRdLen, pstruPacket->uRdIdx, pstruPacket->uWrIdx, pstruPacket->struParam.uBuffSize);
		switch (err) {
			case PACK_SUCC:
				pstruPacket->uWrIdx = (pstruPacket->uWrIdx + uRdLen + pstruPacket->struParam.uBuffSize) % pstruPacket->struParam.uBuffSize;
				if (uRdLen < uLen1) {
					return PACK_SUCC;
				}
				break;
			default:
				return err;
		}
	}

	if (NULL != pAddr2 && uLen2 > 0) {
		err = packet_pack_read(pstruPacket->struParam.iStreamFd, pstruPacket->streamFdType, pstruPacket->struParam.iWakeUpFd, \
			pAddr2, uLen2, &uRdLen, pstruTimeOut);
		DBG("err: %s, uRdLen = %u, uRdIdx = %u, uWrIdx = %u, uBuffSize = %u\n", packet_strerr(err), uRdLen, pstruPacket->uRdIdx, pstruPacket->uWrIdx, pstruPacket->struParam.uBuffSize);
		switch (err) {
			case PACK_SUCC:
				pstruPacket->uWrIdx = (pstruPacket->uWrIdx + uRdLen + pstruPacket->struParam.uBuffSize) % pstruPacket->struParam.uBuffSize;
				break;
			default:
				return PACK_SUCC;
		}
	}

	return err;

}

static PACK_ERR _packet_read(PACKET_T *pstruPacket, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, int iWaitMs)
{
	PACK_ERR err = PACK_SUCC;
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
	pstruPacket->anaState = DATA_GET;

	do {
		DBG("packetAnaState: %s\n", packet_stranastate(pstruPacket->anaState));
		switch (pstruPacket->anaState) {
			case DATA_GET:
				err = _packet_read_DATA_GET(pstruPacket);
				switch (err) {
					case PACK_SUCC:
						pstruPacket->anaState = DATA_ANA;
						break;
					case PACK_DATA_NOT_ENOUGH:
						pstruPacket->anaState = DATA_NOT_ENOUGH;
						break;
					default:
						return err;
				}
				break;
			case DATA_ANA:
				err = _packet_read_DATA_ANA(pstruPacket, pBuff, uBuffSize, puReadLen);
				switch (err) {
					case PACK_SUCC:
						pstruPacket->anaState = DATA_GET;
						return PACK_SUCC;
                    case PACK_PACKET_ERROR:
						pstruPacket->anaState = DATA_GET;
						break;
					case PACK_DATA_ERROR:					
					case PACK_DATA_NOT_ENOUGH:
						pstruPacket->anaState = DATA_NOT_ENOUGH;
						break;
					default:
						return err;
				}
				break;
			case DATA_NOT_ENOUGH:
				err = _packet_read_DATA_NOT_ENOUGH(pstruPacket, pstruTimeOut);
				switch (err) {
					case PACK_SUCC:
						pstruPacket->anaState = DATA_GET;
						break;
					default:
						return err;
				}
				break;
			default:
				ERR("INVALID ana state: 0x %x, PLEASE CHECK YOUR CODE!!!\n", pstruPacket->anaState);
				return PACK_INVALID_ANA_STATE;
		}
	} while (1);

	return PACK_SUCC;

}

static PACK_ERR _packet_cmd(PACKET_T *pstruPacket, PACKET_CMD cmd, void *pCmdBuff, unsigned int uCmdLen, void *pRspBuff, unsigned int uRspSize, unsigned int *puRspLen, int iWaitMs)
{
	switch (cmd) {
		case PACK_CMD_WAKEUPFD_SET:
			if (NULL == pCmdBuff) {
				ERR("pCmdBuff = NULL...\n");
				return PACK_INVALID_INPUT;
			}
			pstruPacket->struParam.iWakeUpFd = atoi((char *)pCmdBuff);
			break;
		default:
			WARN("UNKNOWN cmd %d\n", (int)cmd);
			return PACK_UNKNOWN_CMD;
	}

	return PACK_SUCC;

}

#ifdef __cplusplus
extern "C" {
#endif

int packet_open(const PACKET_PARAM_T *pstruParam, PACK_ERR *pErr)
{
	PACK_ERR err;
	PACKET_T *pstruPacket = NULL;

	err = packet_check(pstruParam);
	if (PACK_SUCC != err) {
		PACK_ERR_SET(pErr, err);
		return -1;
	}

	err = packet_init(pstruParam, &pstruPacket);
	if (PACK_SUCC != err) {
		PACK_ERR_SET(pErr, err);
		return -1;
	}

	err = packet_main(pstruPacket);
	if (PACK_SUCC != err) {
		PACK_ERR_SET(pErr, err);
		goto errExit;
	}

	PACK_ERR_SET(pErr, err);

	return packet_2_id(pstruPacket);

errExit:

	if (NULL != pstruPacket) {
		packet_fini(pstruPacket);
	}
	
	return -1;
	
}

int packet_read(int iId, void *pBuff, unsigned int uBuffSize, unsigned int *puReadLen, int iWaitMs, PACK_ERR *pErr)
{
	PACKET_T *pstruPacket;
	PACK_ERR err;

	pstruPacket = id_2_packet(iId);
	if (NULL == pstruPacket) {
		PACK_ERR_SET(pErr, PACK_INVALID_ID);
		return -1;
	}
	if (NULL == pBuff || 0 == uBuffSize) {
		ERR("INVALID input param, pBuff = %p, uBuffSize = %u\n", pBuff, uBuffSize);
		PACK_ERR_SET(pErr, PACK_INVALID_INPUT);
		return -1;
	}

	pthread_mutex_lock(&pstruPacket->struMSem);
	{
		if (!pstruPacket->bInuse) {
			ERR("BAD packet id: %d\n", iId);
			PACK_ERR_SET(pErr, PACK_BAD_ID);
			pthread_mutex_unlock(&pstruPacket->struMSem);
			return -1;
		}
		pstruPacket->iRefCount++;
	}
	pthread_mutex_unlock(&pstruPacket->struMSem);

	err = _packet_read(pstruPacket, pBuff, uBuffSize, puReadLen, iWaitMs);
	PACK_ERR_SET(pErr, err);

	pthread_mutex_lock(&pstruPacket->struMSem);
	{
		pstruPacket->iRefCount--;
		if (pstruPacket->iRefCount <= 0) {
			pthread_cond_signal(&pstruPacket->struCond);
		}
	}
	pthread_mutex_unlock(&pstruPacket->struMSem);

	return (PACK_SUCC == err) ? 0 : -1;

}

int packet_cmd(int iId, PACKET_CMD cmd, void *pCmdBuff, unsigned int uCmdLen, void *pRspBuff, unsigned int uRspSize, unsigned int *puRspLen, int iWaitMs, PACK_ERR *pErr)
{
	PACKET_T *pstruPacket;
	PACK_ERR err;

	pstruPacket = id_2_packet(iId);
	if (NULL == pstruPacket) {
		PACK_ERR_SET(pErr, PACK_INVALID_ID);
		return -1;
	}

	pthread_mutex_lock(&pstruPacket->struMSem);
	{
		if (!pstruPacket->bInuse) {
			ERR("BAD packet id: %d\n", iId);
			PACK_ERR_SET(pErr, PACK_BAD_ID);
			pthread_mutex_unlock(&pstruPacket->struMSem);
			return -1;
		}
		pstruPacket->iRefCount++;
	}
	pthread_mutex_unlock(&pstruPacket->struMSem);

	err = _packet_cmd(pstruPacket, cmd, pCmdBuff, uCmdLen, pRspBuff, uRspSize, puRspLen, iWaitMs);
	PACK_ERR_SET(pErr, err);

	pthread_mutex_lock(&pstruPacket->struMSem);
	{
		pstruPacket->iRefCount--;
		if (pstruPacket->iRefCount <= 0) {
			pthread_cond_signal(&pstruPacket->struCond);
		}
	}
	pthread_mutex_unlock(&pstruPacket->struMSem);

	return (PACK_SUCC == err) ? 0 : -1;

}

int packet_close(int iId, PACK_ERR *pErr)
{
	PACKET_T *pstruPacket;
	int iRet = -1;
	PACK_ERR err;

	pstruPacket = id_2_packet(iId);
	if (NULL == pstruPacket) {
		PACK_ERR_SET(pErr, PACK_INVALID_ID);
		return -1;
	}

	pthread_mutex_lock(&pstruPacket->struMSem);
	{
		if (!pstruPacket->bInuse) {
			ERR("BAD packet id: %d\n", iId);
			PACK_ERR_SET(pErr, PACK_BAD_ID);
			pthread_mutex_unlock(&pstruPacket->struMSem);
			return -1;
		}
		while (pstruPacket->iRefCount > 0) {
			DBG("PACKET id = %d, iRefCount = %d, still wait\n", iId, pstruPacket->iRefCount);
			iRet = pthread_cond_wait(&pstruPacket->struCond, &pstruPacket->struMSem);
			if (0 != iRet) {
				ERR("FAIL to pthread_cond_wait, %s\n", strerror(errno));
				PACK_ERR_SET(pErr, PACK_PTHREAD_COND_WAIT_FAIL);
				pthread_mutex_unlock(&pstruPacket->struMSem);
				return -1;
			}
		}
		err = packet_fini(pstruPacket);
	}
	pthread_mutex_unlock(&pstruPacket->struMSem);

	PACK_ERR_SET(pErr, err);

	return (PACK_SUCC == err) ? 0 : -1;

}

const char * packet_strerr(PACK_ERR err)
{
	return packet_strtype(sg_astruPacketErrNoTab, ARRAY_SIZE(sg_astruPacketErrNoTab), (int)err);

}

void packet_dbg_set(int bEnablePackDbg)
{
	sg_bEnablePacketDbg = bEnablePackDbg;
	return;

}

#ifdef __cplusplus
}
#endif


