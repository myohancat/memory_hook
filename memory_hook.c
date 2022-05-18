#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // _GNU_SOURCE

#include "shared_library.h"

#include <dlfcn.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#define IS_POWER_OF_TWO(x)     ((x != 0) && !(x & (x - 1)))
#define PADDING(size, align)  (~(size - 1) & (align - 1))

/************************************************************
 * HOOK Code 
 ************************************************************/
#define SUPPORT_CLI

#ifdef SUPPORT_CLI
#include "sc_cli.h"

extern void memory_dump_summary(void (*cb)(void*, int, int, void*), void* param);

static void print_memory_status(void* param, int refCnt, int total, void* retAddr)
{
    sc_cli_out(param, "refCnt : %d, total : %d, retAddr : %p\n", refCnt, total, retAddr);
}

CLI(dump_memory, "dump memory")
{
    sc_cli_out(session, "==== DUMP MEMORY ===================\n");
    memory_dump_summary(print_memory_status, session);
    sc_cli_out(session, "\n");

    return 0;
}

CLI(conv_retaddr, "convert return address to library offset")
{
    static int _init = 0;

    SharedLibrary_t* pLib;
    unsigned int retaddr;

    if(argc != 2)
    {
        sc_cli_out(session, "invalid argument inptued !\n");
        return 0;
    }
 
    retaddr = strtoul(argv[1], NULL, 16);

    if(_init == 0)
    {
        load_shared_library_info(getpid());
        _init = 1;
    }
    pLib = get_shared_library_from_offset(retaddr);
    if(!pLib)
    {
        sc_cli_out(session, ">> convert failed !\n");
        return 0;
    }

    sc_cli_out(session, "library : %s\n", pLib->mBaseName);
    sc_cli_out(session, "range   : %08x-%08x\n", pLib->mStartAddr, pLib->mEndAddr);
    sc_cli_out(session, "offset  : %x\n", retaddr - pLib->mStartAddr);

    return 0;
}
#endif // SUPPORT_CLI

static pthread_mutex_t _hookMutex = PTHREAD_MUTEX_INITIALIZER;

static void* (*orign_malloc)(size_t size);
static void  (*orign_free)(void *ptr);
static void* (*orign_calloc)(size_t nmemb, size_t size);
static void* (*orign_realloc)(void *ptr, size_t size);
static void* (*orign_memalign)(size_t alignment, size_t size);
static int   (*orign_posix_memalign)(void **memptr, size_t alignment, size_t size);
static void* (*orign_valloc)(size_t size);


static int is_initializing = 0;

static void hook_init()
{
    if(orign_malloc)
        return;

    pthread_mutex_lock(&_hookMutex);

    if(orign_malloc)
        goto EXIT;

    is_initializing = 1;

    orign_malloc         = dlsym(RTLD_NEXT, "malloc");
    orign_free           = dlsym(RTLD_NEXT, "free");
    orign_calloc         = dlsym(RTLD_NEXT, "calloc");
    orign_realloc        = dlsym(RTLD_NEXT, "realloc");
    orign_memalign       = dlsym(RTLD_NEXT, "memalign");
    orign_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
    orign_valloc         = dlsym(RTLD_NEXT, "valloc");

    if(!orign_malloc 
       || !orign_free
       || !orign_calloc
       || !orign_realloc
       || !orign_memalign
       || !orign_posix_memalign
       || !orign_valloc
       )
    {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        exit(1);
    }

#ifdef SUPPORT_CLI
    INSTALL_CLI(dump_memory);
    INSTALL_CLI(conv_retaddr);
    sc_cli_init();
#endif // SUPPORT_CLI

    is_initializing = 0;

EXIT:
    pthread_mutex_unlock(&_hookMutex);
}

/************************************************************
 * Memory Info
 ************************************************************/
#define HEAP_MAGIC        0x00A110C0
#define MAX_HASH_SIZE     1024
#define POLYNOMIAL        0x04c11db7L   

typedef struct HeapEntryHdr_s
{
    unsigned int     mMagic; 

    struct HeapEntryHdr_s* mPrev;
    struct HeapEntryHdr_s* mNext;

    size_t        mSize;
    int           mPadding;
    void*         mRetAddr;

} HeapEntryHdr_t;

typedef struct HashEntry_S
{
    struct HashEntry_S* mNext;
    
    unsigned int   mRefCnt;
    unsigned int   mTotalMem;
    void*          mRetAddr;

}HashEntry_T;

static HeapEntryHdr_t* _pMemHead = NULL;
static HeapEntryHdr_t* _pMemEnd  = NULL;

