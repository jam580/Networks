/*******************************************************************************
*
* Tufts University
* Comp112 A1 - Memory, Strings, & Data Structures in C
* By: Ramon Fernandes
* Last Edited: October 1, 2020
*                               
* a1.c - main program processor for a proxy HTTP server
* Command Arguments:
*      port: port to listen to for incoming connections
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>

struct ConnectionDetails
{
    int fd; // socket
    struct sockaddr_in addr; // socket connection info
    socklen_t len; // addr length
    int portno; // port number to connect to
};

struct CacheItem
{
    char* key;
    char* value;
    int secs_to_live;
    time_t created_at;
    time_t last_accessed_at;
};

void check_usage(int argc, const char *argv[]);
void exit_failure(char* msg);
size_t ensure_capacity(char **stringptr, size_t current_capacity, 
                       size_t desired_size);

void initialize_socket(struct ConnectionDetails *cd);
void set_sockaddr(struct ConnectionDetails *cd, int portno, 
                  unsigned int s_addr);

ssize_t read_socket(int sckt, char **s, size_t n, size_t curr_s_len);
ssize_t write_socket(int sckt, const char*s, size_t n);
int check_for_req_hdr_end(char *msg, size_t len);
char* get_req_path(char *hdr, size_t len);
char* get_http_meth(char *hdr, size_t len);
char* get_host(char *hdr, size_t len);
int get_port(char *host, int p);
int get_max_age(char* msg, int d);
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void update_cache(struct CacheItem **cache, int size, struct CacheItem *it);
void free_cache_item(struct CacheItem *it);
char* get_value_by_key(struct CacheItem **cache, int size, char *key);
struct CacheItem *get_item_by_key(struct CacheItem **cache, int size, 
                                  char *key);

void Mem_ensure_allocated(void *ptr);
void *Mem_alloc (long nbytes);
void *Mem_calloc(long count, long nbytes);
void Mem_free(void *ptr);
void *Mem_resize(void *ptr, long nbytes);
#define ALLOC(nbytes) Mem_alloc((nbytes))
#define CALLOC(count, nbytes) Mem_calloc((count), (nbytes))
#define NEW(p) ((p) = ALLOC((long)sizeof *(p)))
#define NEW0(p) ((p) = CALLOC(1, (long)sizeof *(p)))
#define FREE(ptr) ((void)(Mem_free((ptr)), (ptr) = NULL))
#define RESIZE(ptr, nbytes) ((ptr) = Mem_resize((ptr), (nbytes)))

char *Str_sub(const char *s, size_t i, size_t j);
char *Str_cat_replace(char **s1, char *s2, size_t s1_len, size_t s2_len);
char *Str_cat(const char *s1, size_t i1, size_t j1,
              const char *s2, size_t i2, size_t j2);
size_t Str_len(const char *s, size_t i, size_t j);
int Str_cmp(const char *s1, size_t i1, size_t j1,
            const char *s2, size_t i2, size_t j2);
int Str_chr(const char *s, size_t i, size_t j, char c);
int Str_find(const char *s, size_t i, size_t j, const char *str);
int Str_any(const char *s, size_t i, size_t j, const char *set);

/*******************************************************************************
 * Main program handling functions.
 ******************************************************************************/
