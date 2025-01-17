/*******************************************************************************
*
* Tufts University
* COMP112 (Networks) - Final Project
* By: Ramon Fernandes & James Mattei
* Last Edited: December 2, 2020
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
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include "failure.h"
#include "mem.h"
#include "clientlist.h"
#include "headerfieldslist.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BUFF_SIZE 10000

struct blockstruct
{
    char* Object;
    int valid;
    char* firstline;
    time_t intime;
    double maxage;
    char* key;
};
typedef struct blockstruct Block;
Block* initBlock()
{
    Block* b1 = malloc(sizeof(Block));
    b1->Object = NULL;
    b1->key = calloc(100, sizeof(char));
    b1->valid = 0;
    b1->firstline = calloc(1, sizeof(char));
    return b1;
};
struct cachestruct
{
    Block** Entries;
    int sz;
};
typedef struct cachestruct Cache;

Cache* initCache(int x)
{

    Cache* c1 = malloc(sizeof(Cache));
    //allocate enough block pointers for appropriate size
    c1->Entries = malloc(sizeof(Block*)*x);
    c1->sz=x;
    //pre-allocate memory for OBJECTS
    for(int i = 0; i<x; i++)
    {
        c1->Entries[i] = initBlock();
    }
    return c1;
}

//Function to go through the entries in the cache and check if they are
//still valid
void update(Cache*c1)
{
    for(int i=0;i<c1->sz;i++)
    {
        if(c1->Entries[i]->valid==1)
        {
            //if the difference in time between when entry was stored and now is too big
            if( ( (long) time(NULL) - (long) c1->Entries[i]->intime )  > (long) c1->Entries[i]->maxage)
            {
                c1->Entries[i]->valid=0;
            }
        }
    }
}

//error wraper taken from server example
void error(char *msg) {
  perror(msg);
  exit(1);
}
//Trim function to clean up header
static void trim(char* segment)
{
    int i = strlen(segment);
    while(i > 0 && (segment[i-1] =='\n' ||segment[i-1] =='\r'))
        segment[--i] = '\0';
}
//find url in cache entry and return index of said entry
//return -1 if not found
int find(char* url, Cache* cache)
{
    for(int i=0; i<cache->sz;i++)
    {
        if( strcmp( cache->Entries[i]->key, url)==0 &&cache->Entries[i]->valid==1)
            return i;
    }
    return -1;
}

/*
 * Processes the given Keep-Alive header and stores relevant information in
 * client info. A NULL keep_alive does not alter the existing keep_alive config.
 */
void process_keep_alive(ClientList cl, int clientsock, char *keep_alive)
{
    int start, stop, timeout;
    bool more_directives;
    char tmp;
    start = 12;
    more_directives = true;

    if (keep_alive == NULL)
        return;

    while (more_directives)
    {
        while (keep_alive[start] == ' ')
            start++;
        stop = start;
        while (keep_alive[stop] != ',' && keep_alive[stop] != '\r' && 
               keep_alive[stop] != '\n')
            stop++;
        if (keep_alive[stop] != ',')
            more_directives = false;

        if (strncasecmp("timeout", keep_alive+start, stop-start) == 0)
        {
            tmp = keep_alive[stop];
            keep_alive[stop] = '\0';
            timeout = atoi(keep_alive + start + 8);
            ClientList_set_keepalive(cl, clientsock, true, timeout);
            keep_alive[stop] = tmp;
            break;
        }
        start = stop+1;
    }
}

bool process_header_buf(char *buf, int *bytes, HeaderFieldsList *hf)
{
    int i;
    char *new_buf, *line;
    bool end_of_header;

    i = 0;
    new_buf = buf;
    end_of_header = false;
    while (i < *bytes && !end_of_header)
    {
        if (*new_buf != '\n') // keep searching until end of line
        {
            // do nothing
        }
        else if (strncmp(buf, "\r\n", 2) == 0) // last line
        {
            end_of_header = true;
            buf = new_buf + 1;
        }
        else
        {
            line = calloc(new_buf - buf + 2, sizeof(*line));
            memcpy(line, buf, new_buf - buf + 1);
            *hf = HeaderFieldsList_push(*hf, line);
            FREE(line);
            buf = new_buf + 1;
        }
        
        i++;
        new_buf++;
    }

    if (i == *bytes)
    {
        *bytes = 0;
        buf = NULL;
    }
    else
    {
        *bytes = new_buf - buf;
        buf = buf + i;
    }

    return end_of_header;
}

