#include "sc_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>

extern int read_line(int nSock, char* pszBuf, int nMaxBufSize);

#define SC_MIN(x, y) ((x > y)?y:x)

typedef void (*TelnetCallback_Fn)(void* pSession, char* pszBuf, int nLen);

typedef struct SCTelnetServer_S
{
    int         m_nServSock;
    int         m_nTerm;
    pthread_t*  m_pThread;
    TelnetCallback_Fn m_fnCallback;
}SCTelnetServer_T;


typedef struct SCTelnetSession_S
{
    int         m_nSock;
    int         m_nExit;
    int         m_nIndex; 
    pthread_t     m_sThread;
    TelnetCallback_Fn m_fnCallback;
}SCTelnetSession_T;

SCTelnetServer_T*      g_pTelnet = NULL;
SCTelnetSession_T* g_arrSession[MAX_SESSION_CNT];


static void 
_telnet_server_destroy(SCTelnetServer_T* pTelnet)
{    
    if(!pTelnet)
        return;

    if(pTelnet->m_nServSock >= 0)
        close(pTelnet->m_nServSock);

    free(pTelnet);
}


static SCTelnetServer_T*
_telnet_server_create(TelnetCallback_Fn fnCallback)
{    
    SCTelnetServer_T* pTelnet = NULL;
    struct sockaddr_in sServAddr;
    int nOpt = 1;

    pTelnet = (SCTelnetServer_T*)malloc(sizeof(SCTelnetServer_T));
    if(!pTelnet)
        goto ERROR;

    memset(pTelnet, 0x00, sizeof(SCTelnetServer_T));

    pTelnet->m_nServSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    pTelnet->m_fnCallback = fnCallback;

    if(pTelnet->m_nServSock <  0)
    {
        goto ERROR;
    }

    memset(&sServAddr, 0x00, sizeof(struct sockaddr_in));
    sServAddr.sin_family = AF_INET;
    sServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sServAddr.sin_port = htons(DEFAULT_SERV_PORT);

    if(setsockopt(pTelnet->m_nServSock, SOL_SOCKET, SO_REUSEADDR, &nOpt, sizeof(nOpt)) < 0)
    {
        goto ERROR;
    }

    if(bind(pTelnet->m_nServSock, (struct sockaddr *)&sServAddr, sizeof(struct sockaddr_in)) < 0)
    {
        goto ERROR;
    }

    if(listen(pTelnet->m_nServSock, 5) < 0)
    {
        goto ERROR;
    }

    goto EXIT;
ERROR:
    _telnet_server_destroy(pTelnet);
    pTelnet = NULL;    
EXIT:

    return pTelnet;
}


static SCTelnetSession_T*
_session_pool_index_alloc(int nSock, TelnetCallback_Fn fnCallback)
{
    SCTelnetSession_T* pSession = NULL;
    int ii = 0;

    for(ii = 0; ii < MAX_SESSION_CNT; ii++)
    {
        if(g_arrSession[ii] == NULL)
            break;
    }

    if(ii == MAX_SESSION_CNT)
        return NULL;

    pSession = (SCTelnetSession_T*)malloc(sizeof(SCTelnetSession_T));
    if(!pSession)
        return NULL;
    
    pSession->m_nSock = nSock;
    pSession->m_nIndex = ii;
    pSession->m_fnCallback = fnCallback;

    g_arrSession[ii] = pSession;

    return pSession;
}


static void 
_session_pool_index_free(SCTelnetSession_T* pSession)
{
    if(!pSession)
        return;

    g_arrSession[pSession->m_nIndex] = NULL;
    free(pSession);
}

char charactor_mode[] = {255, 251, 1, 255, 251, 3, 255, 252, 34};

static void*
_telnet_session_thread_proc(void* pArg)
{
    int rc;
    char szLine[2048];
    SCTelnetSession_T* pSession = (SCTelnetSession_T*)pArg;
    int nRead = 0;
    
    if(!pSession)
        goto EXIT;

    pSession->m_nExit = 0;

    // Mode Setting
    send(pSession->m_nSock, charactor_mode, sizeof(charactor_mode), 0);
    // Flush Response
    recv(pSession->m_nSock, szLine, 1, MSG_PEEK);
    if(szLine[0] == -1)
    {
        rc = read(pSession->m_nSock, szLine, 2048);
        if(rc < 0)
            return NULL;
    }

    strcpy(szLine, "\r\n"SC_CLI_WELCOME_MSG"\r\n");
    send(pSession->m_nSock, szLine, strlen(szLine), 0);

    while(!pSession->m_nExit)
    {
        strcpy(szLine, SC_CLI_PROMPT);
        send(pSession->m_nSock, szLine, strlen(szLine), 0);

        if((nRead = read_line(pSession->m_nSock, szLine, 2048)) < 0)
            break;

        if(pSession->m_fnCallback)
            pSession->m_fnCallback((void*)pSession, (char*)szLine, nRead);
    }

EXIT:
    close(pSession->m_nSock);
    _session_pool_index_free(pSession);

    return NULL;
}


