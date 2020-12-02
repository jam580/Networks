#include <assert.h>
#include "clientlist.h"
#include "clientinfo.h"

ClientList ClientList_new()
{
    return  NULL;
}

ClientList ClientList_push(ClientList cl, int socket)
{
    ClientInfo ci = ClientInfo_new(socket);
    return List_append(cl, List_push(NULL, ci));
}

ClientList ClientList_remove(ClientList cl, int socket)
{
    ClientList head, new_clients, prev;
    ClientInfo ci;
    
    head = cl;
    new_clients = cl;
    prev = NULL;

    while (head != NULL)
    {
        ci = head->first;
        if (ci->socket == socket)
        {
            if (prev == NULL) // removing head
                new_clients = head->rest;
            else // removing middle or last
                prev->rest = head->rest;
            head->rest = NULL;
            ClientList_free(&head);
            break;
        }
        prev = head;
        head = head->rest;
    }

    return new_clients;
}

size_t ClientList_length(ClientList cl)
{
    return List_length(cl);
}

void ClientList_free(ClientList *cl)
{
    ClientList head;
    ClientInfo ci;
    head = *cl;
    while (head != NULL)
    {
        ci = head->first;
        ClientInfo_free(&ci);
        head = head->rest;
    }
    List_free(cl);
}