HeaderFieldsList read_header(int sckt, char *buf, int *bytes)
{
    HeaderFieldsList header_fields;
    int i, new_bytes;
    char *new_buf;
    bool first_line_read, end_of_header;

    header_fields = HeaderFieldsList_new();
    i = 0;
    new_buf = buf;
    first_line_read = false;
    end_of_header = false;

    // skip over first line
    while (i < *bytes && !first_line_read)
    {
        if (*new_buf == '\n')
            first_line_read = true;
        i++;
        new_buf++;
    }

    new_buf = calloc(BUFF_SIZE, sizeof(*new_buf));
    *bytes = *bytes - i; 
    buf = buf + i;
    if (i != *bytes)
        end_of_header = process_header_buf(buf, bytes, &header_fields);

    while (!end_of_header)
    {
        if (buf != NULL)
            memcpy(new_buf, buf, *bytes);
        new_bytes = read(sckt, new_buf + *bytes, BUFF_SIZE);
        end_of_header = process_header_buf(new_buf, &new_bytes, &header_fields);
    }

    buf = new_buf;
    *bytes = new_bytes;

    return header_fields;
}

static void relay_ssl(char* method, char* host, char* protocol, FILE* sockrfp, FILE* sockwfp, FILE * clientsock, Cache* cache, char*url)
{
    int client_read, server_read, client_write, server_write;
    struct timeval timeout;
    fd_set master;
    int maxp, r;
    char buff[10000];
    timeout.tv_sec = 1;
    timeout.tv_usec =0;

    // clear unusued parameter warnings
    (void)method;
    (void)host;
    (void)protocol;
    (void)cache;
    (void)url;

    //return proxy header to client
    fputs("HTTP/1.0 200 Connection established\r\n\r\n", clientsock);
    fflush (clientsock);

    //set up other file handles
    server_read = fileno(sockrfp);
    server_write = fileno(sockwfp);
    client_read = fileno(stdin);
    client_write = fileno(clientsock);

    if(client_read >= server_read)
        maxp = client_read+1;
    else
    {
        maxp = server_read +1;
    }

    while(1)
    {
        //printf("read %d, write %d \n", doneread, donewrite);
        FD_ZERO( &master);
        FD_SET(client_read, &master);
        FD_SET(server_read, &master);

        //printf("About to select\n");
        r = select (maxp, &master, (fd_set*) 0, (fd_set*) 0, &timeout);
        if(r==0)
        {
            break; // send timeout so client stops communicating
        } 
        else if (FD_ISSET(client_read, &master))
        {
            printf("Client\n");
            r = read(client_read, buff, sizeof(buff));
            if (r<=0)
                break;
            r= write(server_write, buff, r);
            if (r<=0)
                break;
        }
        else if (FD_ISSET(server_read, &master))
        {
            r = read(server_read, buff, sizeof(buff));
            printf("Read %d bytes\n", r);
            if(r<=0)
                break;
            r = write(client_write, buff, r);
            if(r<=0)
                break;
        }
    }
}

