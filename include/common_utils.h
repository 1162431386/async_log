
#ifndef _COMMON_UTILS_H_
#define _COMMON_UTILS_H_

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
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
#define SAFE_FREE(ptr) do { \
	if (NULL != (ptr)) { \
		free((ptr)); \
		(ptr) = NULL; \
	} \
} while (0)
#endif

#ifndef SAFE_STRNCPY
#define SAFE_STRNCPY(dst, src, n) do { \
    memset((char *)(dst), 0, (n)); \
    strncpy((dst), (src), n); \
    (dst)[n - 1] = '\0'; \
}while (0)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof ((x)) / sizeof ((x)[0]))
#endif

/* int型数据转换成字符串的结构体 */
typedef struct {
	int iType;
	const char *pszType;
}TYPE_STR_T;

#ifndef TYPE_STR_ITEM
#define TYPE_STR_ITEM(type) {type, #type}
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char * strtype(const TYPE_STR_T astruTable[], unsigned int uArraySize, int iType);

int pthreadIdVerify(pthread_t tid);

const char * strtime(time_t t, char *pszTime, unsigned int uStrSize);

#ifdef __cplusplus
}
#endif

#endif