int main(int argc, const char *argv[])
{
    struct ConnectionDetails parent;
    struct ConnectionDetails child;
    struct ConnectionDetails server;
    int i; // looping variable
    int bytes; // message byte size (the return value for read() and write())
    size_t bufsize = 1024; // standard buffer size
    char *buf; // message buffer
    char *reqmsg; // full request message
    char *resmsg; // full response message
    int failed; // failed flag for while loop
    int cache_size; // size of cache
    struct CacheItem **cache;
    struct CacheItem *dummy_item;
    time_t curr_time;
    int max_age, first_line_len;
    char *max_age_line;
    char *server_path, *host;
    
    struct hostent *serverip; // server ip address information
    size_t reqmsg_len; // total length of request message
    size_t sent, received; // total bytes sent and received

    check_usage(argc, argv);
    parent.portno = atoi(argv[1]);

    cache_size = 10;
    cache = CALLOC(cache_size, sizeof *cache);
    for (i = 0; i < cache_size; i++) 
    {
        cache[i] = NULL;
    }

    initialize_socket(&parent);
    if (parent.fd < 0)
        exit_failure("ERROR opening socket\n");

    set_sockaddr(&parent, parent.portno, INADDR_ANY);
    
    if (bind(parent.fd, (struct sockaddr *) &(parent.addr), parent.len) < 0)
        exit_failure("ERROR binding to server port\n");

    if (listen(parent.fd, 5) < 0)
        exit_failure("ERROR listening to server port\n");

    child.len = sizeof(child.addr);
    buf = ALLOC(bufsize * (sizeof *buf));
    while (1) {
        reqmsg = NULL;
        resmsg = NULL;
        server_path = NULL;
        host = NULL;
        failed = 0;
        child.fd = accept(parent.fd, (struct sockaddr *) &(child.addr), 
                         &(child.len));
        if (child.fd < 0) 
        {
            fprintf(stderr, "ERROR connecting to client\n");
            continue;
        }

        received = 0;
        do {
            bytes = read_socket(child.fd, &reqmsg, bufsize, received);
            if (bytes <= 0)
            {
                fprintf(stderr, "ERROR reading from client socket\n");
                failed = 1;
                break;
            }
            received += bytes;
        } while (!check_for_req_hdr_end(reqmsg, received));
        if (failed) 
        {
            close(child.fd);
            continue;
        }
        reqmsg_len = strlen(reqmsg);

        server_path = get_req_path(reqmsg,reqmsg_len);
        host = get_host(reqmsg, reqmsg_len);
        server.portno = get_port(host, 80);
        dummy_item = get_item_by_key(cache, cache_size, server_path);
        
        if (dummy_item != NULL) 
        {
            resmsg = dummy_item->value;
            curr_time = time(NULL);
            received = strlen(resmsg);

            // Write first line
            first_line_len = Str_find(resmsg, 0, received, "\n") + 1;
            bytes = write_socket(child.fd, resmsg, first_line_len);
            if (bytes < 0)
            {
                fprintf(stderr, "ERROR writing message to socket\n");
                close(child.fd);
                continue;
            }
            // Write Age key
            max_age_line = CALLOC(100,sizeof *max_age_line);
            sprintf(max_age_line, "Age: %ld\r\n", 
                    curr_time - dummy_item->created_at);
            bytes = write_socket(child.fd, max_age_line, strlen(max_age_line));
            FREE(max_age_line);
            if (bytes < 0)
            {
                fprintf(stderr, "ERROR writing message to socket\n");
                close(child.fd);
                continue;
            }
            // Write rest of value
            bytes = write_socket(child.fd, resmsg + first_line_len, 
                                 received - first_line_len);
            if (bytes < 0)
            {
                fprintf(stderr, "ERROR writing message to socket\n");
                close(child.fd);
                continue;
            }
            FREE(server_path); // don't need to store it again
        }
        else 
        {
            serverip = gethostbyname(host);
            if (serverip == NULL)
            {
                fprintf(stderr, "ERROR, no such host\n");
                close(child.fd);
                continue;
            }
            
            initialize_socket(&server);
            if (server.fd < 0)
            {
                fprintf(stderr, "ERROR opening server socket\n");
                close(child.fd);
                continue;
            }
            set_sockaddr(&server, server.portno, INADDR_NONE);
            memset(&(server.addr.sin_addr.s_addr), '\0', 
                sizeof(server.addr.sin_addr.s_addr));
            memcpy(&(server.addr.sin_addr.s_addr), serverip->h_addr, 
                    serverip->h_length);

            if (connect(server.fd, (struct sockaddr *) &(server.addr), 
                        server.len) < 0)
            {
                fprintf(stderr, "ERROR connecting to server\n");
                close(child.fd);
                close(server.fd);
                continue;
            }

            sent = 0;
            do {
                bytes = write(server.fd, reqmsg + sent, reqmsg_len - sent);
                if (bytes < 0)
                {
                    fprintf(stderr, "ERROR writing message to socket\n");
                    failed = 1;
                    break;
                }
                if (bytes == 0)
                    break;
                sent += bytes;
            } while (sent < reqmsg_len);
            if (failed)
            {
                close(server.fd);
                close(child.fd);
                continue;
            }

            received = 0;
            do {
                bytes = read_socket(server.fd , &resmsg, bufsize, received);
                if (bytes < 0)
                {
                    fprintf(stderr, "ERROR reading response from socket\n");
                    failed = 1;
                    break;
                }
                received += bytes;
            } while (bytes != 0);
            if (failed)
            {
                close(server.fd);
                close(child.fd);
                continue;
            }

            close(server.fd);
            max_age = get_max_age(resmsg, 60*60);
            NEW(dummy_item);
            dummy_item->key = server_path;
            dummy_item->value = resmsg;
            dummy_item->secs_to_live = max_age;
            curr_time = time(NULL);
            dummy_item->created_at = curr_time;
            dummy_item->last_accessed_at = curr_time;
            update_cache(cache, cache_size, dummy_item);

            bytes = write_socket(child.fd, resmsg, received);
            if (bytes < 0)
            {
                close(child.fd);
                fprintf(stderr, "ERROR writing message to socket\n");
                continue;
            }
        }

        close(child.fd);
        FREE(reqmsg);
        FREE(host);
    }
    close(parent.fd);

    for (int i = 0; i < cache_size; i++) {
        if (cache[i] != NULL) {
            free_cache_item(cache[i]);
        }
    }
    FREE(cache);
    FREE(buf);
    FREE(host);
    return EXIT_SUCCESS;
}