static void relay_http(char* method, char* path, char* protocol, int serverfd, int clientfd, Cache* cache, char* url, ClientList cl, char *buf, int bytes)
{
    char line[10000], prot[10000], coms[10000];
    int first_line, stat, new_bytes;
    char hold;
    long con_length =-1;
    long i;
    int found = -1; //bool to dictate cache hit
    char* endptr, *obj, *obj_tmp;
    int age;
    HeaderFieldsList header_fields;
    bool keepalive = true;

    //look for cache hit
    update(cache);
    found = find(url,cache);
    
    //if there is a cache hit
    if(found!=-1)
    {
        printf("Cache hit\n");
        obj = cache->Entries[found]->Object;
        while(strncmp(obj, "\r\n", 2) != 0)
        {
            if (strncasecmp(obj, "Content-Length:", 15) == 0)
                con_length=atol( &(obj[15]));
            obj_tmp = strchr(obj, '\n') + 1;
            new_bytes = write(clientfd, obj, obj_tmp - obj + 1); // write line
        }
        long currentage = (long) time(NULL) - (long) cache->Entries[found]->intime;
        new_bytes = write(clientfd, "Age: %ld \r\n\r\n", currentage);
        obj += 2; // blank new line

        if(con_length != -1)
            new_bytes = write(clientfd, obj, con_length);

        //now that we've sent the item, move it to front of cache
        Block* temp = cache->Entries[found];
        for(int i=found; i>0;i--)
            cache->Entries[i]=cache->Entries[i-1];
        cache->Entries[0]=temp;
        temp=NULL;

        //flush the remainder of the client request
        header_fields = read_header(clientfd, buf, &bytes);
        char *field = HeaderFieldsList_get(header_fields, "Content-Length");
        
        if (field != NULL)
        {
            con_length = atol(field + 15);
            new_bytes = 1;
            for(i = 0; i < con_length && bytes == 1; i++)
                new_bytes = read(clientfd, &hold, 1);
        }
    }//end of cache hit
    else
    {
        printf("Not in cache\n");
        header_fields = HeaderFieldsList_new();
        //First send http request, add back the \r\n
        new_bytes = write(serverfd, buf, strchr(buf, '\n') - buf + 1);
        header_fields = read_header(clientfd, buf, &bytes);
        printf("Finished reading header\n");
        // Check for Connection
        char *field = HeaderFieldsList_get(header_fields, "Connection");
        if (field != NULL)
        {
            printf("Connection header found -- %s", field);
            int start = 12, stop;
            char tmp;
            bool more_directives = true;
        
            if (strncasecmp(protocol, "HTTP/1.0", 8) == 0)
            {
                // Proxies are not allowed to persist connections with 1.0 clients (RFC 7230 pg. 53)
                ClientList_set_keepalive(cl, clientfd, false, -1);
                keepalive = false;
            }
            else 
            {
                while (more_directives)
                {
                    printf("getting directives -- %s", field + start);
                    while (field[start] == ' ')
                        start++;
                    stop = start;
                    while (field[stop] != ',' && field[stop] != '\r' &&
                        field[stop] != '\n')
                        stop++;
                    if (field[stop] != ',')
                        more_directives = false;
                    
                    if (strncasecmp("close", field+start, stop-start) == 0)
                    {
                        ClientList_set_keepalive(cl, clientfd, false, -1);
                        keepalive = false;
                        break;
                    }
                    if (strncasecmp("Keep-Alive", 
                                    field + start, stop-start) == 0) 
                    {
                        process_keep_alive(cl, clientfd, 
                            HeaderFieldsList_get(header_fields, "Keep-Alive"));
                        printf("is keep-alive\n");
                    }

                    tmp = field[stop];
                    field[stop] = '\0';
                    header_fields = HeaderFieldsList_remove(header_fields, 
                                                                field + start);
                    field[stop] = tmp;
                    start = stop+1;
                }
            }
            memcpy(field + 12, "close\r\n", 8);
        }
        else
        {
            if (strncasecmp(protocol, "HTTP/1.0", 8) == 0)
            {
                // Proxies are not allowed to persist connections with 1.0 clients (RFC 7230 pg. 53)
                ClientList_set_keepalive(cl, clientfd, false, -1);
                keepalive = false;
            }
            HeaderFieldsList_push(header_fields, "Connection: close\r\n");
        }

        header_fields = HeaderFieldsList_pop(header_fields, &field);
        new_bytes = 1;
        while(field != NULL && new_bytes > 0)
        {
            new_bytes = write(serverfd, field, strlen(field));
            if( strncasecmp(field, "Content-Length:", 15) ==0)
                con_length=atol( &(field[15]));
            FREE(field);
            header_fields = HeaderFieldsList_pop(header_fields, &field);
        }
        new_bytes = write(serverfd, "\r\n", 2);
        
        //check for content and flush every char
        if(con_length !=-1)
        {
            for(i=0; i<con_length; i++ )
            {
                if (bytes - i > 0)
                    new_bytes = write(serverfd, buf + i, 1);
                else
                {
                    read(clientfd, buf, 1);
                    write(serverfd, buf, 1);
                }
            }
        }

        //recieve response form server and forward to client
        char* buff = calloc(1024*1024, sizeof(char));
        con_length = -1;
        first_line = 1;
        stat = -1;
        int cacheage = 0;
        char* firstlen;

        bytes = read(serverfd, buff, 1024*1024);
        header_fields = read_header(serverfd, buff, &bytes);
        field = HeaderFieldsList_get(header_fields, "Connection");
        if (field != NULL)
        {
            if (keepalive)
                memcpy(field + 12, "keep-alive\r\n", 13);
            else
                memcpy(field + 12, "close\r\n", 8);
        }
        else
        {
            if (keepalive)
                header_fields = HeaderFieldsList_push(
                                    header_fields, 
                                    "Connection: keep-alive\r\n");
            else
                header_fields = HeaderFieldsList_push(
                                    header_fields, 
                                    "Connection: close\r\n");
        }

        field = HeaderFieldsList_get(header_fields, "Content-Length");
        if (field != NULL)
            con_length = atol(field + 15);
        
        field = HeaderFieldsList_get(header_fields, "Cache-Control:");
        if (field != NULL)
        {
            int start = 12, stop;
            bool more_directives = true;
            while (more_directives)
            {
                while(field[start] == ' ')
                    start++;
                stop = start;
                while (field[stop] != ',' && field[stop] != '\r' && field[stop] != '\n')
                    stop++;
                if (field[stop] != ',')
                    more_directives = false;

                if (strncasecmp("max-age", field+start, stop-start) == 0)
                {
                    age = atoi(line + start + 8);
                    cacheage = 1;
                }
                start = stop+1;
            }
        }

        header_fields = HeaderFieldsList_pop(header_fields, &field);
        new_bytes = 1;
        while(field != NULL && new_bytes > 0)
        {
            new_bytes = write(clientfd, field, strlen(field));
            FREE(field);
            header_fields = HeaderFieldsList_pop(header_fields, &field);
        }
        new_bytes = write(clientfd, "\r\n", 2);

        if(!cacheage)
            age = 3600;

        //check for content
        if(con_length != -1)
        {
            printf("We have %ld content\n", con_length);
            for(i=0; i<con_length; i++)
            {
                new_bytes = read(serverfd, &hold, 1);
                //memcpy(cache->Entries[found]->Object + header_len + i, &hold, 1);
                write(clientfd, &hold, 1);
            }
        }
        FREE(buff);


        /*
        strcat(buff,end);
        size_t header_len = strlen(buff);
        //strcat(buff,"Connection: close\r\n");

        //Cache value - found should be -1
        for(int i =0; i< cache->sz;i++)
        {
            if(cache->Entries[i]->valid==0)
            {
                found = i;
                break;
            }
        }
        if(found ==-1)
            found = cache->sz-1;

        //found is the right index 
        cache->Entries[found]->valid=1;
        FREE(cache->Entries[found]->Object);
        cache->Entries[found]->Object = malloc(header_len + con_length);
        memcpy(cache->Entries[found]->Object, buff, header_len);

        //update key to url
        strcpy(cache->Entries[found]->key, url);
        cache->Entries[found]->maxage=age;
        cache->Entries[found]->intime=time(NULL);
        FREE(cache->Entries[found]->firstline);
        cache->Entries[found]->firstline=strdup(firstlen);
        //value is now cached
        FREE(firstlen);
        FREE(buff);*/
        HeaderFieldsList_free(&header_fields);
    }//end of cache miss   
}

