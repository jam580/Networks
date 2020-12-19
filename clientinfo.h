#ifndef CLIENT_INFO_INCLUDED
#define CLIENT_INFO_INCLUDED

#include <time.h>
#include <stdbool.h>

#define KEEPALIVE_TIMEOUT 10

typedef struct ClientInfo {
    int socket;
    time_t time_added;
    bool keep_alive;
    int timeout; // ignored if keep_alive is false
} *ClientInfo;

extern ClientInfo ClientInfo_new(int socket);
extern void ClientInfo_free(ClientInfo *ci);

#endif