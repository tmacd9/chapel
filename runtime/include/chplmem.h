#ifndef _chplmem_H_
#define _chplmem_H_

#include <stddef.h>
#include <stdint.h>
#include "arg.h"
#include "chpltypes.h"
#include "chplthreads.h"

typedef struct {
  char* head;
  char* current;
  char* tail;
} chpl_meminfo_t;


extern chpl_meminfo_t chpl_meminfo;
extern int heapInitialized;

extern chpl_mutex_t _memtrack_lock;
extern chpl_mutex_t _memstat_lock;
extern chpl_mutex_t _memtrace_lock;
extern chpl_mutex_t _malloc_lock;

void initMemTable(void);
void printMemTable(int64_t threshold, int32_t lineno, _string filename);
void resetMemStat(void);
void startTrackingMem(void);
void printMemStat(int32_t lineno, _string filename);
void printFinalMemStat(int32_t lineno, _string filename);
void initHeap(void* start, size_t size);

void setMemmax(int64_t value);
void setMemstat(void);
void setMemtrack(void);
void setMemthreshold(int64_t value);
void setMemtrace(char* memLogname);

#define CHPL_ALLOC_PERMIT_ZERO(s,d,l,f) ((s == 0) ? 0x0 : chpl_alloc(s,d,l,f))
#define chpl_alloc(size, description, lineno, filename) \
  chpl_malloc(1, size, description, lineno, filename)
void* chpl_malloc(size_t number, size_t size, const char* description,
                   int32_t lineno, _string filename);
void* chpl_calloc(size_t number, size_t size, const char* description,
                   int32_t lineno, _string filename);
void* chpl_realloc(void* ptr, size_t number, size_t size, 
                    const char* description, int32_t lineno, _string filename);
void  chpl_free(void* ptr, int32_t lineno, _string filename);

uint64_t mem_used(int32_t lineno, _string filename);

#endif