void close_client(ClientList *clients, fd_set *master_fds, int socket)
{
    printf("Closing connection\n");
    close(socket);
    FD_CLR(socket, master_fds);
    *clients = ClientList_remove(*clients, socket);
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

int strcpy_until(char *src, char *dest, int start, int max, char delim)
{
    int j = 0;
    while (src[start] != delim && start < max)
    {
        memcpy(dest + j, src + start, 1);
        start++;
        j++;
    }
    dest[j] = '\0';

    return j;
}

/*
 * Split the start line in buf into the method, the request-target, and the 
 * protocol version. More info in RFC 7230, pg. 21. Reads for max bytes given.
 * 
 * Returns true if successfully parsed or false otherwise.
 * 
 * // TODO: currently this assumes properly formatted start line
 */
bool explode_start_line(char *buf, char *method, char *url, 
    char *protocol, int bytes)
{
    assert(buf);
    int i = 0, copied;

    i += strcpy_until(buf, method, i, bytes, ' ');
    if (i >= bytes)
        return false;

    i += strcpy_until(buf, url, ++i, bytes, ' ');
    if (i >= bytes)
        return false;

    copied = strcpy_until(buf, protocol, ++i, bytes, '\n');
    if (i + copied >= bytes)
        return false;
    if (protocol[copied - 1] == '\r')
        protocol[copied - 1] = '\0';

    return true;
}

/*
 * Break apart the url into host, path, and port if they exist.
 * References: https://cboard.cprogramming.com/c-programming/112381-using-sscanf-parse-string.html
 * 
 * Returns true if successfully parsed or false otherwise.
 */
bool explode_url(char *url, char *host, int *port, char *path)
{
    if (sscanf(url, "http://%[^:]:%d%s", host, port, path) == 3 ||
        sscanf(url, "https://%[^:]:%d%s", host, port, path) == 3)
    {
        // do nothing
    }
    else if (sscanf(url, "http://%[^:/]%s", host, path) == 2 ||
             sscanf(url, "https://%[^:/]%s", host, path) == 2)
    {
        *port = -1;
    }
    /*
    else if (sscanf( url, "http://%[^:/]:%d", host, &accessport)==2)
    {
        finport = (unsigned short) accessport;
        *path = '\0';
    }
    else if (sscanf (url, "http://%[^/]", host)==1)
    {
        finport = 80;
        *path = '\0';
    }*/
    else
    {
        return false;
    }
    return true;
}

int main(int argc, const char **argv) {
    //Arrays to be used for HTTP header
    char line[10000], method[10000], url[10000], protocol[10000], host[10000], path[10000];
    int parentfd; // parent socket 
    int childfd; // child socket 
    int portno; // port to listen on 
    int https; //bit to see if we need to relay https
    int accessport; //port of website to be accessed
    unsigned short finport; //final port for relay
    socklen_t addresslen; // byte size of client address 
    int optval; // flag value for setsockopt 
    FILE* sockrfp; // read socket file 
    FILE* sockwfp;// write socket file 
    struct sockaddr_in serveraddr; // server's addr 
    Cache* cache = initCache(100);
    fd_set master_fds;
    fd_set read_fds;
    int fdmax;
    int sckt; // looping variable
    ClientList clients;
    int bytes;

    check_usage(argc, argv);
    portno = atoi(argv[1]);
    
    // socket: create the parent socket 
    parentfd = socket(AF_INET, SOCK_STREAM, 0);
    if (parentfd < 0) 
        exit_failure("ERROR opening socket\n");

    //command to kill the server and avoid "already in use error"
    //taken from example server
    optval = 1;
    setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    //build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));


    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // set port number
    serveraddr.sin_port = htons((unsigned short)portno);

    //bind: associate the parent socket with a port 
    if (bind(parentfd, (struct sockaddr *) &serveraddr, 
        sizeof(serveraddr)) < 0) 
        exit_failure("ERROR on binding\n");

    /* 
    * listen: make this socket ready to accept connection requests 
    */
    if (listen(parentfd, 5) < 0) // allow 5 requests to queue up  
        exit_failure("ERROR on listen\n");

    /*
     * Clear memory in fdsets
     */
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(parentfd, &master_fds);
    fdmax = parentfd;

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE

    clients = ClientList_new();

    /***************************************************
     ****** main loop to handle socket connections******
     ***************************************************/
    memset(&addresslen, '\0', sizeof(addresslen));
    while(1){
        for (sckt = 0; sckt <= fdmax; sckt++) 
            if (!ClientList_keepalive(clients, sckt))
                close_client(&clients, &master_fds, sckt);

        read_fds = master_fds;

        /*
         * select: wait for a socket to be ready
         */
        printf("Waiting for socket to be ready\n");
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
            exit_failure("ERROR on select\n");

        for (sckt = 0; sckt <= fdmax; sckt++) 
        {
            if (!FD_ISSET(sckt, &read_fds))
                continue;
            
            if (sckt == parentfd) 
            {
                /* Incoming connection request */
                printf("Accepting new connection\n");
                childfd = accept(parentfd, (struct sockaddr *) &serveraddr, &addresslen);
                if (childfd < 0) 
                {
                    fprintf(stderr, "ERROR on accept\n");
                }
                else
                {
                    FD_SET(childfd, &master_fds);
                    fdmax = MAX(childfd, fdmax);
                    clients = ClientList_push(clients, childfd);
                }
            } 
            else 
            {
                childfd = sckt;
                /* Data arriving from already connected socket */

                if ((bytes = read(sckt, line, sizeof(line)-1)) <= 0)
                {
                    fprintf(stderr, "Failed to read\n");
                    close_client(&clients, &master_fds, childfd);
                    continue;
                }

                if (!explode_start_line(line, method, url, protocol, bytes))
                {
                    printf("Request is: %s\n", line);
                    fprintf(stderr, "Failed to Parse Request\n");
                    close_client(&clients, &master_fds, childfd);
                    continue;
                }

                if(url[0] == '\0') // TODO: why is this necessary?
                {
                    fprintf(stderr, "Bad url on Request\n");
                    close_client(&clients, &master_fds, childfd);
                    continue;
                }
                
                update(cache);
                if(strcmp(method, "GET") == 0 && strncasecmp(url, "http://", 7) == 0)
                {
                    if(!explode_url(url, host, &accessport, path))
                    {
                        fprintf(stderr, "Failed to parse url\n");
                        close_client(&clients, &master_fds, childfd);
                        continue;
                    }
                    if (accessport == -1)
                        finport = 80;
                    else
                        finport = (unsigned short) accessport;
                    
                    https = 0;    
                }
                else if (strcmp(method, "CONNECT")==0) //check for https request
                {
                    if(!explode_url(url, host, &accessport, path))
                    {
                        fprintf(stderr, "Failed to parse url\n");
                        close_client(&clients, &master_fds, childfd);
                        continue;
                    }
                    https = 1;
                }
                else // TODO: Add support for POST
                {
                        fprintf(stderr, "Unsupported method\n");
                        close_client(&clients, &master_fds, childfd);
                        continue;
                }

                //Start building destination
                //build the server's Internet address
                struct addrinfo destaddr; //destinatrion addr
                char portarray[10];
                struct addrinfo* fullinfo;
                struct sockaddr_in6 finsock;
                int sock_fam, sock_type, sock_prot, sock_len;
                int serversock;
                //initialize some values
                memset(&destaddr, 0, sizeof (destaddr));
                memset( (void*) &finsock, 0, sizeof(finsock) );

                destaddr.ai_family = PF_UNSPEC;
                destaddr.ai_socktype = SOCK_STREAM;
                //copy port into array
                snprintf( portarray, sizeof(portarray), "%d", (int) finport );
                //perform lookup
                if( (getaddrinfo(host, portarray, &destaddr, &fullinfo))!=0)
                {
                    fprintf(stderr, "Bad address not found\n");
                    close_client(&clients, &master_fds, childfd);
                    freeaddrinfo(fullinfo);
                    continue;
                }

                //loop through fulladdress info for ipv4 or ipv6 addresses
                struct addrinfo* ipv4 = (struct addrinfo*) 0;
                struct addrinfo* ipv6 = (struct addrinfo*) 0;
                for(struct addrinfo* i =fullinfo; i != (struct addrinfo*) 0; i=i->ai_next)
                {
                    //switch on address info family type to match ipv4 vs 6
                    switch (i->ai_family)
                    {
                    case AF_INET:
                        if(ipv4== (struct addrinfo*) 0)
                            ipv4 = i;
                        break;
                    case AF_INET6:
                    if(ipv6== (struct addrinfo*) 0)
                        ipv6 = i;
                    break;
                    }
                }
                //use ipv4 address if found
                if(ipv4 !=(struct addrinfo*) 0)
                {
                    sock_fam = ipv4->ai_family;
                    sock_type = ipv4->ai_socktype;
                    sock_prot = ipv4->ai_protocol;
                    sock_len = ipv4->ai_addrlen;
                    memmove(&finsock, ipv4->ai_addr, sock_len);
                }
                //use ipv6
                else
                {
                    sock_fam = ipv6->ai_family;
                    sock_type = ipv6->ai_socktype;
                    sock_prot = ipv6->ai_protocol;
                    sock_len = ipv6->ai_addrlen;
                    memmove(&finsock, ipv6->ai_addr, sock_len);
                }

                //now that final socket info is set up open socket to server
                serversock = socket(sock_fam, sock_type, sock_prot);
                if(serversock<0)
                {
                    fprintf(stderr, "Couldn't form server socket\n");
                    close_client(&clients, &master_fds, childfd);
                    continue;
                }
                if( connect(serversock, (struct sockaddr*) &finsock, sock_len) <0) 
                {
                    fprintf(stderr, "Couldn't connect to server\n");
                    close_client(&clients, &master_fds, childfd);
                    continue;
                }

                //Now client socket has been connected to server
                //open read and write channels
                
                sockrfp = fdopen(serversock, "r");
                sockwfp = fdopen(serversock, "w");
                FILE* clientsock = fdopen(childfd, "w");
                if(https)
                {
                    printf("Routting https!\n");
                    printf("Url is: %s\n", url);
                    relay_ssl(method, host, protocol, sockrfp, sockwfp, clientsock, cache, url);
                    printf("Finished routing\n");
                }
                else
                {
                    printf("routing client\n");
                    printf("url is: %s\n", url);
                    relay_http(method, path, protocol, serversock, childfd, cache, url, clients, line, bytes);
                    printf("Finished routing\n");
                }

                freeaddrinfo(fullinfo);
                close(serversock);
            }
        }
    }//while loop

    ClientList_free(&clients);   
}


