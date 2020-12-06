/*******************************************************************************
*
* Tufts University
* COMP112 (Networks) - Final Project
* By: Ramon Fernandes & James Mattei
* Last Edited: December 5, 2020
*                               
* server.c - main processor for an HTTP proxy server application
* Command Arguments:
*      port: port to listen on for incoming connections
*
*******************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include "failure.h"
#include "mem.h"
#include "clientlist.h"
#include "headerfieldslist.h"
#include "socketconn.h"
#include "table.h"
#include "atom.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BUFF_SIZE 2048

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

typedef struct Directive{
    char *key;
    char *value;
} Directive;

HTTPMessage HTTPMessage_new()
{
    HTTPMessage msg;
    NEW(msg);
    msg->start_line = NULL;
    msg->header = HeaderFieldsList_new();
    msg->body = NULL;
    msg->content_len = 0;
    msg->body_remaining = 0;
    msg->has_full_header = false;
    msg->is_complete = false;
    msg->unprocessed = NULL;
    msg->bytes_unprocessed = 0;
    return msg;
}

void HTTPMessage_free(HTTPMessage *httpmsg)
{
    HTTPMessage msg = *httpmsg;
    if (msg->header != NULL)
        HeaderFieldsList_free(&(msg->header));
    if (msg->start_line != NULL)
        FREE(msg->start_line);
    if (msg->body != NULL)
        FREE(msg->body);
    if (msg->unprocessed != NULL)
        FREE(msg->unprocessed);
    FREE(*httpmsg);
}

void check_usage(int argc, const char* argv[])
{
    if (argc != 2) 
    {
        // need to add 2 for extra space around %s and for the null terminator.
        int buffer_size = strlen("Usage: %s <port>\n") + strlen(argv[0]) + 2;
        char msg[buffer_size];
        sprintf(msg, "Usage: %s <port>\n", argv[0]);
        exit_failure(msg);
    }
}

void close_client(ClientList *clients, fd_set *master_fds, int sckt, 
    Table_T sckt_to_msg)
{
    HTTPMessage msg;
    printf("Closing connection\n");
    FD_CLR(sckt, master_fds);
    *clients = ClientList_remove(*clients, sckt);
    msg = Table_get(sckt_to_msg, Atom_int(sckt));
    HTTPMessage_free(&msg);
    Table_remove(sckt_to_msg, Atom_int(sckt));
    close(sckt);
}

/*
 * Function: write_msg
 * -------------------
 *   Write the given msg to the given sckt. It is a checked runtime error to
 *   provide an msg that is not complete or a NULL msg. It is an unched runtime
 *   error to provide an sckt with that is not properly connected.
 */
bool write_msg(HTTPMessage msg, int sckt)
{
    int bytes;
    char *field;
    HeaderFieldsList header;

    assert(msg && msg->is_complete);
    bytes = write(sckt, msg->start_line, strlen(msg->start_line));
    if (bytes <= 0) 
        return false;
    
    header = msg->header;
    for (; header != NULL; header = header->rest)
    {
        field = header->first;
        bytes = write(sckt, field, strlen(field));
        if (bytes <= 0)
            return false;
    }

    bytes = write(sckt, "\r\n", 2); // new line end of header
    if (bytes <= 0)
        return false;

    if (msg->content_len > 0)
    {
        bytes = write(sckt, msg->body, msg->content_len);
        if (bytes <= 0)
            return false;
    }
    
    return true;
}

/*
 * Function: get_line
 * ------------------
 *   Returns the first line in buf as a nul-terminated malloc'ed c-string or 
 *   NULL if not found. Moves buf head to one after new line. Updates bytes to
 *   new length in buf.
 * 
 *   buf: an array of characters
 *   bytes: pointer to the length of buf
 */
char *get_line(char **buf, int *bytes)
{
    char *end_ptr, *line;
    int i;
    size_t line_len;

    if (*bytes == 0)
        return NULL;

    end_ptr = *buf;
    for (i = 0; i < *bytes; end_ptr++, i++)
        if (*end_ptr == '\n')
            break;

    line = NULL;
    if (i != *bytes && *end_ptr == '\n')
    {
        line_len = end_ptr - *buf + 1;
        line = malloc(line_len + 1);
        memcpy(line, *buf, line_len);
        line[line_len] = '\0';
        *buf = *buf + line_len;
        *bytes = *bytes - line_len;
    }
    return line;
}

