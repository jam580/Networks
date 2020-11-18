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
    b1->key = malloc(sizeof(char)*100);
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
static void relay_ssl(char* method, char* host, char* protocol, FILE* sockrfp, FILE* sockwfp, FILE * clientsock, Cache* cache, char*url)
{
    int client_read, server_read, client_write, server_write;
    struct timeval timeout;
    fd_set master;
    int maxp, r;
    char buff[10000];
    timeout.tv_sec = 3;
    timeout.tv_usec =0;
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
    int doneread, donewrite=0;

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
            donewrite=1;
            doneread=1;
        }
            
        else if (FD_ISSET(client_read, &master))
        {
            printf("Client\n");
            r = read(fileno(stdin), buff, sizeof(buff));
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
            r = write(fileno(clientsock), buff, r);
            if(r<=0)
                break;
        }
    }
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
            //printf("Next line is: %s\n", line);
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
        char* buff = malloc(1024*1024*1024);
        con_length = -1;
        first_line = 1;
        stat = -1;
        int cacheage = 0;
        char* firstlen;
        while( fgets(line, sizeof(line), sockrfp)!= (char*) 0)
        {
            //printf("Newline inside relayhttp: %s\n", line);
            
            if( strcmp(line, "\n") ==0 || strcmp(line, "\r\n") ==0 )
                break;
            if( first_line)
            {
                firstlen = strdup(line);
                fprintf(clientsock, "%s", line);
                fflush(clientsock);
                strcpy(buff, line);
                trim(line);
                sscanf(line, "%[^ ] %d %s", prot, &stat, coms);
                first_line=0;
            }
            else
            {
                strcat(buff,line);
                fprintf(clientsock, "%s", line);
                fflush(clientsock);
                trim( line);
                
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
        //fprintf(clientsock, "%s", end); 
        fprintf(clientsock, "%s", end);
        fflush(clientsock);
        
        strcat(buff,end);
        //strcat(buff,"Connection: close\r\n");

        //check for content
        if(con_length != -1)
        {
            printf("We have %ld content\n", con_length);
            for(i=0; i<con_length; i++)
            {
                hold = fgetc(sockrfp);
                //fputc(hold,clientsock);
                //fflush(clientsock);
                strncat(buff,&hold,sizeof(hold));
                fprintf(clientsock,"%c", hold);
                
            }
        }
        //flush content to client
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
    int https; //bit to see if we need to relay https
    int accessport; //port of website to be accessed
    unsigned short finport; //final port for relay
    int addresslen; // byte size of client address 
    int optval; // flag value for setsockopt 
    FILE* sockrfp; // read socket file 
    FILE* sockwfp;// write socket file 
    struct sockaddr_in serveraddr; // server's addr 
    Cache* cache = initCache(10);
    

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


    /***************************************************
     ****** main loop to handle socket connections******
     ***************************************************/

    while(1){
        /* 
        * accept: wait for a connection request 
        */
       printf("Waiting for new connection\n");
        childfd = accept(parentfd, (struct sockaddr *) &serveraddr, &addresslen);
        if (childfd < 0) 
            error("ERROR on accept");
        int stdin_save = dup(STDIN_FILENO); // saved stdin for potential reset 
        dup2(childfd,STDIN_FILENO); // set stdin to client socket 
        
        //start reading request from STDIN
        if( fgets(line, sizeof(line), stdin)== (char*)0)
            error("Failed to get Request");
        printf("Just got line %s\n", line);
        //clean up and parse request
        trim(line);
        if(sscanf( line, "%[^ ] %[^ ] %[^ ]", method, url, protocol ) != 3 )
        {
            printf("Line is: %s\n", line);
            error("Failed to Parse Request");
        }
            

        if(url[0] =='\0')
            error("Bad url on Request");

        update(cache);
        if(strncasecmp(url, "http://", 7) == 0)
        {
            strncpy( url, "http", 4 );

            if(sscanf( url, "http://%[^:/]:%d%s", host, &accessport, path)==3)
                finport = (unsigned short) accessport;
            //if no port is specified assume 80
            else if (sscanf( url, "http://%[^:/]%s", host, path)==2)
                finport = 80;
            else if (sscanf( url, "http://%[^:/]:%d", host, &accessport)==2)
            {
                finport = (unsigned short) accessport;
                *path = '\0';
            }
            else if (sscanf (url, "http://%[^/]", host)==1)
            {
                finport = 80;
                *path = '\0';
            }
            else
            {
                error("Could not sscanf on url");
            }
            https = 0;    
        }
        else if (strcmp(method, "CONNECT")==0) //check for https request
        {
            if (sscanf(url, "%[^:]:%d", host, &accessport) ==2)
                finport = (unsigned short) accessport;
            else if (sscanf(url, "%s", host)==1)
                finport = 443;
            else
            {
                error("Failed to parse https url");
            }
            https = 1;
        }

        

        //break apart the url and get host / path / port if they exist
        //I referenced a few pages to help with this part including:
        //https://cboard.cprogramming.com/c-programming/112381-using-sscanf-parse-string.html

        //if sscanf properly parses hostname into host buffer, a port number into accessport, 
        // and the rest into path
        


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
            error("Bad address not found");

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
            error("Couldnt form server socket");
        if( connect(serversock, (struct sockaddr*) &finsock, sock_len) <0) 
            error("Couldn't connect to server");

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
        }
        else
        {
            printf("routing client\n");
            printf("url is: %s\n", url);
            relay_http(method, path, protocol, sockrfp, sockwfp, clientsock, cache, url);
        }
        //flush input
        while(fgets(line, sizeof(line), stdin) != (char*)0 )
        {
            printf("Line is: %s\n", line);
        }
        close(serversock);
        //dup2(stdin_save,STDIN_FILENO);
        close(childfd); 
    }//while loop
    
}


