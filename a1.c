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
#include "failure.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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
    b1->Object = (char*) malloc(1024*10240);
    b1->key = calloc(100, sizeof(char));
    b1->valid = 0;
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
    while(segment[i-1] =='\n' ||segment[i-1] =='\r')
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

static void relay_http(char* method, char* path, char* protocol, FILE* sockrfp, FILE* sockwfp, FILE* clientsock, Cache* cache, char* url)
{
    char line[10000], prot[10000], coms[10000];
    int first_line, stat;
    char hold;
    long con_length =-1;
    long i;
    int found = -1; //bool to dictate cache hit
    char* endptr;
    double age;

    //look for cache hit
    update(cache);
    found = find(url,cache);
    
    //if there is a cache hit
    if(found!=-1)
    {
        fprintf(clientsock, "%s",cache->Entries[found]->firstline);
        fflush(clientsock);
        long currentage = (long) time(NULL) - (long) cache->Entries[found]->intime;
        fprintf(clientsock, "Age: %ld \n",currentage);
        fflush(clientsock);
        fprintf(clientsock, "%s", cache->Entries[found]->Object);
        fflush(clientsock);

        //now that we've sent the item, move it to front of cache
        Block* temp = cache->Entries[found];
        for(int i=found; i>0;i--)
            cache->Entries[i]=cache->Entries[i-1];
        cache->Entries[0]=temp;
        temp=NULL;
        //flush the remainder of the client request
        while(fgets(line, sizeof(line), stdin) != (char*)0 )
        {
            if( strcmp( line, "\n" ) == 0 || strcmp( line, "\r\n" ) == 0 )
                break;
            if( strncasecmp(line, "Content-Length:", 15) ==0)
                con_length=atol( &(line[15]));
        }
        if(con_length !=-1)
            for(i=0; i<con_length && (hold = getchar())!= EOF; i++ );
    }//end of cache hit
    else
    {
        //First send http request, add back the \r\n
        fprintf(sockwfp, "%s %s %s\r\n", method, path, protocol);
        //make sure all info is sent
        while(fgets(line, sizeof(line), stdin) != (char*)0 )
        {
            if( strcmp( line, "\n" ) == 0 || strcmp( line, "\r\n" ) == 0 )
                break;
            fputs(line, sockwfp);
            trim(line);
            if( strncasecmp(line, "Content-Length:", 15) ==0)
                con_length=atol( &(line[15]));
        }
        // flush to make sure all forwarded
        fputs(line, sockwfp);
        fflush(sockwfp);
        //check for content and flush every char
        if(con_length !=-1)
        {
            for(i=0; i<con_length && (hold = getchar())!= EOF; i++ )
                putc(hold, sockwfp);
        }
        
        fflush(sockwfp);

        //recieve response form server and forward to client
        char* buff = calloc(1024*1024, sizeof(char));
        con_length = -1;
        first_line = 1;
        stat = -1;
        int cacheage = 0;
        char* firstlen;
        while( fgets(line, sizeof(line), sockrfp)!= (char*) 0)
        {
            
            if( strcmp(line, "\n") ==0 || strcmp(line, "\r\n") ==0 )
                break;
            if( first_line)
            {
                firstlen = strdup(line);
                fprintf(clientsock, "%s", line);
                trim(line);
                sscanf(line, "%[^ ] %d %s", prot, &stat, coms);
                first_line=0;
            }
            else
            {
                strncat(buff,line,sizeof(line));
                trim( line);
                //grab the first line
                
                if (strncasecmp(line, "Content-Length:",15)==0)
                    con_length = atol( &(line[15]));
                if (strncasecmp(line, "Cache-Control: max-age=",23)==0)
                {
                    age = strtod(&(line[23]), &endptr);
                    cacheage = 1;
                }
            }    
        } //end of reading headers
        if(!cacheage)
            age = 3600;
        //Add the end of the header 
        char* end = "\r\n";
        strncat(buff,end,sizeof(end));

        //check for content
        if(con_length != -1)
        {
            for(i=0; i<con_length; i++)
            {
                hold = fgetc(sockrfp);
                //fputc(hold,clientsock);
                //fflush(clientsock);
                strncat(buff,&hold,sizeof(hold));
                if(i==10000)
                    break;
            }
        }
        //Send entire http response to client
        fprintf(clientsock, "%s", buff);
        fflush(clientsock);

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
        strcpy(cache->Entries[found]->Object, buff);
        //update key to url
        strcpy(cache->Entries[found]->key, url);
        cache->Entries[found]->maxage=age;
        cache->Entries[found]->intime=time(NULL);
        cache->Entries[found]->firstline=strdup(firstlen);
        //value is now cached
    }//end of cache miss
    
    
}