/*
 * ARGS: 
 *      argc: integer number of command line arguments
 *      argv: char**, the command line arguments
 * DOES: asserts proper usage of command (correct # of arguments, etc.)
 *       exits with error if improper usage.
 * RETURNS: nothing
 */
void check_usage(int argc, const char* argv[])
{
    if (argc != 2) {
        // +2 is 1 for the extra space around %s and 1 for the null terminator
        int buffer_size = strlen("Usage: <port>\n") + strlen(argv[0]) + 2;
        char msg[buffer_size];
        sprintf(msg, "Usage: %s <port>\n", argv[0]);
        exit_failure(msg);
    }
}

/*
 * ARGS: 
 *      msg: char*, a null-terminated string to print to stderr
 * DOES: Prints the given message and exits with error
 * RETURNS: Nothing
 */
void exit_failure(char* msg)
{
    fprintf(stderr, msg);
    exit(EXIT_FAILURE);
}

/* 
 * ARGS: 
 *      stringptr: a pointer to a malloc'ed string
 *      current_capacity: the capacity of the string
 *      desired_size: lower bound for number of character *stringptr should
 *                    be able to hold (including the null terminator)
 * DOES: ensures that there is enough space for desired_size characters in 
 *       *stringptr
 * RETURNS: the capacity of stringptr
 */
size_t ensure_capacity(char **stringptr, size_t current_capacity, 
                       size_t desired_size)
{
        char *src = *stringptr, *destination = NULL;
        size_t current_size = strlen(src), new_capacity, i;

        if (desired_size >= current_capacity) {
            new_capacity = current_capacity * 2 + 3;
            new_capacity = new_capacity >= desired_size ? new_capacity : 
                           desired_size + 3;
        } else {
            return current_capacity;
        }

        destination = malloc(new_capacity * sizeof *destination);
        for (i = 0; i < current_size; i++) {
            destination[i] = src[i];
        }
        if (current_size == 0) {
            memset(destination, '\0', new_capacity);
        }

        *stringptr = destination;
        FREE(src);
        return new_capacity;
}

/*******************************************************************************
 * Functions to handle ConnectionDetails initialization
 ******************************************************************************/
/*
 * Function: initialize_socket
 * ----------------------------
 *   Initializes fd in sckt using the AF_INET address domain, a SOCK_STREAM 
 *   type, and a 0 protocal (dynamically decide based on type). Sets fd to
 *   < 0 if it could not be initialized. Sets opt vals to allow for the reuse
 *   of local addresses.
 *
 *   cd: contains fd to initialize
 */