static void 
_create_telnet_session(int nSock, TelnetCallback_Fn fnCallback)
{
    SCTelnetSession_T* pSession = _session_pool_index_alloc(nSock, fnCallback);
//    pthread_attr_t sThreadAttr;

    if(!pSession)
        goto ERROR;
    
//    pthread_attr_init(&sTrheadAttr);

    if(pthread_create(&pSession->m_sThread, NULL, _telnet_session_thread_proc, pSession) < 0)
        goto ERROR;

    goto EXIT;
ERROR:
    if(pSession)
        _session_pool_index_free(pSession);

EXIT:
    return;
}


void
stop_telnet_session(void* pArg)
{
    void* pRet = NULL;
    SCTelnetSession_T* pSession = (SCTelnetSession_T*)pArg;
    if(pSession == NULL)
        return;

    printf(">>>> Enter stop_telnet_session() !!!!\n");
    pSession->m_nExit = 1;

    pthread_join(pSession->m_sThread, &pRet);
    printf("<<<<< Exit stop_telnet_session() !!!!\n");
}


static void* 
_telnet_thread_proc(void *pArg)
{
    SCTelnetServer_T* pTelnet = (SCTelnetServer_T*)pArg;
    int nCnt = 0;
    int nLastFd = pTelnet->m_nServSock + 1;
    struct timeval sWait;
    fd_set sReadFds;
    int  nClientSock= -1;
    socklen_t nClientAddr= 0;
    struct sockaddr_in  sClientAddr;

    while(!pTelnet->m_nTerm)
    {
        FD_ZERO(&sReadFds);
        FD_SET(pTelnet->m_nServSock, &sReadFds);

        sWait.tv_sec = 1;
        sWait.tv_usec = 0; //100000; /* 0.1 sec */

        nCnt = select(nLastFd, &sReadFds, NULL, NULL, &sWait);
        if(nCnt < 1)
            continue;

        nClientSock = accept(pTelnet->m_nServSock, (struct sockaddr*)&sClientAddr, &nClientAddr);
        if(nClientSock < 0)
            continue;

        _create_telnet_session(nClientSock, pTelnet->m_fnCallback);
    }

    return NULL;
}


static int
_telnet_server_start(SCTelnetServer_T* pTelnet)
{
    int nRet = 0;

    if(!pTelnet)
    {
        nRet = -1;
        goto ERROR;
    }

    if(pTelnet->m_pThread)
    {
        nRet = -2;
        goto ERROR;
    }

    pTelnet->m_nTerm = 0;

    pTelnet->m_pThread = (pthread_t*)calloc(1, sizeof(pthread_t));
    if(!pTelnet->m_pThread)
    {
        nRet = -3;
        goto ERROR;
    }

    if(pthread_create(pTelnet->m_pThread, NULL, _telnet_thread_proc, pTelnet) < 0)
    {
        nRet = -4;
        goto ERROR;
    }

    goto EXIT;
ERROR:
    if(pTelnet && pTelnet->m_pThread)
    {
        free(pTelnet->m_pThread);
        pTelnet->m_pThread = NULL;
    }
EXIT:
    return nRet;
}


static void 
_telnet_server_stop(SCTelnetServer_T* pTelnet)
{
    void* pRet = NULL;
    
    if(!pTelnet->m_pThread)
        return;

    pTelnet->m_nTerm = 1;
    pthread_join(*pTelnet->m_pThread, &pRet);
    
    free(pTelnet->m_pThread);
    pTelnet->m_pThread = NULL;
}


int 
sc_telnet_init(TelnetCallback_Fn fnCallback)
{
    int nRet = 0;

    if(g_pTelnet)
    {
        nRet = -1;
        goto EXIT;
    }

    g_pTelnet = _telnet_server_create(fnCallback);
    if(!g_pTelnet)
    {
        nRet = -2;
        goto EXIT;
    }

    nRet = _telnet_server_start(g_pTelnet);    
EXIT:
    return nRet;
}


