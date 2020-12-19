#ifndef SOCKETCONN_INCLUDED
#define SOCKETCONN_INCLUDED

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>

typedef struct SocketConn
{
    int fd; // socket
    struct sockaddr_in addr; // socket connection info
    socklen_t len; // sddr length
    int portno; // port number to connect to. -1 if not applicable.
} *SocketConn;

/*
 * Function: SocketConn_new
 * ------------------------
 *  Returns a heap-allocated socket connect with blank details.
 */
extern SocketConn SocketConn_new();

extern bool SocketConn_set_portno(SocketConn sc, int portno);

/*
 * Function: SocketConn_open
 * -------------------------
 *   Initializes fd in sckt using the AF_INET address domain, a SOCK_STREAM 
 *   type, and a 0 protocol (dynamically decide based on type). Sets fd to
 *   < 0 if it could not be initialized. Sets opt vals to allow for the reuse
 *   of local addresses. Sets the connection details to given portno and IP
 *
 *   sc: contains fd to initialize
 *   portno: the port number to connect to. Must be >-1 OR sc->portno > -1
 *   sin_addr: IP address of the host
 * 
 *   Returns false if failed and true otherwise
 */
extern bool SocketConn_open(SocketConn sc, int portno, unsigned int s_addr);

/*
 * Function: SocketConn_bind
 * -------------------------
 *   Binds the socket to the stored addr. It is an unchecked runtime error to 
 *   provide a SocketConn that was not opened with SocketConn_open.
 *
 *   sc: contains the socket details
 * 
 *   Returns false if failed and true otherwise
 */
extern bool SocketConn_bind(SocketConn sc);

/*
 * Function: SocketConn_listen
 * ---------------------------
 *   Listens for connections on the socket. Will queue up to n connection
 *   requests. It is an unchecked runtime error to call SocketConn_listen before
 *   a successful SocketConn_bind. 
 *
 *   sc: contains the socket details
 *   n: maximum number of connection requests to queue
 * 
 *   Returns false if failed and true otherwise
 */
extern bool SocketConn_listen(SocketConn sc, int n);

/*
 * Function: SocketConn_connect
 * ----------------------------
 *   Connects server.
 *
 *   s: contains the server socket details
 * 
 *   Returns false if failed and true otherwise
 */
extern bool SocketConn_connect(SocketConn sc);

/*
 * Function: SocketConn_accept
 * ---------------------------
 *   Accepts an incoming connection to server. Stores client connection in 
 *   client. It is an unchecked runtime error to call SocketConn_accept before
 *   a successful SocketConn_listen on server.
 *
 *   server: contains the server socket details
 *   client: SocketConn to store the client socket details.
 * 
 *   Returns false if failed and true otherwise
 */
extern bool SocketConn_accept(SocketConn server, SocketConn client);

/*
 * Function: SocketConn_close
 * --------------------------
 *   Closes the given socket connection.
 */
extern void SocketConn_close(SocketConn sc);

/*
 * Function: SocketConn_free
 * -------------------------
 *   Deallocate memory associated with SocketConn
 */
extern void SocketConn_free(SocketConn *sc);

#endif