void initialize_socket(struct ConnectionDetails *cd)
{
    int optval;
    cd->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cd->fd >= 0)
    {
        optval = 1;
        setsockopt(cd->fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, 
                   sizeof(int));
    }
}

/*
 * Function: set_sockaddr
 * ----------------------------
 *   Sets addr in cd to the given values. And updates the len of addr.
 *
 *   cd: pointer to ConnectionDetails that contains addr to initialize
 *   portno: the port number to connect to
 *   sin_addr: IP address of the host
 */
void set_sockaddr(struct ConnectionDetails *cd, int portno, unsigned int s_addr)
{
    cd->addr.sin_family = AF_INET;
    cd->addr.sin_port = htons(portno);
    cd->addr.sin_addr.s_addr = s_addr;
    cd->len = sizeof(cd->addr);
}

/*******************************************************************************
 * Processing Functions
 ******************************************************************************/
/*
 * Function: read_socket
 * ----------------------------
 *   reads maximum n bytes from sckt and concatenates result to s
 *
 *   sckt: the socket to read from
 *   s: pointer to location to append read bytes to
 *   n: maximum number of bytes to read
 */
ssize_t read_socket(int sckt, char **s, size_t n, size_t curr_s_len)
{
    char *buf;
    ssize_t bytes;
    
    buf = CALLOC(n + 1, sizeof *buf);
    bytes = read(sckt, buf, n);
    if (bytes < 0)
        return bytes;
    buf[bytes] = '\0';
    *s = Str_cat_replace(s, buf, curr_s_len, bytes);
    FREE(buf);
    return bytes;
}

/*
 * Function: write_socket
 * ----------------------------
 *   Writes n characters of s to sckt until all bytes written or first
 *   failure. Returns total bytes written or -1 if failed.
 *
 *   sckt: the socket to write t0
 *   s: pointer to first char to write
 *   n: number of bytes to write
 */
ssize_t write_socket(int sckt, const char*s, size_t n)
{
    size_t sent;
    int bytes;
    sent = 0;
    do
    {
        bytes = write(sckt, s + sent, n - sent);
        if (bytes < 0)
            return bytes;
        sent += bytes;
    } while (sent < n);
    return sent;
}

/*
 * ARGS:
 *      msg: char* containg the header characters read so far
 * DOES: Determines where the end of the request header has been received
 * RETURNS: 0 if false, 1 otherwise
 */
int check_for_req_hdr_end(char *msg, size_t len)
{
    return len < 4 || Str_cmp(msg, len - 4, len, "\r\n\r\n", 0, 4) == 0;
}

char* get_req_path(char *hdr, size_t len)
{
    int i, j;
    assert(hdr != NULL);
    i = 4; // first line will always be GET path method
    j = Str_chr(hdr, i, len, ' '); 
    return Str_sub(hdr, i, j);
}

char* get_http_meth(char *hdr, size_t len)
{
    int i, j;
    assert(hdr != NULL);
    i = 4; // first line will always be GET path method
    i = Str_chr(hdr, i, len, ' ') + 1;  // skip over server path
    j = Str_any(hdr, i, len, "\r\n"); 
    return Str_sub(hdr, i, j);
}

char* get_host(char *hdr, size_t len)
{
    int i, j;
    char *match = "Host: ";
    size_t match_len = strlen(match);
    assert(hdr != NULL);
    i = Str_chr(hdr, 0, len, '\n') + 1; // skip over first line
    while (Str_cmp(hdr, i, i + match_len, match, 0, match_len) != 0)
        i = Str_chr(hdr, i, len, '\n') + 1;
    i = i + match_len;
    j = Str_any(hdr, i, len, "\r\n"); 
    return Str_sub(hdr, i, j);
}

/*
 * ARGS:
 *      host: char* containing the null-terminated full host name
 *      p: port to default to if one isn't specified in hostpp
 * DOES: Extracts the port from hostpp if one exists. Removes the port from
 *       hostpp and returns it as an int
 * RETURNS: the port or default if one isn't specified
 */