void sc_telnet_deinit()
{
    if(!g_pTelnet)
        goto EXIT;

    _telnet_server_stop(g_pTelnet);
    _telnet_server_destroy(g_pTelnet);

    g_pTelnet = NULL;

EXIT:
    return;
}


SCCli_T* g_pCliList = NULL;

int
sc_strcmp(const char* pszVal1, const char* pszVal2)
{
    int nLen1 = strlen(pszVal1);
    int nLen2 = strlen(pszVal2);

    int nCompare = SC_MIN(nLen1, nLen2);
    int nRet = 0;
    int ii = 0;

    for(ii = 0; ii <nCompare; ii++)
    {
        if(pszVal1[ii] == pszVal2[ii])
            continue;

        nRet = (pszVal1[ii] > pszVal2[ii])?(-1):(1);

        break;
    }

    if(nRet == 0 && nLen1 != nLen2)
        nRet = (nLen1 > nLen2)?(-1):(1);

    return nRet;
}

void sc_cli_out(void* pArg, const char* pszFmt, ...)
{
    SCTelnetSession_T* pSession = (SCTelnetSession_T*)pArg;
    char* pMsg = NULL;
    char* pNewMsg = NULL;
    int nMaxMsg = 100;
    int nLen = 0;

    va_list sVaList;

    if(!pArg)
        return;
    
    if((pMsg = (char*)malloc(nMaxMsg)) == NULL)
        return;

    while(1)
    {
        va_start(sVaList, pszFmt);
        nLen = vsnprintf(pMsg, nMaxMsg, pszFmt, sVaList);
        if(nLen > -1 && nLen < nMaxMsg)
            break;
        if(nLen > -1)
            nMaxMsg = nLen + 1;
        else
            nMaxMsg *= 2;

        if((pNewMsg = (char*)realloc(pMsg, nMaxMsg)) == NULL)
        {
            free(pMsg);
            return;
        }
        else
        {
            pMsg = pNewMsg;
        }
    }

    // Convert \n -> \r\n
    {
        int nLen = strlen(pMsg);
        int nNewLen = 0;
        int ii = 0;
        int nRetCnt = 0;
        for(ii = 0; ii < nLen; ii++)
        {
            if(pMsg[ii] == '\n')
            {
                if(ii - 1 < 0)
                    nRetCnt++;
                else if(pMsg[ii -1] != '\r')
                    nRetCnt++;
            }
        }

        if(nRetCnt == 0)
            goto EXIT;

        nNewLen = nLen + nRetCnt;
        if(nMaxMsg < nNewLen + 1)
        {
            nMaxMsg = nNewLen + 1;
            if((pNewMsg =  (char*)realloc(pMsg, nMaxMsg)) == NULL)
            {
                free(pMsg);
                return;
            }
            pMsg = pNewMsg;    
        }

        pMsg[nNewLen--] = '\0';
        for(ii = nLen - 1; ii >= 0; ii--)
        {
            pMsg[nNewLen--] = pMsg[ii];
            if(pMsg[ii] == '\n')
            {
                if(ii == 0)
                    pMsg[ii] = '\r';
                else if(pMsg[ii -1] != '\r')
                    pMsg[nNewLen--] = '\r';
            } 
        }
    }

EXIT:
    send(pSession->m_nSock, pMsg, strlen(pMsg), 0);
    free(pMsg);
    
    return;
}

