
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <regex.h>
#include "common_utils.h"

#define COMMON_UTILS_INFO_print
#ifdef COMMON_UTILS_INFO_print
#define INFO(fmt, args...) printf("I[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define INFO(fmt, args...)
#endif

#define COMMON_UTILS_WARN_print
#ifdef COMMON_UTILS_WARN_print
#define WARN(fmt, args...) printf("W[%s:%4d] " fmt, __FILE__, __LINE__, ##args)
#else
#define WARN(fmt, args...)
#endif

#define COMMON_UTILS_ERR_print
#ifdef COMMON_UTILS_ERR_print
#define ERR(fmt, args...) printf("\033[31mE[%s:%4d]\033[0m " fmt, __FILE__, __LINE__, ##args)
#else
#define ERR(fmt, args...)
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char * strtype(const TYPE_STR_T astruTable[], unsigned int uArraySize, int iType)
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

int pthreadIdVerify(pthread_t tid)
{
    if ((pthread_t)-1 == tid || (pthread_t)0 == tid) {
        return -1;
    }

    return pthread_kill(tid, 0);

}

const char * strtime(time_t t, char *pszTime, unsigned int uStrSize)
{
    struct tm struTm;

    if (NULL == pszTime || 0 == uStrSize) {
        return "INVALID INPUT";
    }

    gmtime_r(&t, &struTm);

    snprintf(pszTime, uStrSize, "%02d%02d%02d_%02d%02d%02d", 
        struTm.tm_year + 1900, struTm.tm_mon + 1, struTm.tm_mday, 
        struTm.tm_hour, struTm.tm_min, struTm.tm_sec);

    return (const char *)pszTime;

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
		ERR("Fail to compile regex: \"%s\", %s\n", pattern, reg_errstr);
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

int str_seg_replace(const char *pszSrcStr, const char *pszSrcDelim, const char *pszDstDelim, char *pszDstBuf, int iDstBufLen)
{
	char *p = NULL;
	char *s = NULL;
	char *pToken = NULL;
	
	if (NULL == pszSrcStr || '\0' == pszSrcStr[0] || NULL == pszSrcDelim \
		|| '\0' == pszSrcDelim[0] || NULL == pszDstDelim \
		|| '\0' == pszDstDelim[0] || NULL == pszDstBuf || iDstBufLen <= 0) {
		ERR("%s %d error param in, pszSrcStr:%p pszDelim:%p pszDstDelim:%s pszDstBuf:%p iDstBufLen:%d\n", \
			__FUNCTION__, __LINE__, pszSrcStr, pszSrcDelim, pszDstDelim, pszDstBuf, iDstBufLen);
			return -1;
	}
	
	memset(pszDstBuf, 0, iDstBufLen);
	
	s = strdup(pszSrcStr);
	if (NULL == s) {
		ERR("%s %d strdup failed!\n", __FUNCTION__, __LINE__);
		return -1;
	}
	
	p = s;
	for(pToken = strsep(&s, pszSrcDelim); pToken != NULL && (iDstBufLen - strlen(pszDstBuf) - 1) >= strlen(pToken); pToken = strsep(&s, pszSrcDelim)) {
		if (NULL != s) {
			/*s为NULL时已经到达末尾*/
			snprintf(pszDstBuf + strlen(pszDstBuf), iDstBufLen - strlen(pszDstBuf) - 1,"%s%s", pToken, pszDstDelim);
		}else {
			snprintf(pszDstBuf + strlen(pszDstBuf), iDstBufLen - strlen(pszDstBuf) - 1,"%s", pToken);
		}
	}

	free(p);
	
	return 0;
	
}

#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)
#define YEAR   (365*DAY)
static int month[12] = 
{   //－－每月初所经过的秒数
    0,
    DAY*(31),
    DAY*(31+29),
    DAY*(31+29+31),
    DAY*(31+29+31+30),
    DAY*(31+29+31+30+31),
    DAY*(31+29+31+30+31+30),
    DAY*(31+29+31+30+31+30+31),
    DAY*(31+29+31+30+31+30+31+31),
    DAY*(31+29+31+30+31+30+31+31+30),
    DAY*(31+29+31+30+31+30+31+31+30+31),
    DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

unsigned int myself_mktime(struct tm *tm)
{
    unsigned int res;
    int year;
    year = tm->tm_year - 70;                       //－－年数
    /* magic offsets (y+1) needed to get leapyears right.*/
    res = YEAR * year + DAY * ((year+1)/4);       //－－年数＋闰年数
    res += month[tm->tm_mon];                      //－－月
    /* and (y+2) here. If it wasn't a leap-year, we have to adjust */
    if ((tm->tm_mon > 1) && ((year+2) % 4))      //－－若(y+2)不是闰年，则减一天
    {
        res -= DAY;
    }
    res += DAY * (tm->tm_mday-1);                  //－－当月已经过的天数
    res += HOUR * tm->tm_hour;                     //－－当天已经过的小时数
    res += MINUTE * tm->tm_min;                    //－－此时已经过的分钟
    res += tm->tm_sec;                             //－－此分已经过的秒
    return res;
}

int strStartsWith(const char *line, const char *prefix)
{
    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    return *prefix == '\0';
}

#ifdef __cplusplus
}
#endif