int get_port(char *host, int p)
{
    size_t len = strlen(host), i;

    for (i = 0; i < len; i++)
    {
        if (host[i] == ':')
        {
            int port = atoi(host + i + 1);
            host[i] = '\0';
            return port;
        }
    }

    return p;
}

/*
 * Function: get_max_age
 * ----------------------------
 *   Searches for Cache-Control in the given msg and return the max-age value. 
 *   Returns 0 if Cache-Control was not found or max-age was not specified. 
 *   It is a checked runtime error for msg to be null. It is an unchecked 
 *   runtime error for msg to not be properly formatted. 
 * 
 *   msg: properly formatted header and body response
 *   default: default max age to return
 */
int get_max_age(char* msg, int d)
{
    size_t len, match_len, dir_key_len;
    char *match, *directives, *dir_key, *max_age;
    int i, j, k, max_age_i, max_age_index;
    assert(msg != NULL);
    len = strlen(msg);
    match = "Cache-Control: ";
    match_len = strlen(match);
    dir_key = "max-age=";
    dir_key_len = strlen(dir_key);
    i = Str_find(msg, 0, len, "\n") + 1; // skip over first line
    j = i;
    
    while ((size_t)j < len)
    {
        if (msg[i] == '\n' || Str_cmp(msg, i, i + 2, "\r\n", 0, 2) == 0)
            return d; // end of header

        j = Str_find(msg, i, len, "\n"); // end of line
        if (Str_cmp(msg, i, MIN(i + match_len, (size_t)j), match, 0, 
                    match_len) == 0)
        {
            directives = Str_sub(msg, i + match_len, j);
            for (k = 0; (size_t)k < (j - i - match_len); k++)
                directives[k] = tolower(directives[k]);
            max_age_index = Str_find(directives, 0, 
                strlen(directives), dir_key);
            if (i < max_age_index)
                return d; // no max-age directive
            max_age_index += dir_key_len;
            k = Str_find(directives, max_age_index, strlen(directives), ",");
            j = k < 0 ? (int)strlen(directives) : k;
            max_age = Str_sub(directives, max_age_index, j);
            max_age_i = atoi(max_age);
            free(max_age);
            return max_age_i;
        }
        i = j + 1;
    }
    return d;
}

/*******************************************************************************
 * Functions for the cache
 ******************************************************************************/
void update_cache(struct CacheItem **cache, int size, struct CacheItem *it)
{
    // Check if already in cache
    for (int i = 0; i < size; i++) {
        if (cache[i] != NULL && strcmp(cache[i]->key, it->key) == 0) {
            free_cache_item(cache[i]);
            cache[i] = it;
            return;
        }
    }

    // Check for an empty space
    for (int i = 0; i < size; i++) {
        if (cache[i] == NULL) {
            cache[i] = it;
            return;
        }
    }
    
    // Cache full, check for and replace expired key
    time_t curr_time = time(NULL);
    for (int i = 0; i < size; i++) {
        struct CacheItem *currit = cache[i];
        if (currit->created_at + currit->secs_to_live >= curr_time) {
            free_cache_item(currit);
            cache[i] = it;
            return;
        }
    }

    // Cache full, replace last accessed key
    int oldest_index = 0;
    double oldest_time = difftime(curr_time, (cache[0])->last_accessed_at);
    for (int i = 1; i < size; i++) {
        struct CacheItem *currit = cache[i];
        double time_diff = difftime(curr_time, currit->last_accessed_at);
        if (time_diff > oldest_time) {
            oldest_index = i;
            oldest_time = time_diff;
        }
    }
    free_cache_item(cache[oldest_index]);
    cache[oldest_index] = it;
}

void free_cache_item(struct CacheItem *it)
{
    if (it != NULL) {
        FREE(it->key);
        FREE(it->value);
    }
    FREE(it);
    it = NULL;
}

char* get_value_by_key(struct CacheItem **cache, int size, char *key)
{
    time_t curr_time = time(NULL);
    for (int i = 0; i < size; i++) {
        struct CacheItem *currit = cache[i];
        if (currit != NULL && strcmp(currit->key, key) == 0 && 
            curr_time < currit->created_at + currit->secs_to_live) {
            return currit->value;
        }
    }

    return NULL;
}