int main(int argc, char **argv) {
    //Arrays to be used for HTTP header
    char line[10000], method[10000], url[10000], protocol[10000], host[10000], path[10000];
    int parentfd; // parent socket 
    int childfd; // child socket 
    int portno; // port to listen on 
    int accessport; //port of website to be accessed
    unsigned short finport; //final port for relay
    socklen_t addresslen; // byte size of client address 
    int optval; // flag value for setsockopt 
    FILE* sockrfp; // read socket file 
    FILE* sockwfp;// write socket file 
    struct sockaddr_in serveraddr; // server's addr 
    Cache* cache = initCache(10);
    fd_set master_fds;
    fd_set read_fds;
    int fdmax;
    int sckt; // looping variable

    //check command line arguments 
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    
    // socket: create the parent socket 
    parentfd = socket(AF_INET, SOCK_STREAM, 0);
    if (parentfd < 0) 
        error("ERROR opening socket");

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
        error("ERROR on binding");

    /* 
    * listen: make this socket ready to accept connection requests 
    */
    if (listen(parentfd, 5) < 0) // allow 5 requests to queue up  
        error("ERROR on listen");

    /*
     * Clear memory in fdsets
     */
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(parentfd, &master_fds);
    fdmax = parentfd;

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE

    /***************************************************
     ****** main loop to handle socket connections******
     ***************************************************/
    memset(&addresslen, '\0', sizeof(addresslen));
    while(1){
        read_fds = master_fds;

        /*
         * select: wait for a socket to be ready
         */
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
            exit_failure("ERROR on select\n");

        for (sckt = 0; sckt <= fdmax; sckt++) 
        {
            if (!FD_ISSET(sckt, &read_fds))
                continue;
            
            if (sckt == parentfd) 
            {
                /* Incoming connection request */
                childfd = accept(parentfd, (struct sockaddr *) &serveraddr, &addresslen);
                if (childfd < 0) 
                {
                    fprintf(stderr, "ERROR on accept\n");
                }
                else
                {
                    FD_SET(childfd, &master_fds);
                    fdmax = MAX(childfd, fdmax);
                    printf("Accepted %d\n", childfd);
                }
            } 
            else 
            {
                childfd = sckt;
                /* Data arriving from already connected socket */
                // int stdin_save = dup(STDIN_FILENO); // saved stdin for potential reset 
                dup2(childfd,STDIN_FILENO); // set stdin to client socket 

                //start reading request from STDIN
                if( fgets(line, sizeof(line), stdin)== (char*)0)
                {
                    fprintf(stderr, "Failed to get Request\n");
                    close(childfd);
                    FD_CLR(childfd, &master_fds);
                }
                //clean up and parse request
                trim(line);
                if(sscanf( line, "%[^ ] %[^ ] %[^ ]", method, url, protocol ) != 3 )
                {
                    fprintf(stderr, "Failed to Parse Request\n");
                    close(childfd);
                    FD_CLR(childfd, &master_fds);
                }

                //ensure http is lower case
                strncpy( url, "http", 4 ); // TODO why is this necessary?
                
                update(cache);


                //break apart the url and get host / path / port if they exist
                //I referenced a few pages to help with this part including:
                //https://cboard.cprogramming.com/c-programming/112381-using-sscanf-parse-string.html

                //if sscanf properly parses hostname into host buffer, a port number into accessport, 
                // and the rest into path
                if(sscanf( url, "http://%[^:/]:%d%s", host, &accessport, path)==3)
                    finport = (unsigned short) accessport;
                //if no port is specified assume 80
                else if (sscanf( url, "http://%[^:/]%s", host, path)==2)
                    finport = 80;


                //Start building destination
                //build the server's Internet address
                struct addrinfo destaddr; //destinatrion addr
                char portarray[10];
                struct addrinfo* fullinfo;
                struct sockaddr_in6 finsock;\
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
                    fprintf(stderr, "Bad address not found");
                    close(childfd);
                    FD_CLR(childfd, &master_fds);
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
                    fprintf(stderr, "Couldnt form server socket");
                    close(childfd);
                    FD_CLR(childfd, &master_fds);
                }
                if( connect(serversock, (struct sockaddr*) &finsock, sock_len) <0) 
                {
                    fprintf(stderr, "Couldn't connect to server");
                    close(childfd);
                    FD_CLR(childfd, &master_fds);
                }

                //Now client socket has been connected to server
                //open read and write channels
                
                sockrfp = fdopen(serversock, "r");
                sockwfp = fdopen(serversock, "w");
                FILE* clientsock = fdopen(childfd, "w");
                relay_http(method, path, protocol, sockrfp, sockwfp, clientsock, cache, url); // TODO look at error handling
                close(serversock);
                close(childfd); 
                FD_CLR(childfd, &master_fds);
                printf("Closing %d\n", childfd);
            }
        }
    }//while loop
    
}