/*
 * Function: extract_header
 * ------------------------
 *   Extract the header from buf and store in msg. Moves buf head to one after
 *   the last complete header item processed. Updates bytes to
 *   new length in buf.
 */
void extract_header(char **buf, int *bytes, HTTPMessage msg)
{
    char *line;
    bool start_line;

    start_line = msg->start_line == NULL;
    while (!(msg->has_full_header) && (line = get_line(buf, bytes)) != NULL)
    {
        if (start_line)
        {
            msg->start_line = line;
            start_line = false;
        }
        else if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0)
        {
            msg->has_full_header = true;
            FREE(line);
        }
        else
            msg->header = HeaderFieldsList_push(msg->header, line);
    }
}

/*
 * Function: extract_body
 * ------------------------
 *   Extract up to bytes given of the content/body from buf and store in msg.
 *   returns number of bytes remaining in given body.
 */
int extract_body(char *body, int bytes, HTTPMessage msg)
{
    size_t body_read, bytes_copied;

    body_read = msg->content_len - msg->body_remaining;
    bytes_copied = MIN((size_t)bytes, msg->body_remaining);
    memcpy(msg->body + body_read, body, bytes_copied);
    bytes = bytes - bytes_copied;
    msg->body_remaining = msg->body_remaining - bytes_copied;

    return bytes;
}

/*
 * Function: get_content_len
 * -------------------------
 *   Returns the content length, as specified in the header.
 */
size_t get_content_len(HeaderFieldsList header)
{
    char *field = HeaderFieldsList_get(header, "Content-Length");
    if (field != NULL)
        return atoll(field + 15);
    else
        return 0;
}

/*
 * Function: read_sckt
 * -------------------
 *   Reads a request or response from the client at given socket and stores
 *   the message in the given HTTPMessage.
 *   
 *   Returns false if the read failed and true otherwise;
 */
bool read_sckt(int sckt, HTTPMessage msg)
{
    int bytes;
    char *buf, *buf_dup;
    size_t content_len;

    buf = malloc(BUFF_SIZE);
    if (msg->bytes_unprocessed > 0)
        memcpy(buf, msg->unprocessed, msg->bytes_unprocessed);

    bytes = read(sckt, 
                 buf + msg->bytes_unprocessed, 
                 BUFF_SIZE - msg->bytes_unprocessed);
    if (bytes <= 0)
    {
        printf("bad read\n"); // TODO remove
        FREE(buf);
        return false;
    }
    else
    {
        bytes += msg->bytes_unprocessed;
        msg->bytes_unprocessed = 0;
        FREE(msg->unprocessed);
    }
    
    buf_dup = buf;
    extract_header(&buf, &bytes, msg);

    if (msg->has_full_header)
    {
        content_len = get_content_len(msg->header);
        msg->content_len = content_len;
        if (content_len > 0)
        {
            if (msg->body == NULL)
            {
                msg->body_remaining = content_len;
                msg->body = calloc(content_len, sizeof(*(msg->body)));
            }
            bytes = extract_body(buf, bytes, msg);
            if (msg->body_remaining == 0)
            {
                msg->is_complete = true;
                if (bytes > 0)
                {
                    printf("content read but still has more bytes"); // TODO remove
                    return false;
                }
            }
        }
        else
        {
            msg->is_complete = true;
            if (bytes > 0)
            {
                printf("no content but still has more bytes"); // TODO remove
                return false;
            }
        }
    }
    
    if (bytes > 0)
    {
        msg->unprocessed = malloc(bytes);
        memcpy(msg->unprocessed, buf, bytes);
        msg->bytes_unprocessed = bytes;
    }
    
    FREE(buf_dup);
    return true;
}

/*
 * Function: explode_start_line
 * ----------------------------
 *   Split the start line into the method, the request-targer, and the protocol.
 *   More infor in RFC 7230, pg. 21. Max length of each pointer given by len.
 * 
 *   Returns false on failure and true otherwise.
 */
bool explode_start_line(char *line, char *m, char *u, char *p)
{
    return sscanf(line, "%s %s %s", m, u, p);
}

