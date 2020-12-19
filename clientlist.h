#ifndef CLIENT_LIST_INCLUDED
#define CLIENT_LIST_INCLUDED

#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

#define ClientList List_T

extern ClientList ClientList_new();
extern ClientList ClientList_push(ClientList cl, int socket);
extern ClientList ClientList_remove(ClientList cl, int socket);
extern size_t ClientList_length(ClientList cl);
extern void ClientList_set_keepalive(ClientList cl, int socket, bool ka, int timeout);
extern bool ClientList_keepalive(ClientList cl, int socket);
extern int ClientList_get(ClientList cl);
extern void ClientList_free(ClientList *cl);

#endif