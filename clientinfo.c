#include <stdlib.h>
#include <assert.h>
#include "mem.h"
#include "clientinfo.h"

ClientInfo ClientInfo_new(int socket)
{
    ClientInfo ci;
    NEW(ci);
    ci->socket = socket;
    ci->time_added = time(NULL);
    ci->keep_alive = true;
    return ci;
}

void ClientInfo_free(ClientInfo *ci)
{
    FREE(*ci);
}

