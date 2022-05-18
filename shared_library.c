#include "shared_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define BASENAME(str)       (strrchr(str, '/') ? strrchr(str, '/') + 1 : str)
#define MAX_BUF_SIZE   (2 * 1024)

#define MAX_LIBRARIES (32)
#define DELIM         " \n\t"
static SharedLibrary_t gLibraries[MAX_LIBRARIES];
static int             gLibrariesCnt = 0;

int load_shared_library_info(int pid)
{
    FILE* pMapFile = NULL;
    char szBuffer[MAX_BUF_SIZE];
    char szAddr[128];

    sprintf(szBuffer, "/proc/%d/maps", pid);

    pMapFile = fopen(szBuffer, "r");
    if(!pMapFile)
    {
        printf("cannot open %s\n", szBuffer);
        return -1;
    }

    gLibrariesCnt = 0;
    while(fgets(szBuffer, MAX_BUF_SIZE, pMapFile))
    {
        // 7f1232f7c000-7f1233137000 r-xp 00000000 08:01 14160069                   /lib/x86_64-linux-gnu/libc-2.19.so
        char* tok = strtok(szBuffer, DELIM);
        char* pSept = NULL;

        if(!tok) continue;
        strcpy(szAddr, tok);

        tok = strtok(NULL, DELIM);
        if(!tok) continue;
        if(strcmp(tok, "r-xp")) continue;
        
        // Skip
        tok = strtok(NULL, DELIM);
        if(!tok) continue;
        
        // Skip
        tok = strtok(NULL, DELIM);
        if(!tok) continue;

        // Skip
        tok = strtok(NULL, DELIM);
        if(!tok) continue;
    
        // Skip
        tok = strtok(NULL, DELIM);
        if(!tok) continue;
        if(tok[0] == '[') continue;
        
        strcpy(gLibraries[gLibrariesCnt].mBaseName, BASENAME(tok));

        pSept = strchr(szAddr, '-');
        if(!pSept) continue;
        *pSept++ = '\0';

#if (__PTR_SIZE__ == 64)
        gLibraries[gLibrariesCnt].mStartAddr = strtoull(szAddr, NULL, 16);
        gLibraries[gLibrariesCnt].mEndAddr = strtoll(pSept, NULL, 16);
#else
        gLibraries[gLibrariesCnt].mStartAddr = (unsigned int)strtoul(szAddr, NULL, 16);
        gLibraries[gLibrariesCnt].mEndAddr = (unsigned int)strtoul(pSept, NULL, 16);
#endif
        gLibrariesCnt++;
    }
    
    fclose(pMapFile);
    return 0;
}

SharedLibrary_t* get_shared_library_from_offset(unsigned int offset)
{
    int ii = 0;
    for(ii = 0; ii < gLibrariesCnt; ii++)
    {
        if(gLibraries[ii].mStartAddr <= offset && gLibraries[ii].mEndAddr > offset)
        {
            return &gLibraries[ii];
        }
    }

    return NULL;
}
