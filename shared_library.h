#ifndef __SHARED_LIBRARY_H_
#define __SHARED_LIBRARY_H_

//#define __PTR_SIZE__     (64)
#define __PTR_SIZE__     (32)

#define MAX_BASE_NAME  (1024)

typedef struct SharedLibrary_s
{
    char      mBaseName[MAX_BASE_NAME];

#if (__PTR_SIZE__ == 64)
    long long  mStartAddr;
    long long  mEndAddr;
#else
    unsigned int  mStartAddr;
    unsigned int  mEndAddr;
#endif

}SharedLibrary_t;

int load_shared_library_info(int pid);
SharedLibrary_t* get_shared_library_from_offset(unsigned int offset);

#endif // __SHARED_LIBRARY_H_
