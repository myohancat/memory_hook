#include <termios.h>
#include <sys/time.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <sys/stat.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <sys/poll.h> 
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define MAX_READ_LINE_LEN  2048

#define CR        "\r\n"
#define ASCII_ESC     '\x1B'
typedef enum 
{
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_RETURN,
    KEY_END_OF_LINE,
    KEY_BEGIN_OF_LINE,
    KEY_CANCEL_TEXT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_INSERT,
    KEY_DELETE,
    KEY_TAB,
    KEY_CLEAR_LINE,
    KEY_CLEAR_EOL,
    KEY_CTRL_LEFT,
    KEY_CTRL_RIGHT,
    KEY_NUMBERS
}KeyCode_E;

typedef enum 
{
    ACTION_BACKSPACE,
    ACTION_RETURN,
    ACTION_MOVE_EOL,
    ACTION_MOVE_BOL,
    ACTION_HISTORY_UP,
    ACTION_HISTORY_DOWN,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_TOGGLE_INSERT,
    ACTION_DELETE,
    ACTION_COMPLETE,
    ACTION_CLEAR_LINE,
    ACTION_CLEAR_EOL,
    ACTION_ADD_CHAR,
    ACTION_MOVE_BOT,
    ACTION_MOVE_EOT,
    ACTION_CANCEL_TEXT,
    ACTION_EXIT_TERM,
    ACTION_NOACTION
}KeyAct_E;

typedef struct VKey_S
{
    KeyAct_E    m_eKeyAct;    
    char        m_cKey;
}VKey_T;

#define INIT_VKEY(sVKey)    do{                                     \
                                sVKey.m_eKeyAct = ACTION_NOACTION;    \
                                sVKey.m_cKey    = ' ';                \
                            }while(0)

typedef struct ConsolLine_S
{
    char*     m_pBuf;
    int     m_nMaxBufSize;

    int     m_nCnt;    /* Total Inserted Char */

    int        m_nCaretPos; 
}ConsolLine_T;

#define COMPLETE_LINE(sConsolLine)        sConsolLine.m_pBuf[sConsolLine.m_nCnt] = '\0'

typedef struct KeyBind_S
{
    KeyCode_E     m_eKeyCode;
    const char*    m_szKeyString;
    KeyAct_E    m_eKeyAct;
    const char*    m_szKeyAct;    
}KeyBind_T;

static const KeyBind_T s_arrAsciiBind[] = 
{
    { KEY_BACKSPACE,    "\b"           , ACTION_BACKSPACE    , "ACTION_BACKSPACE" },
    { KEY_RETURN,       "\n"           , ACTION_RETURN       , "ACTION_RETURN"    },
    { KEY_RETURN,       "\r"           , ACTION_RETURN       , "ACTION_RETURN"    },
    { KEY_TAB,          "\t"           , ACTION_COMPLETE     , "ACTION_COMPLETE"},
    { KEY_BEGIN_OF_LINE,"\x01"         , ACTION_MOVE_BOL     , "ACTION_MOVE_BOL"},
    { KEY_ARROW_LEFT,   "\x02"         , ACTION_MOVE_LEFT    , "ACTION_MOVE_LEFT"},
    { KEY_CANCEL_TEXT,  "\x03"         , ACTION_CANCEL_TEXT  , "ACTION_CANCEL_TEXT"},
    { KEY_END_OF_LINE,  "\x04"         , ACTION_EXIT_TERM    , "ACTION_EXIT_TERM"},
    { KEY_ARROW_RIGHT,  "\x06"         , ACTION_MOVE_RIGHT   , "ACTION_MOVE_RIGHT"},
    { KEY_BACKSPACE,    "\x08"         , ACTION_BACKSPACE    , "ACTION_BACKSPACE" },
    { KEY_END_OF_LINE,  "\x05"         , ACTION_MOVE_EOL     , "ACTION_MOVE_EOL"},
    { KEY_CLEAR_EOL,    "\x0B"         , ACTION_CLEAR_EOL    , "ACTION_CLEAR_EOL"},
    { KEY_ARROW_DOWN,   "\x0E"         , ACTION_HISTORY_DOWN , "ACTION_HISTORY_DOWN"},
    { KEY_ARROW_UP,     "\x10"         , ACTION_HISTORY_UP   , "ACTION_HISTORY_UP"},
    { KEY_CLEAR_LINE,   "\x15"         , ACTION_CLEAR_LINE   , "ACTION_CLEAR_LINE"},
    { KEY_BACKSPACE,    "\x7F"         , ACTION_BACKSPACE    , "ACTION_BACKSPACE" },
};

