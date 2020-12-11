#ifndef HTTPMESSAGE_INCLUDED
#define HTTPMESSAGE_INCLUDED

#include "headerfieldslist.h"
#include <stdbool.h>

typedef struct HTTPMessage{
    char *start_line;
    HeaderFieldsList header;
    char *body;
    size_t content_len;
    size_t body_remaining;
    bool has_full_header;
    bool is_complete;
    char *unprocessed;
    int bytes_unprocessed;
} *HTTPMessage;

#endif