#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mem.h"
#include "headerfieldslist.h"

typedef struct HeaderField {
    char *name;
    char *option;
} *HeaderField;

HeaderFieldsList HeaderFieldsList_new()
{
    return NULL;
}

HeaderFieldsList HeaderFieldsList_push(HeaderFieldsList hfl, char *l)
{
    char *dup = strdup(l);
    return List_append(hfl, List_push(NULL, dup));
}

HeaderFieldsList HeaderFieldsList_remove(HeaderFieldsList hfl, 
                                                char *field)
{
    HeaderFieldsList head, prev;
    char *line;
    size_t len = strlen(field);
    head = hfl;
    prev = NULL;
    while (head != NULL)
    {
        line = head->first;
        if (strncasecmp(line, field, len) == 0)
        {
            if (prev == NULL)
                hfl = head->rest;
            else
                prev->rest = head->rest;
            head->rest = NULL;
            HeaderFieldsList_free(&head);
            break;
        }
        prev = head;
        head = head->rest;
    }
    return hfl;
}

size_t HeaderFieldsList_length(HeaderFieldsList hfl)
{
    return List_length(hfl);
}

char* HeaderFieldsList_get(HeaderFieldsList hfl, char *field)
{
    HeaderFieldsList head;
    char *line;
    size_t len = strlen(field);
    head = hfl;
    while (head != NULL)
    {
        line = head->first;
        if (strncmp(line, field, len) == 0)
        {
            return line;
        }
        head = head->rest;
    }
    return NULL;
}

HeaderFieldsList HeaderFieldsList_pop(HeaderFieldsList hfl, char **x)
{
    if (!x)
        FREE(hfl->first);
    return List_pop(hfl, (void**)x);
}

void HeaderFieldsList_print(HeaderFieldsList hfl)
{
    HeaderFieldsList head;
    char *line;
    head = hfl;
    while (head != NULL)
    {
        line = head->first;
        printf("%s", line);
        head = head->rest;
    }
}

void HeaderFieldsList_free(HeaderFieldsList *hfl)
{
    HeaderFieldsList head;
    char *line;
    head = *hfl;
    while (head != NULL)
    {
        line = head->first;
        FREE(line);
        head = head->rest;
    }
    List_free(hfl);
}