/*
 * Function: explode_url
 * ---------------------
 *   Split the url into host, path, and port if they exist. An empty value
 *   (e.g. strlen(port) == 0) indicates that it was not found. Each section
 *   will not be nul-terminated.
 *   References: https://cboard.cprogramming.com/c-programming/112381-using-sscanf-parse-string.html
 */
void explode_url(char *url, char *host, char *port, char *path)
{
    char *host_start, *port_start, *path_start;

    host[0] = '\0';
    port[0] = '\0';
    path[0] = '\0';

    // Finding the starting pointers of each section
    host_start = strstr(url, "://");
    if (host_start == NULL)
        return;
    host_start += 3;

    port_start = strstr(host_start, ":");
    if (port_start)
    {
        port_start++;
        path_start = strstr(port_start, "/");
    }
    else
        path_start = strstr(host_start, "/");

    // Copy relevant sections to locations
    if (port_start)
    {
        memcpy(host, host_start, port_start - host_start - 1);
        if (path_start)
        {
            memcpy(port, port_start, path_start - port_start);
            strcpy(path, path_start);
        }
        else
            strcpy(port, port_start);
    }
    else if (path_start)
    {
        memcpy(host, host_start, path_start - host_start);
        strcpy(path, path_start);
    }
    else
    {
        strcpy(host, host_start);
    }
}

/*
 * Return whether http req method is supported by this proxy
 */
bool method_is_supported(char *meth)
{
    return strcmp(meth, "GET") == 0 ||
           strcmp(meth, "CONNECT");
}

/*
 * Returns file descriptor to connection to host at port or -1 if failed.
 */
int build_server_conn(char *host, char *port)
{
    SocketConn server;
    struct hostent *serverip;
    unsigned int s_addr;
    int fd;

    serverip = gethostbyname(host);
    if (serverip == NULL)
    {
        printf("No host\n");
        return -1;
    }

    s_addr = 0;
    memcpy(&s_addr, serverip->h_addr, serverip->h_length);

    server = SocketConn_new();
    if (!SocketConn_open(server, atoi(port), s_addr))
    {
        printf("Failed to open\n");
        SocketConn_free(&server);
        return -1;
    }
    if (!SocketConn_connect(server))
    {
        printf("Failed to connect\n");
        SocketConn_free(&server);
        return -1;
    }

    fd = server->fd;
    SocketConn_free(&server);

    return fd;
}

/*
 * Function: get_res
 * -----------------
 *   Forwards the req to the given socket and stores response in res
 * 
 *   Returns false on failure and true otherwise
 */
bool get_res(HTTPMessage req, HTTPMessage res, int server)
{
    if(!write_msg(req, server))
        return false;

    while (!res->is_complete && read_sckt(server, res)) {}

    if (!res->is_complete)
        return false;

    return true;
}

/*
 * Function: get_directives
 * ------------------------
 * Extracts the directives from the header field and returns them as an array
 * of directives.
 * 
 * field: header field
 * start: start position of directives
 * max: maximum number of directives to extract
 * dirs: array to store Directives in
 */
int get_directives(char *field, int start, int max, Directive dirs[])
{
    int stop, i;
    char tmp;
    bool more_directives;

    more_directives = true;
    for (i = 0; i < max && more_directives; i++)
    {
        while (field[start] == ' ') { start++; }
        stop = start;
        while (field[stop] != '=' && field[stop] != ',' &&
               field[stop] != '\r' && field[stop] != '\n') 
        { 
            stop++; 
        }

        tmp = field[stop];
        field[stop] = '\0';
        dirs[i].key = strdup(field+start);
        field[stop] = tmp;

        if (field[stop] == '=')
        {
            start = ++stop;
            while (field[stop] != ',' && field[stop] != '\r' && field[stop] != '\n')
                stop++;

            tmp = field[stop];
            field[stop] = '\0';
            dirs[i].value = strdup(field+start);
            field[stop] = tmp;
        }
        
        if (field[stop] != ',')
            more_directives = false;
        start = stop + 1;
    }
    return i;
}

