#include <time.h>
#include <string.h>
#include "cache.h"
#include "list.h"
#include "table.h"
#include "atom.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


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
    char filehold [50];
    char conthold [50];
    if (item)
    {
        *age = time(NULL) - item->intime;
        if (*age > item->secs_to_live)
        {
            unsigned char* id = malloc(strlen(key)*sizeof(unsigned char));
            strncpy((char*) id, key, strlen(key));
            unsigned long fn = hash(id);
            sprintf(filehold, "files/%lu.txt", fn);
            sprintf(conthold, "files/%lu.gz", fn);

            char*filename = &filehold[0];
            char*bodname= &conthold[0];
            //if the file exists, remove it
            if(access(filename, 0)==0)
            {
                remove(filename);
            }
            if(access(bodname,0)==0)
            {
                remove(bodname);
            }
                
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

// http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash(unsigned char const * input)
{
    unsigned long hash = 5382;
    int c;

    while( (c=*input++) )
        hash = ((hash<<5)+hash)+c;

    return hash;
}

//iterate through cache and write to files
void Cache_write_out(Cache_T cache)
{
    HTTPMessage res;
    char *key;
    int age;
    List_T head;
    int i = 0;
    char filehold [50];
    char conthold [50];

    head = cache->keys;
    while(head!=NULL)
    {
        key = head->first;
        List_T temp = head->rest;
        if ((res = Cache_get(cache, key, &age))!=NULL)
        {
            //hash url for file id
            unsigned char* id = malloc(strlen(key)*sizeof(unsigned char));
            printf("url is: %s\n", key);
            strncpy((char*) id, key, strlen(key));
            unsigned long fn = hash(id);
            printf("hash id: %lu", fn);
            sprintf(filehold, "files/%lu.txt", fn);
            sprintf(conthold, "files/%lu.gz", fn);

            char*filename = &filehold[0];
            char*bodname = &conthold[0];
            if(access(filename, 0)!= 0)
            {
                FILE * fp;
                //put url in file
                printf("%s\n", key);
                
                //print headers to file
                char* encoding = "Content-Encoding";
                printf("About to check for encoding \n");
                if(HeaderFieldsList_get(res->header, encoding)!=NULL)
                {
                    printf("Endoing present\n");
                    fp = fopen(filename, "w+");
                    if(fp==NULL)
                        printf("Failed to open file");
                    fprintf(fp, "%s\n", key);
                    HeaderFieldsList_file(res->header, fp);
                    //print content to file
                    fprintf(fp, "sep\n");

                    fclose(fp);
                    //open new gzip file to write contents
                    fp = fopen(bodname, "wb+");
                    fwrite(res->body,1,res->content_len,fp);
                    fclose(fp);
                }
                else //if there is no encoding, payload is plaintext
                {
                    printf("No encoding\n");
                    fp = fopen(filename, "w+");
                     if(fp==NULL)
                        printf("Failed to open file");
                    fprintf(fp, "%s\n", key);
                    HeaderFieldsList_file(res->header, fp);
                    fprintf(fp, "sep\n");
                    fwrite(res->body,1,res->content_len,fp);
                    fclose(fp);
                }
                
                    
            }

            printf("LETS GET THAT INFO\n");
            
        }
        head = temp;
        i++;
    }
    

}