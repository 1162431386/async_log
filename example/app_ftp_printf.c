#include <sys/prctl.h>
#include <pthread.h>
#include "ftp_printf.h"
#include "stor_printf.h"
#include "app_ftp_printf.h"


typedef struct {
    pthread_mutex_t struMutex;
    BOOL bInit;
    int iFtpPrtId;
    int iStorPrtId;
}APP_FTP_PRINTF_PRT_T;

static APP_FTP_PRINTF_PRT_T sg_struAppFtpPrintPrt = {
    .struMutex = PTHREAD_MUTEX_INITIALIZER,
    .bInit = FALSE,
    .iFtpPrtId = -1,
    .iStorPrtId = -1,
};


int app_stor_printf_search(time_t tStart, time_t tEnd, const char *pszFtpURL, BOOL bUploadSp, BOOL bCompress, sem_t *pstruSyncSem)
{
    APP_FTP_PRINTF_PRT_T *p = &sg_struAppFtpPrintPrt;
    const char *_pszFtpURL = NULL;
    enum ftp_printf_errno ftpErr = FTP_PRINTF_SUCC;
    int iFtpPrtId = -1;
    STOR_PRINTF_SE_HANDLE hSpSe = NULL;
    struct stor_printf_spfile_t struSpFile;
    STOR_PRINTF_SE_HANDLE hSe = NULL;
    struct stor_printf_search_param_t struSeParam;
    enum STOR_PRINTF_ERRNO errNo = STOR_PRINTF_SUCC;
    int iRet = -1;
    char szBuff[1024] = {0};

    if (NULL != pszFtpURL) {
        _pszFtpURL = strdup(pszFtpURL);
    }
    if (NULL != pstruSyncSem) {
        sys_sem_post(pstruSyncSem);
    }

    if (0 == tStart || 0 == tEnd || tStart > tEnd) {
        ERR("INVALID time range, tStart: %u, tEnd: %u\n", (UINT32)tStart, (UINT32)tEnd);
        return -1;
    }
    if (!p->bInit || -1 == p->iStorPrtId) {
        ERR("p->bInit = %d, p->iStorPrtId = %d\n", p->bInit, p->iStorPrtId);
        return -1;
    }

    
    if (NULL != _pszFtpURL && '\0' != _pszFtpURL[0]) {
        iFtpPrtId = app_stor_printf_ftpprt_open(tStart, tEnd, _pszFtpURL, bUploadSp);
    }
    
    memset((char *)&struSeParam, 0, sizeof (struSeParam));
    struSeParam.starttime = tStart;
    struSeParam.endtime = tEnd;

    if (1)
    {
        /* ¿ªÆô¼ìË÷ */
        hSpSe = stor_printf_search_start(p->iStorPrtId, &struSeParam, &errNo);
        if (NULL == hSpSe) {
            if (-1 == iFtpPrtId) {
                ERR("FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to start stor printf spfiles search, %s\n", stor_printf_strerr(errNo));
            }
            iRet = -1;
            goto exit;
        }
        /* ¼ìË÷´¦Àí */
        if (stor_printf_search_spfile_count(p->iStorPrtId, hSpSe, NULL) > 0) {
            if (-1 == app_stor_printf_search_spfiles_print(p->iStorPrtId, hSpSe, iFtpPrtId)) {
                iRet = -1;
                goto exit;
            }
        }
        else {
            app_stor_printf_all_spfiles_print(p->iStorPrtId, hSpSe, iFtpPrtId);
        }
        
        /* ¹Ø±Õ¼ìË÷ */
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSpSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                WARN("FAIL to stop spfile stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop spfile stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
        }
        hSpSe = NULL;
    }

    if (-1 == iFtpPrtId && '\0' != _pszFtpURL[0]) {
        iRet = -1;
        goto exit;
    }
    if (2)
    {
        hSe = stor_printf_search_start(p->iStorPrtId, &struSeParam, &errNo);
        if (NULL == hSe) {
            if (-1 == iFtpPrtId) {
                ERR("FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to start stor printf search, %s\n", stor_printf_strerr(errNo));
            }
            iRet = -1;
            goto exit;
        }

        memset(szBuff, 0, sizeof (szBuff));

        if (bUploadSp) {
            if (-1 != iFtpPrtId) {
                do {
                    iRet = stor_printf_search_spfile_next(p->iStorPrtId, hSe, &struSpFile, &errNo);
                    if (-1 == iRet) {
                        if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                            break;
                        }
                        ftp_printf(iFtpPrtId, "FAIL to spfile search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                        goto exit;
                    }         
                    if (-1 == ftp_printf_file_upload(iFtpPrtId, struSpFile.path, &ftpErr)) 
                    {
                        ERR("FAIL to upload sp file: %s => ftp, %s\n", struSpFile.path, ftp_printf_strerr(ftpErr));
                        goto exit;
                    }
                    
                } while (1);
            }
        } 

        else if(bCompress)
        /*Ñ¹Ëõ²Ù×÷*/          
        {
            if (-1 != iFtpPrtId) 
            {
                do {
                    iRet = stor_printf_search_spfile_next(p->iStorPrtId, hSe, &struSpFile, &errNo);
                    if (-1 == iRet) {
                        if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                            break;
                        }
                        ERR("FAIL to spfile search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                        goto exit;
                    }   

                    if(-1 == app_store_printf_compress_upload(iFtpPrtId, struSpFile.path, pszFtpURL, tStart, tEnd, &ftpErr))
                    {
                        ERR("FAIL to upload  compress sp file: %s => ftp, %s\n", struSpFile.path, ftp_printf_strerr(ftpErr));
                        goto exit;  
                    } 
                            
                } while (1);
            }
           
        }  
        else
        {
            do {
                iRet = stor_printf_search_next(p->iStorPrtId, hSe, szBuff, sizeof (szBuff), 3000, &errNo);
                if (-1 == iRet) {
                    if (STOR_PRINTF_SEARCH_COMPLETE == errNo) {
                        if (-1 == iFtpPrtId) {
                            INFO("search complete!\n");
                        }
                        else {
                            ftp_printf(iFtpPrtId, "search complete!\n");
                        }
                        break;
                    }
                    if (-1 == iFtpPrtId) {
                        ERR("FAIL to search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                    }
                    else {
                        ftp_printf(iFtpPrtId, "FAIL to search next, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
                    }
                    goto exit;
                }
                else if (0 == iRet) {
                    if (-1 == iFtpPrtId) {
                        WARN("searching...\n");
                    }
                    else {
                        ftp_printf(iFtpPrtId, "searching...\n");
                    }
                    continue;
                }
                if (-1 == iFtpPrtId) {
                    INFO("+++ %s", szBuff);
                }
                else {
                    ftp_printf(iFtpPrtId, "%s", szBuff);
                }
            } while (1);
        }
    }
    
    iRet = 0;

exit:

    if (NULL != hSe) {
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                WARN("FAIL to stop stor printf search, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop stor printf search, iStorPrtId = %d, hSe = %p, %s\n", p->iStorPrtId, hSe, stor_printf_strerr(errNo));
            }
        }
        hSe = NULL;
    }

    if (NULL != hSpSe) {
        if (-1 == stor_printf_search_stop(p->iStorPrtId, hSpSe, &errNo)) {
            if (-1 == iFtpPrtId) {
                WARN("FAIL to stop stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
            else {
                ftp_printf(iFtpPrtId, "FAIL to stop stor printf search, iStorPrtId = %d, hSpSe = %p, %s\n", p->iStorPrtId, hSpSe, stor_printf_strerr(errNo));
            }
        }
        hSpSe = NULL;
    }

    if (-1 != iFtpPrtId) {
        ftp_printf_close(iFtpPrtId, NULL);
        iFtpPrtId = -1;
    }

    return iRet;

}




#if 0
static int app_ftp_printf_prt_init(APP_FTP_PRINTF_PRT_T *p)
{
    if (-1 == p->iFtpPrtId) {
        p->iFtpPrtId = app_ftp_printf_open(&struParam);
        if (-1 == p->iFtpPrtId) {
            goto next;
        }
    }
  
}

int app_ftp_printf_init(void)
{
    APP_FTP_PRINTF_PRT_T *p = &sg_struAppFtpPrintPrt;
	
    pthread_mutex_lock(&p->struMutex);
    {
        if (!p->bInit) {
            p->iFtpPrtId = app_ftp_printf_open(&struParam);
            if (-1 == p->iFtpPrtId) {
                pthread_mutex_unlock(&p->struMutex);
                return -1;;
            }
            p->bInit = TRUE;
        }
    }
    pthread_mutex_unlock(&p->struMutex);

    return 0;

}
#endif