void sc_cli_out_all(const char* pszFmt, ...)
{
    char* pMsg = NULL;
    char* pNewMsg = NULL;
    int nMaxMsg = 100;
    int nLen = 0;

    va_list sVaList;

    if((pMsg = (char*)malloc(nMaxMsg)) == NULL)
        return;

    while(1)
    {
        va_start(sVaList, pszFmt);
        nLen = vsnprintf(pMsg, nMaxMsg, pszFmt, sVaList);
        if(nLen > -1 && nLen < nMaxMsg)
            break;
        if(nLen > -1)
            nMaxMsg = nLen + 1;
        else
            nMaxMsg *= 2;

        if((pNewMsg = (char*)realloc(pMsg, nMaxMsg)) == NULL)
        {
            free(pMsg);
            return;
        }
        else
        {
            pMsg = pNewMsg;
        }
    }

    // Convert \n -> \r\n
    {
        int nLen = strlen(pMsg);
        int nNewLen = 0;
        int ii = 0;
        int nRetCnt = 0;
        for(ii = 0; ii < nLen; ii++)
        {
            if(pMsg[ii] == '\n')
            {
                if(ii - 1 < 0)
                    nRetCnt++;
                else if(pMsg[ii -1] != '\r')
                    nRetCnt++;
            }
        }

        if(nRetCnt == 0)
            goto EXIT;

        nNewLen = nLen + nRetCnt;
        if(nMaxMsg < nNewLen + 1)
        {
            nMaxMsg = nNewLen + 1;
            if((pNewMsg =  (char*)realloc(pMsg, nMaxMsg)) == NULL)
            {
                free(pMsg);
                return;
            }
            pMsg = pNewMsg;    
        }

        pMsg[nNewLen--] = '\0';
        for(ii = nLen - 1; ii >= 0; ii--)
        {
            pMsg[nNewLen--] = pMsg[ii];
            if(pMsg[ii] == '\n')
            {
                if(ii == 0)
                    pMsg[ii] = '\r';
                else if(pMsg[ii -1] != '\r')
                    pMsg[nNewLen--] = '\r';
            } 
        }
    }

EXIT:
    {
        int ii = 0;
        for(; ii < MAX_SESSION_CNT; ii++)
            if(g_arrSession[ii] != NULL)    
                send(g_arrSession[ii]->m_nSock, pMsg, strlen(pMsg), 0);
    }
    free(pMsg);
    
    return;
    
}

void sc_install_cli(SCCli_T* pNewCli)
{
    SCCli_T* pCli = NULL;
    SCCli_T* pPrev = NULL;
    int nRet = 0;
    
    pCli = g_pCliList;

    while(pCli)
    {
        nRet = sc_strcmp(pCli->m_pszCli, pNewCli->m_pszCli);
        if(nRet == 0)
            return;
        if(nRet < 0)
            break;

        pPrev = pCli;
        pCli = pCli->m_pNext;
    }

    if(pPrev == NULL)
    {
        if(g_pCliList != NULL)
            pNewCli->m_pNext = g_pCliList;
            
        g_pCliList = pNewCli;
    }
    else
    {
        pNewCli->m_pNext = pPrev->m_pNext;
        pPrev->m_pNext = pNewCli;
    }
}


#define DELIM       " \t"
void sc_cmd_callback(void* pSession, char* pszBuf, int nLen)
{
    char* pszToken = NULL;
    int argc = 0;
    char* argv[1024];
    SCCli_T* pCli;

    if(!pszBuf || !nLen)
        return;

    pszToken = strtok((char*)pszBuf, DELIM);
    if(!pszToken)
        return;

    do
    {
        argv[argc++] = (char*)strdup(pszToken);
    }
    while((pszToken = strtok(NULL, DELIM)));

    pCli = g_pCliList;
    
    while(pCli)
    {
        if(!strcmp(pCli->m_pszCli, argv[0]))
            break;

        pCli = pCli->m_pNext;
    }

    if(!pCli)
        sc_cli_out(pSession, "Cannot Found Command(%s), help show cli list..\n", argv[0]);
    else
    {
        int nRet = pCli->m_fnCli(pSession, argc, argv);
        if(nRet != 0)
            sc_cli_out(pSession, "%s failed.. ret(%d)\n", pCli->m_pszCli, nRet);
    }
    while(argc > 0)
        free(argv[--argc]);    
}


CLI( help, "help")
{
    SCCli_T* pCli = g_pCliList;

    sc_cli_out(session, "- CLI List:\n");
    while(pCli)
    {
        sc_cli_out(session, "   %-32s %s\n", pCli->m_pszCli, pCli->m_pszHelp);
        pCli = pCli->m_pNext;
    }
    
    return 0;
}

CLI( exit, "exit cli")
{
    stop_telnet_session(session);
    
    return 0;
}

int sc_cli_init()
{
    INSTALL_CLI(help);
    INSTALL_CLI(exit);

    return sc_telnet_init(sc_cmd_callback);
}

void sc_cli_deinit()
{
    SCCli_T* pCli = NULL;

    sc_telnet_deinit();

    while(g_pCliList)
    {
        pCli = g_pCliList->m_pNext;

        g_pCliList->m_pNext = NULL;

        g_pCliList = pCli;
    }
}