static const KeyBind_T s_arrEscSeqBind[] = 
{
    { KEY_BEGIN_OF_LINE,"\x1B[1~"      , ACTION_MOVE_BOL     , "ACTION_MOVE_BOL"},
    { KEY_INSERT,       "\x1B[2~"      , ACTION_TOGGLE_INSERT, "ACTION_TOGGLE_INSERT"},
    { KEY_DELETE,       "\x1B[3~"      , ACTION_DELETE       , "ACTION_DELETE"},
    { KEY_END_OF_LINE,  "\x1B[4~"      , ACTION_MOVE_EOL     , "ACTION_MOVE_EOL"  },
    { KEY_BEGIN_OF_LINE,"\x1B[7~"      , ACTION_MOVE_BOL     , "ACTION_MOVE_BOL"},
    { KEY_END_OF_LINE,  "\x1B[8~"      , ACTION_MOVE_EOL     , "ACTION_MOVE_EOL"  },
    { KEY_CTRL_LEFT,    "\x1B[1;5D"    , ACTION_MOVE_BOT     , "ACTION_MOVE_BOT"},
    { KEY_CTRL_RIGHT,   "\x1B[1;5C"    , ACTION_MOVE_EOT     , "ACTION_MOVE_EOT"},
    { KEY_ARROW_UP,     "\x1B[A"       , ACTION_HISTORY_UP   , "ACTION_HISTORY_UP"},
    { KEY_ARROW_DOWN,   "\x1B[B"       , ACTION_HISTORY_DOWN , "ACTION_HISTORY_DOWN"},
    { KEY_ARROW_RIGHT,  "\x1B[C"       , ACTION_MOVE_RIGHT   , "ACTION_MOVE_RIGHT"},
    { KEY_ARROW_LEFT,   "\x1B[D"       , ACTION_MOVE_LEFT    , "ACTION_MOVE_LEFT"},
    { KEY_END_OF_LINE,  "\x1B[F"       , ACTION_MOVE_EOL     , "ACTION_MOVE_EOL"},
    { KEY_BEGIN_OF_LINE,"\x1B[H"       , ACTION_MOVE_BOL     , "ACTION_MOVE_BOL"},
};

#define KEY_BINDS_COUNT sizeof(s_arrKeyBind) / sizeof (KeyBind_T)
#define MAX_HARDKEY_BUF_LEN 6
#define _SIZEOF(array, type)    (int)(sizeof(array)/sizeof(type))

#define MAX_HISTORY_LEN     5

static char s_arrCmd[MAX_HISTORY_LEN][MAX_READ_LINE_LEN];
static int  s_nFirstPos = 0;
static int  s_nLastPos = 0;

#define HISTORY_EMPTY() (s_nFirstPos == s_nLastPos)
#define HISTORY_FULL()  (s_nFirstPos == (s_nLastPos+ 1) % MAX_HISTORY_LEN)
#define HISTORY_TOTAL() ((s_nLastPos - s_nFirstPos + MAX_HISTORY_LEN) % MAX_HISTORY_LEN)

void remove_history(void)
{
    if(HISTORY_EMPTY())
        return;

    s_nFirstPos = (s_nFirstPos + 1) % MAX_HISTORY_LEN;
}

void insert_history(const char* szCmd)
{
    if(HISTORY_FULL())
        remove_history();

    s_nLastPos =  (s_nLastPos + 1) % MAX_HISTORY_LEN;
    memcpy(s_arrCmd[s_nLastPos], szCmd, MAX_READ_LINE_LEN);
    s_arrCmd[s_nLastPos][MAX_READ_LINE_LEN -1] = '\0';
}

