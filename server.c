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
} *HTTPMessage;

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

int read_sckt(int sckt, HTTPMessage message)
{
    return true;
}

int main(int argc, const char *argv[]) 
{
    SocketConn parent, child; // parent and child socket connections
    fd_set master_fds, read_fds; // select fd sets
    int fdmax; // highest socket number
    int sckt; // socket loop counter
    ClientList clients; // list of client connections
    int read_result, process_result;
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
                NEW(req);
                read_result = read_sckt(sckt, req);
                FREE(req);
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
