#ifndef CLIENT_INFO_INCLUDED
#define CLIENT_INFO_INCLUDED

#include <time.h>
#include <stdbool.h>

typedef struct ClientInfo {
    int socket;
    time_t time_added;
    bool keep_alive;
} *ClientInfo;

extern ClientInfo ClientInfo_new(int socket);
extern void ClientInfo_free(ClientInfo *ci);

#endif