static HashEntry_T* _hashTable[MAX_HASH_SIZE];
#define CALC_HASH(retAddr)  (((unsigned int)retAddr >> 4) % MAX_HASH_SIZE);

static pthread_mutex_t _sMutex = PTHREAD_MUTEX_INITIALIZER;

static void _add_heap_entry(HeapEntryHdr_t* entry)
{
    if(entry == NULL)
        goto EXIT;

    if(_pMemEnd == NULL)
    {
        _pMemHead = entry;
        _pMemEnd  = entry;
        entry->mPrev = NULL;
        entry->mNext = NULL;
        goto EXIT;
    }
    
    entry->mPrev    = _pMemEnd;
    _pMemEnd->mNext = entry;
    _pMemEnd        = entry;
    entry->mNext    = NULL;

EXIT:
    return;
}

static void _delete_heap_entry(HeapEntryHdr_t* entry)
{
    if(entry == NULL)
        goto EXIT;

    if( entry->mPrev != NULL)
        entry->mPrev->mNext = entry->mNext;

    if( entry->mNext != NULL)
        entry->mNext->mPrev = entry->mPrev;

    if(_pMemHead == entry)
        _pMemHead = entry->mNext;
    
    if(_pMemEnd == entry)
        _pMemEnd = entry->mPrev;
    
EXIT:
    return;
}

static int _add_heap_entry_to_summary_table(HeapEntryHdr_t* entry)
{
    unsigned int hash  = CALC_HASH(entry->mRetAddr);
    HashEntry_T* iter  = _hashTable[hash];

    while(iter)
    {
        if(entry->mRetAddr == iter->mRetAddr)
            break;

        iter = iter->mNext;
    }

    if(iter == NULL)
    {
        iter = (HashEntry_T*)orign_malloc(sizeof(HashEntry_T));
        memset(iter, 0x00, sizeof(HashEntry_T));
        iter->mRetAddr = entry->mRetAddr;

        iter->mNext = _hashTable[hash];
        _hashTable[hash] = iter;
    }
    
    iter->mRefCnt ++;
    iter->mTotalMem += entry->mSize;

    return 0;
}


static int _delete_heap_entry_from_summary_table(HeapEntryHdr_t* entry)
{
    unsigned int hash = CALC_HASH(entry->mRetAddr);
    HashEntry_T* iter = _hashTable[hash];
    HashEntry_T* prev = NULL;

    while(iter)
    {
        if(entry->mRetAddr == iter->mRetAddr)
            break;

        prev = iter;
        iter = iter->mNext;
    }

    if(!iter)
        return -1;

    iter->mRefCnt --;
    iter->mTotalMem -= entry->mSize;
    
    if(iter->mRefCnt != 0)
        return 0;

    if(!prev)
        _hashTable[hash] = iter->mNext;
    else
        prev->mNext = iter->mNext;

    orign_free(iter);

    return 0;
}

void dbg_heap_free(void* ptr, void* retAddr)
{
    HeapEntryHdr_t* entry  = NULL;

    hook_init();

    if(ptr == NULL)
        return;

    entry = (HeapEntryHdr_t*)((char*)ptr - sizeof(HeapEntryHdr_t));

    if(entry->mMagic != HEAP_MAGIC)
    {
           printf("%s() Invalid DbgHeader from caller:%p\n", __FUNCTION__, retAddr);
        orign_free(ptr);
        return;
    }

    pthread_mutex_lock(&_sMutex);
    _delete_heap_entry(entry);
    _delete_heap_entry_from_summary_table(entry);
    pthread_mutex_unlock(&_sMutex);
    
    memset(entry, 0x00, sizeof(HeapEntryHdr_t)); // reset header
    orign_free(entry);
}

