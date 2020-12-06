#include <time.h>
#include <string.h>
#include "cache.h"
#include "list.h"
#include "table.h"
#include "atom.h"
#include "mem.h"

struct Cache_T {
    List_T keys;
    size_t len;
    Table_T cache;
};

typedef struct Cache_Item {
    void *item;
    time_t intime;
    int secs_to_live;
} *Cache_Item;

Cache_T Cache_new(int hint)
{
    Cache_T cache;
    NEW(cache);
    cache->keys = NULL;
    cache->len = 0;
    cache->cache = Table_new(hint, NULL, NULL);
    return cache;
}

void Cache_put(Cache_T cache, const char *key, void *value, int max_secs)
{
    Cache_Item item, existing;

    NEW(item);
    item->item = value;
    item->intime = time(NULL);
    if (max_secs > 0)
        item->secs_to_live = max_secs;
    else
        item->secs_to_live = SECS_TO_LIVE;
    existing = Table_put(cache->cache, Atom_string(key), item);
    if (!existing)
        cache->keys = List_push(cache->keys, strdup(key));
    cache->len += 1;
}

void *Cache_get(Cache_T cache, const char *key, int *age)
{
    Cache_Item item = Table_get(cache->cache, Atom_string(key));
    
    if (item)
    {
        *age = time(NULL) - item->intime;
        if (*age > item->secs_to_live)
        {
            Cache_remove(cache, key);
            return NULL;
        }
        return item->item;
    }
    else
        return NULL;
}

void *Cache_remove(Cache_T cache, const char *key)
{
    Cache_Item item;
    List_T head, prev;
    char *search;

    item = Table_remove(cache->cache, Atom_string(key));
    if (item)
    {
        cache->len -= 1;
        head = cache->keys;
        prev = NULL;
        while (head)
        {
            search = head->first;
            if (strcmp(key, search) == 0)
            {
                if (!prev)
                    cache->keys = head->rest;
                else
                    prev->rest = head->rest;
                head->rest = NULL;
                FREE(search);
                List_free(&head);
                break;
            }
            prev = head;
            head = head->rest;
        }
    }
    return item;
}

size_t Cache_length(Cache_T cache)
{
    return cache->len;
}

void Cache_free(Cache_T *cache)
{
    List_T head;
    head = (*cache)->keys;
    while (head)
    {
        FREE(head->first);
        head = head->rest;
    }
    List_free(&((*cache)->keys));
    Table_free(&((*cache)->cache));
    FREE(*cache);
}