void process_keep_alive(ClientList *clients, int client, char *field)
{
    Directive dirs[10];
    int num_dirs, i, timeout;

    if (field == NULL)
        return;
    
    for (i = 0; i < 10; i++)
    {
        dirs[i].key = NULL;
        dirs[i].value = NULL;
    }

    num_dirs = get_directives(field, strlen("keep-alive:"), 10, dirs);
    for (i = 0; i < num_dirs; i++)
    {
        if (strcasecmp("timeout", dirs[i].key) == 0)
        {
            timeout = atoi(dirs[i].value);
            ClientList_set_keepalive(*clients, client, true, timeout);
            break;
        }
    }

    for (i = 0; i < 10; i++)
    {
        FREE(dirs[i].key);
        FREE(dirs[i].value);
    }
}

/*
 * Function: process_connection_header
 * -----------------------------------
 * Ensures req message has the proper connection header to forward to
 * server and updated client information with appropriate persistency.
 * 
 * req: the request message
 * protocol: the HTTP protocol used by the client
 * client: the client socket
 * clients: pointer to client list
 * 
 * Notes:
 *  - RFC 7230: Proxies are not allowed to persist connections with 1.0 clients
 *  - RFC 7230: 1.1 clients use persistent connections by default.
 */
void process_connection_header(HTTPMessage req, char *protocol, int client, 
    ClientList *clients)
{
    char *field;
    Directive dirs[10];
    int num_dirs, i;

    for (i = 0; i < 10; i++)
    {
        dirs[i].key = NULL;
        dirs[i].value = NULL;
    }

    if (strcmp(protocol, "HTTP/1.0") == 0)
        ClientList_set_keepalive(*clients, client, false, -1);
    else
    {
        field = HeaderFieldsList_get(req->header, "Connection");
        if (field != NULL)
        {
            num_dirs = get_directives(field, strlen("connection:"), 10, dirs);
            for (i = 0; i < num_dirs; i++)
            {
                if (strcasecmp("close", dirs[i].key) == 0)
                {
                    ClientList_set_keepalive(*clients, client, false, -1);
                    break;
                }
                if (strcasecmp("keep-alive", dirs[i].key) == 0)
                {
                    process_keep_alive(clients, client, 
                        HeaderFieldsList_get(req->header, dirs[i].key));
                }

                req->header = HeaderFieldsList_remove(req->header, dirs[i].key);
            }
            req->header = HeaderFieldsList_remove(req->header, "Connection");
        }
    }

    // Push our desired connection with remote server. 
    field = strdup("Connection: close\r\n");
    req->header = HeaderFieldsList_push(req->header, field);

    for (i = 0; i < 10; i++)
    {
        FREE(dirs[i].key);
        FREE(dirs[i].value);
    }
}

/*
 * Function: process_req
 * ---------------------
 *   Forwards the req to the appropriate server and stores response in res.
 * 
 *   Returns file descriptor for server if successful and -1 otherwise.
 */
int process_req(HTTPMessage req, HTTPMessage res, int client, 
    ClientList *clients)
{
    int ret_val;
    char *method, *url, *protocol;
    char *host, *port, *path;
    size_t line_len, url_len;
    int server_fd;

    line_len = strlen(req->start_line);
    method = calloc(line_len, sizeof(*method));
    url = calloc(line_len, sizeof(*url));
    protocol = calloc(line_len, sizeof(*protocol));

    ret_val = -1;
    if (explode_start_line(req->start_line, method, url, protocol) && 
        method_is_supported(method))
    {
        url_len = strlen(url);
        host = calloc(url_len, sizeof(*host));
        port = calloc(url_len, sizeof(*port));
        path = calloc(url_len, sizeof(*path));
        explode_url(url, host, port, path);

        if (url[0] != '\0')
        {
            if (port[0] == '\0')
                strcpy(port, "80");

            server_fd = build_server_conn(host, port);
            ret_val = server_fd;
            if (server_fd >= 0 && strcmp(method, "GET") == 0)
            {
                process_connection_header(req, protocol, client, clients);
                if (!get_res(req, res, server_fd))
                    ret_val = -1;
                else
                {
                    res->header = 
                        HeaderFieldsList_remove(res->header, "Connection");
                    if (ClientList_keepalive(*clients, client))
                        res->header = HeaderFieldsList_push(res->header, 
                            strdup("Connection: keep-alive\r\n"));
                    else
                        res->header = HeaderFieldsList_push(res->header, 
                            strdup("Connection: close\r\n"));
                    
                    if (!write_msg(res, client))
                        ret_val = -1;
                }
            }
            else if (server_fd >= 0 && strcmp(method, "CONNECT") == 0)
            {
                // TODO https
            }
            else
                ret_val = -1; 
        }
        FREE(host);
        FREE(port);
        FREE(path);
    }
    
    FREE(method);
    FREE(url);
    FREE(protocol);
    return ret_val;
}