struct CacheItem *get_item_by_key(struct CacheItem **cache, int size, char *key)
{
    time_t curr_time = time(NULL);
    for (int i = 0; i < size; i++) {
        struct CacheItem *currit = cache[i];
        if (currit != NULL && strcmp(currit->key, key) == 0 && 
            curr_time < currit->created_at + currit->secs_to_live) {
            return currit;
        }
    }

    return NULL;
}

/*******************************************************************************
 * Functions to handle Memory (Mem)
 * Adapted from C Interfaces and Implmentations by David T. Hanson
 ******************************************************************************/
/*
 * Function: Mem_ensure_allocated
 * ----------------------------
 *   Ensures that ptr is not null, exiting the program with failure if it is
 *
 *   ptr: a pointer or NULL
 */
void Mem_ensure_allocated(void *ptr)
{
    if (ptr == NULL)
        exit_failure("Memory allocation failed.\n");
}

/*
 * Function: Mem_alloc
 * ----------------------------
 *   Allocates nbytes and returns a pointer to the first byte. The bytes
 *   are uninitialized. It is a checked runtime error for nbytes to be
 *   nonpositive. The program exits if the memory cannot be allocated.
 *
 *   nbytes: positive integer-like value of bytes to allocate
 *
 *   returns: pointer to the first byte of the allocated nbytes
 */