const char* search_history(int nStep)
{
    if(HISTORY_EMPTY())
        return NULL;

    if(nStep < 0)
        return NULL;

    if(HISTORY_TOTAL() <= nStep)
        return NULL;

    return s_arrCmd[(s_nLastPos - nStep + MAX_HISTORY_LEN) % MAX_HISTORY_LEN];
}

int read_key(int nSock, char* pKey)
{
    unsigned int    nReadChar = -1;
    
    struct pollfd     sPollFd;
    
    sPollFd.fd = nSock;
    sPollFd.events = POLLPRI|POLLIN|POLLERR|POLLHUP|POLLNVAL|POLLRDHUP;
    sPollFd.revents = 0;

    if (poll(&sPollFd, 1, -1) > 0)
    {
        if(sPollFd.revents & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL))
            return -1;

        if (sPollFd.revents & POLLIN) 
            nReadChar = read(nSock, pKey, 1);
    }
    
    if(nReadChar < 1)
        return -1;

    return 0;
}

int write_console(int nSock, const char* pszBuf, int nLen)
{
    return write(nSock, pszBuf, nLen);
}

int get_vkey_from_ascii(VKey_T* pVKey, char cIn)
{
    int nIdx;
    
    for(nIdx = 0; nIdx < _SIZEOF(s_arrAsciiBind, KeyBind_T); nIdx++)
    {
        if(s_arrAsciiBind[nIdx].m_szKeyString[0] == cIn)
        {
            pVKey->m_eKeyAct = s_arrAsciiBind[nIdx].m_eKeyAct;                
            pVKey->m_cKey     = ' ';
        }
    }

    return 0;
}

int get_vkey_from_esc_seq(int nSock, VKey_T* pVKey)
{
    unsigned char abEscSeqBuf[8];
    char      cIn; 
    int     nIdx = 0;
    
    abEscSeqBuf[nIdx++] = ASCII_ESC;
    abEscSeqBuf[nIdx]     = '\0';

    do
    {
        if(read_key(nSock, &cIn) < 0)
            return -1;
    }while(cIn == ASCII_ESC);

    if(cIn != '[')
    {
        pVKey->m_eKeyAct = ACTION_ADD_CHAR;
        pVKey->m_cKey     = cIn;
    
        return 0;
    }

    abEscSeqBuf[nIdx++] = cIn;
    abEscSeqBuf[nIdx]     = '\0';

    while(nIdx < 8)
    {
        int i;
        if(read_key(nSock, &cIn) < 0)
            return -1;
        
        abEscSeqBuf[nIdx++] = cIn;
        abEscSeqBuf[nIdx]     = '\0';
        
        for(i = 0; i < _SIZEOF(s_arrEscSeqBind, KeyBind_T); i++)
        {
            if(!strcmp((char *)abEscSeqBuf, (char *)s_arrEscSeqBind[i].m_szKeyString))
            {
                pVKey->m_eKeyAct = s_arrEscSeqBind[i].m_eKeyAct;
                pVKey->m_cKey      = ' ';
                return 0;
            }
        }
    }
    
    INIT_VKEY((*pVKey));
    
    return 0;
}

int read_vkey(int nSock, VKey_T* pVKey)
{
    char      cIn;

    if(read_key(nSock, &cIn) < 0)
        return -1;

    INIT_VKEY((*pVKey));

    if(isprint(cIn))
    {
        pVKey->m_eKeyAct = ACTION_ADD_CHAR;
        pVKey->m_cKey     = cIn;
    }
    else if(cIn != ASCII_ESC)
    {
        if(get_vkey_from_ascii(pVKey, cIn) < 0)
            return -1;
    }
    else
    {    
        if(get_vkey_from_esc_seq(nSock, pVKey) < 0)
            return -1;
    }

    return 0;
}

