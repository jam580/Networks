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
#include "failure.h"
#include "mem.h"
#include "clientlist.h"
#include "headerfieldslist.h"
#include "socketconn.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BUFF_SIZE 2048

typedef struct HTTPMessage{
    char *start_line;
    HeaderFieldsList header;
    char *body;
    size_t content_len;
    bool has_full_header;
    bool is_complete;
    char *unprocessed;
    int bytes_unprocessed;
} *HTTPMessage;

HTTPMessage HTTPMessage_new()
{
    HTTPMessage msg;
    NEW(msg);
    msg->start_line = NULL;
    msg->header = HeaderFieldsList_new();
    msg->body = NULL;
    msg->content_len = 0;
    msg->has_full_header = false;
    msg->is_complete = false;
    msg->unprocessed = NULL;
    msg->bytes_unprocessed = 0;
    return msg;
}

void HTTPMessage_free(HTTPMessage *httpmsg)
{
    HTTPMessage msg = *httpmsg;
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

void close_client(ClientList *clients, fd_set *master_fds, int sckt)
{
    printf("Closing connection\n");
    FD_CLR(sckt, master_fds);
    *clients = ClientList_remove(*clients, sckt);
    close(sckt);
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
    if (*end_ptr == '\n')
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

    start_line = true;
    while ((line = get_line(buf, bytes)) != NULL && !(msg->has_full_header))
    {
        if (start_line)
        {
            msg->start_line = line;
            start_line = false;
        }
        else if (strcmp(line, "\r\n") == 0)
        {
            msg->has_full_header = true;
            FREE(line);
        }
        else
            msg->header = HeaderFieldsList_push(msg->header, line);
    }
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
    if ((bytes = read(sckt, buf, BUFF_SIZE)) <= 0)
        return false;
    
    // TODO: include unprocessed bytes in processing.
    buf_dup = buf;
    extract_header(&buf, &bytes, msg);

    if (msg->has_full_header)
    {
        content_len = get_content_len(msg->header);
        if (content_len > 0)
        {
            // TODO: process body
        }
        else
        {
            msg->is_complete = true;
            if (bytes > 0)
                return false;
        }
    }
    
    if (bytes > 0)
    {
        msg->unprocessed = buf;
        msg->bytes_unprocessed = bytes;
    }
    else
        FREE(buf_dup);

    return true;
}

int main(int argc, const char *argv[]) 
{
    SocketConn parent, child; // parent and child socket connections
    fd_set master_fds, read_fds; // select fd sets
    int fdmax; // highest socket number
    int sckt; // socket loop counter
    ClientList clients; // list of client connections
    HTTPMessage req, res;

    check_usage(argc, argv);
    parent = SocketConn_new();
    SocketConn_set_portno(parent, atoi(argv[1]));
    
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

    int count = 0; // TODO remove
    while(count < 2) // TODO change to while(true)
    {
        for (sckt = 0; sckt <= fdmax; sckt++) 
            if (!ClientList_keepalive(clients, sckt))
                close_client(&clients, &master_fds, sckt);

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
                printf("Accepting new connection\n"); // TODO remove
                if (SocketConn_accept(parent, child))
                {
                    FD_SET(child->fd, &master_fds);
                    fdmax = MAX(child->fd, fdmax);
                    clients = ClientList_push(clients, child->fd);
                }
                else
                {
                    fprintf(stderr, "ERROR on accept\n");
                }
            }
            else // Data arriving from already connected socket
            {
                printf("Processing request\n"); // TODO remove
                req = HTTPMessage_new();
                if (read_sckt(sckt, req) && req->is_complete)
                {
                    printf("\nRequest Header:\n"); // TODO remove
                    printf(req->start_line); // TODO remove
                    HeaderFieldsList_print(req->header); // TODO remove
                    printf("\n"); // TODO remove
                }
                else
                {
                    fprintf(stderr, "Bad request\n");
                    close_client(&clients, &master_fds, sckt);
                }
                HTTPMessage_free(&req);
            }
        }
        count++; // TODO remove
    }

    ClientList_free(&clients);
    SocketConn_close(parent);
    SocketConn_free(&parent);
    SocketConn_free(&child);
    return EXIT_SUCCESS;
}
