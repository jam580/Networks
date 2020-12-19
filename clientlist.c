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

/*
 * Set Keep-Alive information for given socket. It is a checked runtime error
 * to provide a socket not present in cl.
 * 
 * Note: the max parameter has been deprecated and is thus ignored ()
 * 
 * Args:
 * ----
 * cl:      client list that contains socket
 * socket:  the socket of the client
 * ka:      boolean indicating whether connection should be kept alive
 * timeout: minimum amount of time to wait before timing out an idle client.
 *          -1 for default. Ignored if ka is false.
 */
void ClientList_set_keepalive(ClientList cl, int socket, bool ka, int timeout)
{
    assert(timeout > 0 || timeout == -1);
    ClientList head;
    ClientInfo ci;
    head = cl;
    while (head != NULL)
    {
        ci = head->first;
        if (ci->socket == socket)
        {
            ci->keep_alive = ka;
            if (ka)
            {
                if (timeout != -1)
                    ci->timeout = timeout;
                else
                    ci->timeout = KEEPALIVE_TIMEOUT;
            }
            return;
        }
        head = head->rest;
    }

    assert(false);
}

/*
 * Returns whether given socket should be closed. 
 */
bool ClientList_keepalive(ClientList cl, int socket)
{
    ClientList head;
    ClientInfo ci;
    time_t now;

    head = cl;
    now = time(NULL);
    while (head != NULL)
    {
        ci = head->first;
        if (ci->socket == socket)
        {
            if (!ci->keep_alive || ci->time_added + ci->timeout < now)
                return false;
            else
                return true;
        }
        head = head->rest;
    }
    return true;
}

// Get the socket of the client at the head of cl.
int ClientList_get(ClientList cl)
{
    ClientInfo ci;
    assert(cl);
    ci = cl->first;
    return ci->socket;
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
