
#ifndef __APP_FTP_PRINTF_H__
#define __APP_FTP_PRINTF_H__

typedef  int BOOL;

#define TRUE 1
#define FALSE 0

#ifdef __cplusplus
extern "C" {
#endif

int app_ftp_printf_init(void);

int app_ftp_printf(const char *fmt, ...);

void app_ftp_printf_status(void);

int app_stor_printf_part_add(const char *pszPartPath);

int app_stor_printf_part_remove(const char *pszPartPath);

int app_stor_printf_search_async(time_t tStart, time_t tEnd, const char *pszFtpURL, BOOL bUploadSp, BOOL bCompress);

int app_stor_printf_spfiles_print(unsigned int uPrtMask, char *pszPrtBuff, unsigned int uBuffSize);

int app_stor_printf_search_stopall(void);

int app_stor_printf_close(void);

#ifdef __cplusplus
}
#endif

#endif