void* dbg_heap_alloc(void* ptr, size_t size, int align, void* retAddr)
{
    HeapEntryHdr_t* oldEntry = NULL;
    HeapEntryHdr_t* newEntry = NULL;
    void*           newPtr   = NULL;
    int             padding  = 0;

    hook_init();

    /* check validate */
    if(ptr != NULL)
    {
        oldEntry = (HeapEntryHdr_t*)((char*)ptr - sizeof(HeapEntryHdr_t));
        if(oldEntry->mMagic != HEAP_MAGIC)
        {
               printf("%s() Invalid DbgHeader from caller:%p\n", __FUNCTION__, retAddr);
            return NULL;
        }
    }

    if(size == 0)
        goto EXIT;

    if(align > 0)
        padding = PADDING(size, align);

    /* reallocation memory */
    newEntry = (HeapEntryHdr_t*)orign_malloc(size + padding + sizeof(HeapEntryHdr_t));
    if(!newEntry)
        goto EXIT;

    memset(newEntry, 0x00, sizeof(HeapEntryHdr_t));
    newEntry->mMagic    = HEAP_MAGIC;
    newEntry->mSize     = size;
    newEntry->mPadding  = padding;
    newEntry->mRetAddr  = retAddr;
        
    newPtr = (void*)(newEntry + 1);
    if(ptr != NULL)
    {
        if(oldEntry->mSize > size)
              memcpy(newPtr, ptr, size);
           else
               memcpy(newPtr, ptr, oldEntry->mSize);
    }
  
    pthread_mutex_lock(&_sMutex);
    _add_heap_entry(newEntry);
    _add_heap_entry_to_summary_table(newEntry);
    pthread_mutex_unlock(&_sMutex);

EXIT:
    if(ptr != NULL)
        dbg_heap_free(ptr, retAddr);
   
    return newPtr; 
}

#define HEAP_ALLOC(ptr, size, align) dbg_heap_alloc(ptr, size, align, __builtin_return_address(0))
#define HEAP_FREE(ptr)                 dbg_heap_free(ptr, __builtin_return_address(0))

void* malloc(size_t size)
{
    if(is_initializing)
    {
        extern void*__libc_malloc(size_t);
        return __libc_malloc(size);
    }

    return HEAP_ALLOC(NULL, size, 0);
}

void free(void* ptr)
{
    if(is_initializing)
    {
        extern void __libc_free(void *);
        __libc_free(ptr);
        return;
    }

    HEAP_FREE(ptr);
}

void* realloc(void* ptr, size_t size)
{
    if(is_initializing)
    {
        extern void *__libc_realloc(void *, size_t);
        return __libc_realloc(ptr, size);
    }

    return HEAP_ALLOC(ptr, size, 0); 
}

void* calloc(size_t nmemb, size_t sizem)
{
    void* ptr  = NULL;
    int   size = nmemb*sizem;

    if(is_initializing)
    {
        extern void *__libc_calloc(size_t, size_t);
        return __libc_calloc(nmemb, sizem);
    }

    if((ptr = HEAP_ALLOC(NULL, size, 0)))
        memset(ptr, 0x00, size);

    return ptr;
}

void* memalign(size_t alignment, size_t size)
{
    if(is_initializing)
    {
        extern void*__libc_memalign(size_t, size_t);
        return __libc_memalign(alignment, size);
    }

    return HEAP_ALLOC(NULL, size, alignment);
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    if(is_initializing)
    {
        extern int __libc_posix_memalign(void **, size_t, size_t);
        return __libc_posix_memalign(memptr, alignment, size);
    }

    if(!IS_POWER_OF_TWO(alignment ))
        return EINVAL;
    
    if(!(*memptr = HEAP_ALLOC(NULL, size, alignment)))
        return ENOMEM;

    return 0;
}

void* valloc(size_t size)
{
    if(is_initializing)
    {
        extern void* __libc_valloc(size_t);
        return __libc_valloc(size);
    }

    printf("NOT SUPPORTED YET - original valloc() will be called\n");
    return orign_valloc(size);
}

#if 0 /* strdup is not call, maybe define __GI___strdup to strdup */
char* strdup(const char* str)
#else
char* __GI___strdup (const char* str)
#endif
{
    char* newStr = NULL;
    int size = strlen(str);

    if(is_initializing)
    {
        extern void*__libc_malloc(size_t);
        newStr = __libc_malloc(size + 1);
        memcpy(newStr, str, size);
        newStr[size] = '\0';
        return newStr;
    }

    if(size <= 0)
        return NULL;

    newStr = HEAP_ALLOC(NULL, size + 1, 0);
    if(newStr)
    {
        memcpy(newStr, str, size);
        newStr[size] = '\0';
    }

    return newStr;
}

void memory_dump(void (*cb)(void*, int, void*), void* param)
{
    HeapEntryHdr_t* entry = _pMemHead;

    while(entry)
    {
        cb(param, entry->mSize, entry->mRetAddr);        
        entry = entry->mNext;
    }
}

void memory_dump_summary(void (*cb)(void*, int, int, void*), void* param)
{
    int ii = 0;

    for(ii = 0; ii < MAX_HASH_SIZE; ii++)
    {
        HashEntry_T* entry = _hashTable[ii];
        while(entry)
        {
            cb(param, entry->mRefCnt, entry->mTotalMem, entry->mRetAddr);        
            entry = entry->mNext;
        }
    }
}

