#include "socketconn.h"
#include "mem.h"

SocketConn SocketConn_new()
{
    SocketConn sc;
    NEW(sc);
    sc->fd = -1;
    sc->len = 0;
    sc->portno = -1;
    return sc;
}

bool SocketConn_set_portno(SocketConn sc, int portno)
{
    sc->portno = portno;
    return true;
}

bool SocketConn_open(SocketConn sc, int portno, unsigned int s_addr)
{
    int optval;

    if (portno < 0)
    {
        assert(sc->portno > -1);
        portno = sc->portno;
    }
    sc->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sc->fd >= 0)
    {
        optval = 1;
        setsockopt(sc->fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, 
                   sizeof(int));
        
        sc->addr.sin_family = AF_INET;
        sc->addr.sin_port = htons(portno);
        sc->addr.sin_addr.s_addr = s_addr;
        sc->len = sizeof(sc->addr);
    }
    return sc->fd >= 0;
}

bool SocketConn_bind(SocketConn sc) 
{
    return bind(sc->fd, (struct sockaddr *) &(sc->addr), sc->len) >= 0;
}

bool SocketConn_listen(SocketConn sc, int n) 
{
    return listen(sc->fd, n) >= 0;
}

bool SocketConn_connect(SocketConn sc)
{
    return connect(sc->fd, (struct sockaddr *)&(sc->addr), sc->len) >= 0;
}

bool SocketConn_accept(SocketConn server, SocketConn client)
{
    client->len = sizeof(client->addr);
    client->fd = accept(server->fd, (struct sockaddr *) &(client->addr), 
                        &(client->len));
    client->portno = ntohs(client->addr.sin_port);
    return client->fd >= 0;
}

void SocketConn_close(SocketConn sc)
{
    close(sc->fd);
}

void SocketConn_free(SocketConn *sc)
{
    FREE(*sc);
}