void *Mem_alloc(long nbytes)
{
    void *ptr;
    assert(nbytes > 0);
    ptr = malloc(nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}

/*
 * Function: Mem_calloc
 * ----------------------------
 *   Allocates a block large enough to hold an array of count elements each of 
 *   size nbytes and returns a pointer to the first element. The block is
 *   initialized to zeros. It is a checked runtime error for count or nbytes
 *   to be nonpositive. The program exits if the memory cannot be allocated.
 *
 *   count: positive integer-like number of elements to allocate
 *   nbytes: positive integer-like value of bytes to allocate for each element
 *
 *   returns: pointer to the first element of the allocated array
 */
void *Mem_calloc(long count, long nbytes) 
{
    void *ptr;
    assert(count > 0);
    assert(nbytes > 0);
    ptr = calloc(count, nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}

/*
 * Function: Mem_free
 * ----------------------------
 *   Deallocates memory at ptr. It is an unchecked runtime error to free a
 *   pointer not previous allocated with Mem_alloc, Mem_calloc, or Mem_resize.
 *
 *   ptr: the pointer to deallocate
 */
void Mem_free(void *ptr) 
{
    if (ptr)
        free(ptr);
}

/*
 * Function: Mem_resize
 * ----------------------------
 *   Changes the size of the block at ptr, previously allocated with Mem_alloc,
 *   Mem_calloc, or Mem_resize. Exits the program if memory cannot be allocated.
 *   If nbytes exceeds the size of the blocked pointed to by ptr, the excess
 *   bytes are uninitialized. Otherwise, nbytes beginning at ptr are copied to
 *   the new block. It is a checked runtime error to pass a null ptr and for
 *   nbytes to be nonpositive. It is an unchecked runtime error to pass a ptr
 *   that was not previously allocated by Mem_alloc, Mem_calloc, or Mem_resize.
 *
 *   ptr: non-null pointer to resize
 *   nbytes: positive number of bytes to resize ptr to (expand or contract)
 * 
 *   returns: pointer to the first byte of the resized block.
 */
void *Mem_resize(void *ptr, long nbytes) 
{
    assert(ptr);
    assert(nbytes > 0);
    ptr = realloc(ptr, nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}

/*******************************************************************************
 * Functions to handle Strings (Str)
 * Adapted from C Interfaces and Implmentations by David T. Hanson
 ******************************************************************************/
/*
 * Function: Str_sub
 * ----------------------------
 *   Returns the substring of s from s[i] to s[j-1] inclusive (i.e. returns 
 *   s[i:j]). It is a checked runtime error to provide an i > j or 
 *   j > strlen(s). It is an unchecked runtime error to provide an s that is not
 *   null terminated.
 *
 *   s: a null-terminated array of characters
 *   i: the nonnegative index of s that begins the substring. Must be <= j.
 *   j: the nonnegative index of s that is one past the last char of the 
 *      substring. Must be >= i and <= strlen(s).
 * 
 *   returns: pointer to the first byte of the malloc'ed substring.
 */
char *Str_sub(const char *s, size_t i, size_t j) 
{
    char *str, *p;
    assert(i <= j && j <= strlen(s));
    p = str = ALLOC(j - i + 1);
    while (i < j)
        *p++ = s[i++];
    *p = '\0';
    return str;
}

/*
 * Function: Str_cat
 * ----------------------------
 *   Returns s1[i1:j1] concatenated with s2[i2:j2]. It is a checked
 *   runtime error to provide an i1 > j1, j1 > strlen(s1), i2 > j2, or 
 *   j2 > strlen(s2). It is an unchecked runtime error to provide an s1 or s2 
 *   that is not null terminated.
 *
 *   s1: a null-terminated array of characters
 *   i1: a nonnegative index of s1. Must be <= j1
 *   j1: a nonnegative index of s1. Must be >= i1 and <= strlen(s1)
 *   s2: a null-terminated array of characters
 *   i2: a nonnegative index of s2. Must be <= j2
 *   j2: a nonnegative index of s2. Must be >= i2 and <= strlen(s2)
 * 
 *   returns: pointer to the first byte of the malloc'ed string
 */
char *Str_cat(const char *s1, size_t i1, size_t j1,
              const char *s2, size_t i2, size_t j2) 
{
    char *str, *p;
    assert(i1 <= j1);
    assert(i2 <= j2);
    p = str = ALLOC(j1 - i1 + j2 - i2 + 1);
    while (i1 < j1)
        *p++ = s1[i1++];
    while (i2 < j2)
        *p++ = s2[i2++];
    *p = '\0';
    return str;
}

/*
 * Function: Str_cat_replace
 * ----------------------------
 *   Returns s2 if s1 is null or s1 concatenated with s2 otherwise and
 *   releases memory allocated to s1. It is a checked runtime error to 
 *   provide an s2 with strlen(s2) = 0. It is an unchecked runtime error to 
 *   provide an s1 that was is not on the heap, provide an s1 that is not
 *   null terminated, or provide an s2 that is not null terminated.
 *
 *   s1: a heap-allocated null-terminated array of characters or NULL
 *   s2: a null-terminated array of characters
 *   s1_len: length of s1
 *   s2_len: length of s2
 * 
 *   returns: pointer to the first byte of the malloc'ed string
 */
char *Str_cat_replace(char **s1, char *s2, size_t s1_len, size_t s2_len)
{
    char *str;
    char *dummy;
    dummy = *s1;
    if (dummy)
    {
        str = Str_cat(dummy, 0, s1_len, s2, 0, s2_len);
        FREE(dummy);
    }
    else
    {
        str = Str_cat("", 0, 0, s2, 0, s2_len);
    }
    return str;
}

/*
 * Function: Str_len
 * ----------------------------
 *   Returns the length of s[i:j]. It is a checked runtime error to provide an 
 *   i > j or j > strlen(s). It is an unchecked runtime error to provide an s 
 *   that is not null terminated.
 *
 *   s: a null-terminated array of characters
 *   i: a nonnegative index of s. Must be <= j
 *   j: a nonnegative index of s. Must be >= i and <= strlen(s)
 * 
 *   returns: length of the substring s[i:j]
 */
size_t Str_len(const char *s, size_t i, size_t j) 
{
    assert(i <= j && j <= strlen(s));
    return j - i;
}

/*
 * Function: Str_cmp
 * ----------------------------
 *   Compares the s1[i1:j1] to s2[i2:j2]. It is a checked runtime error to 
 *   provide an i1 > j1, j1 > strlen(s1), i2 > j2, or j2 > strlen(s2). It is an 
 *   unchecked runtime error to provide an s1 or s2 that is not null terminated.
 *
 *   s1: a null-terminated array of characters
 *   i1: a nonnegative index of s1. Must be <= j1
 *   j1: a nonnegative index of s1. Must be >= i1 and <= strlen(s1)
 *   s2: a null-terminated array of characters
 *   i2: a nonnegative index of s2. Must be <= j2
 *   j2: a nonnegative index of s2. Must be >= i2 and <= strlen(s2)
 * 
 *   returns: an integer < 0 if s1[i1:j1] < s2[i2:j2]
 *                       = 0 if s1[i1:j1] = s2[i2:j2]
 *                       > 0 if s1[i1:j1] > s2[i2:j2]
 */
int Str_cmp(const char *s1, size_t i1, size_t j1,
            const char *s2, size_t i2, size_t j2) 
{
    assert(i1 <= j1 && j1 <= strlen(s1));
    assert(i2 <= j2 && j2 <= strlen(s2));
    s1 += i1;
    s2 += i2;
    if (j1 - i1 < j2 - i2) {
        int cond = strncmp(s1, s2, j1 - i1);
        return cond == 0 ? -1 : cond;
    } else if (j1 - i1 > j2 - i2) {
        int cond = strncmp(s1, s2, j2 - i2);
        return cond == 0 ? +1 : cond;
    } else
        return strncmp(s1, s2, j1 - i1);
}

/*
 * Function: Str_chr
 * ----------------------------
 *   Returns the index in s of the leftmost occurence of c in s[i:j], or -1 
 *   otherwise. It is a checked runtime error to provide an i > j or 
 *   j > strlen(s). It is an unchecked runtime error to provided an s that is 
 *   not null terminated.
 *
 *   s: a null-terminated array of characters
 *   i: a nonnegative index of s. Must be <= j
 *   j: a nonnegative index of s. Must be >= i and <= strlen(s)
 *   c: a char to match in s[i:j]
 * 
 *   returns: the index of the leftmost c in s or -1 if it was not found
 */
int Str_chr(const char *s, size_t i, size_t j, char c) 
{
    assert(i <= j && j <= strlen(s));
    for ( ; i < j; i++)
        if (s[i] == c)
            return i;
    return -1;
}

/*
 * Function: Str_find
 * ----------------------------
 *   Returns the index in s of the leftmost occurence of str in s[i:j], or -1
 *   otherwise. It is a checked runtime error to provide an i > j, 
 *   j > strlen(s), or a null str. It is an unchecked 
 *   runtime error to provided an s or str that is not null terminated.
 *
 *   s: a null-terminated array of characters
 *   i: a nonnegative index of s. Must be <= j
 *   j: a nonnegative index of s. Must be >= i and <= strlen(s)
 *   str: a string to match in s[i:j]
 * 
 *   returns: the index to the first character of the leftmost occurrence of str
 *            in s or -1 if it was not found
 */
int Str_find(const char *s, size_t i, size_t j, const char *str) 
{
    size_t len;
    assert(i <= j && j <= strlen(s) && str);
    len = strlen(str);
    if (len == 0)
        return i;
    else if (len == 1) {
        for ( ; i < j; i++)
            if (s[i] == *str)
                return i;
    } else
        for ( ; i + len <= j; i++)
            if ((strncmp(&s[i], str, len) == 0))
                return i;
    return -1;
}

/*
 * Function: Str_any
 * ----------------------------
 *   Returns the index in s of the leftmost occurence in s[i:j] of any of the 
 *   chars in set or -1 otherwise. It is a checked runtime error to provide an 
 *   i > j, j > strlen(s), a null str, or a null s. It is an unchecked 
 *   runtime error to provided an s or str that is not null terminated.
 *
 *   s: a null-terminated array of characters
 *   i: a nonnegative index of s. Must be <= j
 *   j: a nonnegative index of s. Must be >= i and <= strlen(s)
 *   set: a set of characters to match
 * 
 *   returns: the index to the first character of the leftmost occurrence of 
 *            any of the chars in set of -1 if it was not found
 */
int Str_any(const char *s, size_t i, size_t j, const char *set) 
{
	size_t len;
	assert(s);
	assert(set);
    assert(i <= j);
	len = strlen(s);
    assert(j <= len);
    for( ; i < j; i++)
    {
        if (strchr(set, s[i]))
            return i;
    }
	return -1;
}