int main(int argc, const char *argv[]) 
{
    SocketConn parent, child; // parent and child socket connections
    fd_set master_fds, read_fds; // select fd sets
    int fdmax; // highest socket number
    int sckt; // socket loop counter
    ClientList clients; // list of client connections
    HTTPMessage req, res;
    Table_T sckt_to_msg;
    int expected_clients; // expected number of concurrent clients
    int server_fd;

    check_usage(argc, argv);
    parent = SocketConn_new();
    SocketConn_set_portno(parent, atoi(argv[1]));
    expected_clients = 100;
    
    // Set up parent socket
    if (!SocketConn_open(parent, -1, INADDR_ANY))
        exit_failure("ERROR opening socket\n");
    if (!SocketConn_bind(parent))
        exit_failure("ERROR on binding\n");
    if (!SocketConn_listen(parent, 5))
        exit_failure("ERROR on listen\n");

    // Clear memory in fd sets and get ready for select
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(parent->fd, &master_fds);
    fdmax = parent->fd;

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE    
    clients = ClientList_new(); // initialize clients list
    child = SocketConn_new(); // initialize empty child connection
    sckt_to_msg = Table_new(expected_clients, NULL, NULL);

    while(true)
    {
        for (sckt = 0; sckt <= fdmax; sckt++) 
            if (!ClientList_keepalive(clients, sckt))
                close_client(&clients, &master_fds, sckt, sckt_to_msg);

        read_fds = master_fds;

        printf("Waiting for socket to be ready\n"); // TODO remove
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
            exit_failure("ERROR on select\n");

        for (sckt = 0; sckt <= fdmax; sckt++)
        {
            if (!FD_ISSET(sckt, &read_fds))
                continue;

            if (sckt == parent->fd) // Incoming connection request
            {
                if (SocketConn_accept(parent, child))
                {
                    FD_SET(child->fd, &master_fds);
                    fdmax = MAX(child->fd, fdmax);
                    clients = ClientList_push(clients, child->fd);
                    Table_put(sckt_to_msg, Atom_int(child->fd), 
                              HTTPMessage_new());
                    printf("Accepted Connection %d\n", child->fd); // TODO remove
                }
                else
                {
                    fprintf(stderr, "ERROR on accept\n");
                }
            }
            else // Data arriving from already connected socket
            {
                printf("Processing request on %d\n", sckt); // TODO remove
                req = Table_get(sckt_to_msg, Atom_int(sckt));
                if (!read_sckt(sckt, req))
                {
                    fprintf(stderr, "Bad request\n");
                    close_client(&clients, &master_fds, sckt, sckt_to_msg);
                    continue;
                }
                else if (req->is_complete)
                {
                    res = HTTPMessage_new();
                    server_fd = process_req(req, res, sckt, &clients);
                    if(server_fd <= 0)
                    {
                        fprintf(stderr, "Failed to process request\n");
                        close_client(&clients, &master_fds, sckt, sckt_to_msg);
                        continue;
                    }
                    else
                    {
                        fprintf(stderr, "Request processed\n");
                        Table_put(sckt_to_msg, Atom_int(sckt), 
                                  HTTPMessage_new());
                        HTTPMessage_free(&req);
                        close(server_fd);
                    }
                    HTTPMessage_free(&res);
                }
                if (!ClientList_keepalive(clients, sckt))
                    close_client(&clients, &master_fds, sckt, sckt_to_msg);
            }
        }
    }

    for (sckt = 0; sckt <= fdmax; sckt++)
    {
        req = Table_get(sckt_to_msg, Atom_int(sckt));
        if (req)
            HTTPMessage_free(&req);
    }

    Table_free(&sckt_to_msg);
    ClientList_free(&clients);
    SocketConn_close(parent);
    SocketConn_free(&parent);
    SocketConn_free(&child);
    return EXIT_SUCCESS;
}
