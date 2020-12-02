#ifndef HEADER_FIELDS_INCLUDED
#define HEADER_FIELDS_INCLUDED

#include <stdlib.h>
#include "list.h"

#define HeaderFieldsList List_T

extern HeaderFieldsList HeaderFieldsList_new();
extern HeaderFieldsList HeaderFieldsList_push(HeaderFieldsList hfl, char *l);
extern HeaderFieldsList HeaderFieldsList_remove(HeaderFieldsList hfl, 
                                                char *field);
extern size_t HeaderFieldsList_length(HeaderFieldsList hfl);
extern char* HeaderFieldsList_get(HeaderFieldsList hfl, char *field);
extern HeaderFieldsList HeaderFieldsList_pop(HeaderFieldsList hfl, char **x);
extern void HeaderFieldsList_print(HeaderFieldsList hfl);
extern void HeaderFieldsList_free(HeaderFieldsList *hfl);

#endif