void cl_insert_chars(int nSock, ConsolLine_T* psLine, const char* pszIns, int nInsLen) 
{
    static char szBack[MAX_READ_LINE_LEN];

    int nIdx = 0;

    if(nInsLen == 0)
        return;

    if(psLine->m_nCnt + nInsLen > psLine->m_nMaxBufSize)
        return;
    
    if(psLine->m_nCaretPos == psLine->m_nCnt) /* End of Line */
    {
        for(nIdx = 0; nIdx < nInsLen; nIdx++)
        {
            psLine->m_pBuf[psLine->m_nCnt] = pszIns[nIdx];
            psLine->m_nCnt++;
            psLine->m_nCaretPos++;
        }
        write_console(nSock, pszIns, nInsLen);
    }
    else    /* Insert Middle of Line */
    {
        char* pCurPos      = psLine->m_pBuf + psLine->m_nCaretPos;
        char* pTargetPos = psLine->m_pBuf + psLine->m_nCaretPos + nInsLen;
        int   nMoveLen      = psLine->m_nCnt - psLine->m_nCaretPos; 

        memmove(pTargetPos, pCurPos, nMoveLen);
        memcpy(pCurPos, pszIns, nInsLen);

        write_console(nSock, pCurPos, nMoveLen + nInsLen);

        memset(szBack, '\b', nMoveLen);
        write_console(nSock, szBack, nMoveLen);
#if 0 
        for(nIdx = 0; nIdx< nMoveLen; nIdx++)
            write_console(nSock, &cBack, 1);
#endif
        psLine->m_nCaretPos += nInsLen;
        psLine->m_nCnt         += nInsLen;
    }

}


void cl_move_left(int nSock, ConsolLine_T* psLine)
{
    char cBack = '\b';

    if(psLine->m_nCaretPos <= 0)
        return;

    psLine->m_nCaretPos--;
    write_console(nSock, &cBack, 1);    
}

void cl_move_right(int nSock, ConsolLine_T* psLine)
{
    if(psLine->m_nCaretPos >= psLine->m_nCnt)
        return;

    write_console(nSock, &psLine->m_pBuf[psLine->m_nCaretPos], 1);    
    psLine->m_nCaretPos++;
}

#define ERASE_CHAR     "\b \b"
void cl_erase(int nSock, ConsolLine_T* psLine, int nErase)
{
    static char szErase[MAX_READ_LINE_LEN];

    if(nErase == 0)
        return;

    if(psLine->m_nCnt <= 0)
        return;

    if(psLine->m_nCaretPos < nErase)
        return;

    if(psLine->m_nCaretPos < psLine->m_nCnt)
    {
        char* pCurPos = psLine->m_pBuf + psLine->m_nCaretPos;
        char* pTarget = pCurPos - nErase;
        
        int nMoveLen = psLine->m_nCnt - psLine->m_nCaretPos;
        
        memmove(pTarget, pCurPos, nMoveLen);
        memset(pTarget + nMoveLen, ' ', nErase);

        memset(szErase, '\b', nErase);
        memcpy(szErase + nErase, pTarget, nMoveLen + nErase); 
        write_console(nSock, szErase, nMoveLen+ nErase+nErase);
        memset(szErase, '\b', nMoveLen + nErase);
        write_console(nSock, szErase, nMoveLen+ nErase);

        psLine->m_nCaretPos -= nErase;
        psLine->m_nCnt -= nErase;
    }
    else
    {
        int nIdx = 0;
        for(nIdx = 0; nIdx < nErase; nIdx++)
            write_console(nSock, ERASE_CHAR, 3);

        psLine->m_nCaretPos -= nErase;
        psLine->m_nCnt -= nErase;
    }
}

