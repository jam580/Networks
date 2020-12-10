#ifndef CACHE_INCLUDED
#define CACHE_INCLUDED

#include <stdlib.h>
#include "httpmessage.h"

#define SECS_TO_LIVE 60*60
typedef struct Cache_T *Cache_T;

extern Cache_T Cache_new(int hint);
extern void Cache_put(Cache_T cache, const char *key, void *value, int max_secs);
extern void *Cache_get(Cache_T cache, const char *key, int *age);
extern void *Cache_remove(Cache_T cache, const char *key);
extern size_t Cache_length(Cache_T cache);
extern void Cache_free(Cache_T *cache);
extern void Cache_write_out(Cache_T cache);
unsigned long hash(unsigned char const * input);

#endif