void cl_delete(int nSock, ConsolLine_T* psLine, int nDelete)
{
    char szDelete[MAX_READ_LINE_LEN];

    char* pCurPos = psLine->m_pBuf + psLine->m_nCaretPos;
    int nRemain = psLine->m_nCnt - psLine->m_nCaretPos;
    
    if(nDelete == 0)
        return;

    if(nRemain == 0)
        return;

    if(nRemain < nDelete)
        nDelete = nRemain;

    memmove(pCurPos, pCurPos + nDelete, nRemain - nDelete);
    memset(pCurPos + nRemain - nDelete, ' ',  nDelete);
    
    memcpy(szDelete, pCurPos, nRemain);
    write_console(nSock, szDelete, nRemain);
    memset(szDelete, '\b', nRemain);
    write_console(nSock, szDelete, nRemain);

    psLine->m_nCnt-= nDelete;
}

void cl_return(int nSock, ConsolLine_T* psLine)
{
    psLine->m_pBuf[psLine->m_nCnt] = '\0';
    
    write_console(nSock, CR, 2);
}

void cl_clear_line(int nSock, ConsolLine_T* psLine)
{
    memset(psLine->m_pBuf, '\b', psLine->m_nCaretPos);
    write_console(nSock, psLine->m_pBuf, psLine->m_nCaretPos);
    
    memset(psLine->m_pBuf, ' ', psLine->m_nCnt);
    write_console(nSock, psLine->m_pBuf, psLine->m_nCnt);
    memset(psLine->m_pBuf, '\b', psLine->m_nCnt);
    write_console(nSock, psLine->m_pBuf, psLine->m_nCnt);
    
    memset(psLine->m_pBuf, ' ', psLine->m_nCnt);
    
    psLine->m_nCnt = 0;
    psLine->m_nCaretPos = 0;
}

int read_line(int nSock, char* pszBuf, int nMaxBufSize)
{
    VKey_T            sVKey;
    ConsolLine_T    sConsolLine;
    int             nHistoryIdx = -1;

    pszBuf[0] = '\0';
    memset(&sConsolLine, 0x00, sizeof(ConsolLine_T));
    sConsolLine.m_pBuf = pszBuf;
    sConsolLine.m_nMaxBufSize = nMaxBufSize;
    
    do    
    {
        if(read_vkey(nSock, &sVKey) < 0)
            return -1;
    
        switch(sVKey.m_eKeyAct)
        {
            case ACTION_ADD_CHAR:
                cl_insert_chars(nSock, &sConsolLine, &sVKey.m_cKey, 1);
                break;
            case ACTION_MOVE_LEFT:
                cl_move_left(nSock, &sConsolLine);
                break;
            case ACTION_MOVE_RIGHT:
                cl_move_right(nSock, &sConsolLine);
                break;
            case ACTION_BACKSPACE:
                cl_erase(nSock, &sConsolLine, 1);
                break;
            case ACTION_DELETE:
                cl_delete(nSock, &sConsolLine, 1);
                break;                                        
            case ACTION_RETURN:
                cl_return(nSock, &sConsolLine);
                if(strlen(sConsolLine.m_pBuf))
                    insert_history(sConsolLine.m_pBuf);
                return sConsolLine.m_nCnt;
                break;                                
            case ACTION_CANCEL_TEXT:
                cl_insert_chars(nSock, &sConsolLine, "^C", 2);
                cl_return(nSock, &sConsolLine);
                return 0;
                break;                                
            case ACTION_EXIT_TERM:
                return -1;
                break;                                
            case ACTION_HISTORY_UP:
            {
                char* pszHistory = (char*)search_history(++nHistoryIdx);
                if(pszHistory)
                {
                    cl_clear_line(nSock, &sConsolLine);
                    cl_insert_chars(nSock, &sConsolLine, pszHistory, strlen(pszHistory));
                }
                else
                {
                    nHistoryIdx--;    
                }    
            }
                break;
            case ACTION_HISTORY_DOWN:    
            {
                char* pszHistory = (char*)search_history(--nHistoryIdx);
                if(pszHistory)
                {
                    cl_clear_line(nSock, &sConsolLine);
                    cl_insert_chars(nSock, &sConsolLine, pszHistory, strlen(pszHistory));
                }
                else
                {
                    nHistoryIdx++;
                }
            }
                break;
            default:
                break;    
        }

    }while(1);

    return 0;
}

