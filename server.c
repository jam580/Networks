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
#include <pthread.h>
#include <Python.h>
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
#include "cache.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BUFF_SIZE 10240

typedef struct Chunk{
    size_t len;
    char *chunk;
} *Chunk;

Chunk Chunk_new()
{
    Chunk c;
    NEW(c);
    c->len = 0;
    c->chunk = NULL;
    return c;
}

void Chunk_print(Chunk c)
{
    size_t i;
    for (i = 0; i < c->len; i++)
        printf("%c", *(c->chunk + i));
}

void Chunk_free(Chunk *c)
{
    FREE((*c)->chunk);
    FREE(*c);
    *c = NULL;
}

typedef struct HTTPMessage{
    char *start_line;
    char *start_line_elts[3];
    HeaderFieldsList header;
    HeaderFieldsList trailer;
    char *body;
    List_T chunks;
    size_t content_len;
    size_t body_remaining;
    bool has_full_header;
    bool is_complete;
    char *unprocessed;
    int bytes_unprocessed;
    int type;
} *HTTPMessage;
#define NOT_SPECIFIED 0
#define REQUEST 1
#define RESPONSE 2
void * launchServe()
{
    //temp is python filename
    //method is specific funciton inside of python code we wish to call
    char * temp = "search";
    char * method = "launch";
    //set the python environment
    setenv("PYTHONPATH", ".", 0);

    //must be called before all other Py functions
    Py_Initialize();

    PyObject *pValue;
    PyObject *name = PyUnicode_DecodeFSDefault(temp);
    //attempt to load the python program as a module
    PyObject *pModule = PyImport_Import(name);
    Py_DECREF(name);

    if(pModule!=NULL)
    {
        //load the specific function from the python program
        PyObject *pFunc = PyObject_GetAttrString(pModule, method);
        //check if program is callable and exists
        if(pFunc && PyCallable_Check(pFunc))
        {

            printf("Yeehaw\n");
            //run the function in question, store return value
            pValue = PyObject_CallObject(pFunc,NULL);

            //print out a value if we got it
            if (pValue != NULL) {
                printf("Result of call: %ld\n", PyLong_AsLong(pValue));
                Py_DECREF(pValue);
            }
            //error occured
            else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                PyErr_Print();
                fprintf(stderr,"Call failed\n");
                return NULL;
            }
        } //if function exists and is callable
        else { 
            if (PyErr_Occurred())
                PyErr_Print();
            fprintf(stderr, "Cannot find function launch \n");
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    }//if pmodule != NULL
    else { //failed to find python source code
        PyErr_Print();
        fprintf(stderr, "Failed to load \"%s\"\n", temp);
        return NULL;
    }
    if(Py_FinalizeEx() < 0)
        exit(120);
}


typedef struct Directive{
    char *key;
    char *value;
} Directive;

HTTPMessage HTTPMessage_new()
{
    HTTPMessage msg;
    int i;
    NEW(msg);
    msg->start_line = NULL;
    for (i = 0; i < 3; i++)
        (msg->start_line_elts)[i] = NULL;
    msg->header = HeaderFieldsList_new();
    msg->trailer = HeaderFieldsList_new();
    msg->body = NULL;
    msg->chunks = NULL;
    msg->content_len = 0;
    msg->body_remaining = 0;
    msg->has_full_header = false;
    msg->is_complete = false;
    msg->unprocessed = NULL;
    msg->bytes_unprocessed = 0;
    msg->type = NOT_SPECIFIED;
    return msg;
}

void HTTPMessage_free(HTTPMessage *httpmsg)
{
    HTTPMessage msg = *httpmsg;
    Chunk c;
    int i;
    List_T head;
    if (msg->header != NULL)
        HeaderFieldsList_free(&(msg->header));
    if (msg->start_line != NULL)
        FREE(msg->start_line);
    for (i = 0; i < 3; i++)
        if ((msg->start_line_elts)[i] != NULL)
            FREE((msg->start_line_elts)[i]);
    if (msg->body != NULL)
        FREE(msg->body);
    if (msg->unprocessed != NULL)
        FREE(msg->unprocessed);
    if (msg->chunks != NULL)
    {
        head = msg->chunks;
        for (; head; head = head->rest)
        {
            c = head->first;
            Chunk_free(&c);
        }
        List_free(&(msg->chunks));
    }
    if (msg->trailer != NULL)
        HeaderFieldsList_free(&(msg->trailer));
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
    Table_T sckt_to_msg, Table_T server2client, Table_T client2server)
{
    HTTPMessage msg;
    int *server_stored, *client_stored;

    //printf("Closing connection %d\n", sckt); // TODO remove
    FD_CLR(sckt, master_fds);
    *clients = ClientList_remove(*clients, sckt);
    msg = Table_get(sckt_to_msg, Atom_int(sckt));
    HTTPMessage_free(&msg);
    Table_remove(sckt_to_msg, Atom_int(sckt));
    close(sckt);

    if ((server_stored = Table_remove(client2server, Atom_int(sckt))))
    {
        client_stored = Table_remove(server2client, Atom_int(*server_stored));
        FD_CLR(*server_stored, master_fds);
        close(*server_stored);
        FREE(client_stored);
        FREE(server_stored);
    }
}

bool allowaccess(char * host, char **firewall, int firesz)
{
    int i = 0;
    while(i<firesz && strcmp(firewall[i],""))
    {
        if(!strcmp(host, firewall[i]))
            return false;
        i++;
    }
    return true;
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
    List_T chunks;
    Chunk c;

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
    } else if (msg->chunks != NULL)
    {
        chunks = msg->chunks;
        for (; chunks; chunks = chunks->rest)
        {
            c = chunks->first;
            bytes = write(sckt, c->chunk, c->len);
            if (bytes <= 0)
                return false;
        }
        header = msg->trailer;
        for (; header != NULL; header = header->rest)
        {
            field = header->first;
            bytes = write(sckt, field, strlen(field));
            if (bytes <= 0)
                return false;
        }
        bytes = write(sckt, "\r\n", 2); // new line end of payload
    }
    
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
 * Function: explode_start_line
 * ----------------------------
 *   Split the start line in the given msg into
 *   a) the method, the request-target, and the protocol for requests or 
 *   b) the protocol, code, and phrase for responses.
 * 
 *   Updates the elements in the given message if the explosion was successful.
 *   Note, there is no exffect when providing a request that contains non-null
 *   elements. It is assumed that such a message has already been exploded. It
 *   is a checked runtime error to provide an msg with a NULL start_line.
 * 
 *   More info in RFC 7230, pg. 21.
 * 
 *   Returns false on failure and true otherwise.
 */
bool explode_start_line(HTTPMessage msg)
{
    char *elts[3];
    size_t line_len;
    int i;

    assert(msg->start_line);

    if ((msg->start_line_elts)[0])
        return true;
    
    line_len = strlen(msg->start_line);
    for (i = 0; i < 3; i++)
        elts[i] = calloc(line_len, sizeof(**elts));
    
    if (sscanf(msg->start_line, "%s %s %s", elts[0], elts[1], elts[2]) == 3)
    {
        for (i = 0; i < 3; i++)
            (msg->start_line_elts)[i] = elts[i];
        return true;
    }
    else
    {
        printf("NO ELTS\n");
        for (i = 0; i < 3; i++)
            FREE(elts[i]);
        return false;
    }
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
 * Get the first chunk from body, if possible, and update stored body in msg.
 * Returns chunk_data size
 */
int get_chunk(char **body, int *bytes, HTTPMessage msg)
{
    Chunk c;
    size_t chunk_size_len, chunk_data_bytes;
    char *chunk_size, *end_ptr;

    chunk_size = get_line(body, bytes);
    if (chunk_size != NULL)
    {
        chunk_size_len = strlen(chunk_size);
        chunk_data_bytes = strtoul(chunk_size, &end_ptr, 10);
        if (chunk_data_bytes == 0)
        {
            c = Chunk_new();
            c->chunk = chunk_size;
            c->len = chunk_size_len;
            msg->chunks = List_append(msg->chunks, List_push(NULL, c));
            msg->body_remaining = 0;
        }
        else if ((size_t)*bytes >= chunk_data_bytes + 2)
        {
            c = Chunk_new();
            c->chunk = calloc(chunk_size_len + chunk_data_bytes + 2, 1);
            memcpy(c->chunk, chunk_size, chunk_size_len);
            memcpy(c->chunk + chunk_size_len, *body, chunk_data_bytes + 2);
            c->len = chunk_size_len + chunk_data_bytes + 2;
            *body += chunk_data_bytes + 2;
            *bytes -= (chunk_data_bytes + 2);
            msg->chunks = List_append(msg->chunks, List_push(NULL, c));
            FREE(chunk_size);
        }
        else
            FREE(chunk_size);
        return chunk_data_bytes;
    }

    return 0;
}

/*
 * Function: extract_body_chunked
 * ------------------------------
 *   Extracts up to bytes given of body and stores in msg. Body is assumed to
 *   be chunked. Returns the number of bytes remaining in the given body that
 *   could not be processed.
 * 
 *   msg->body_remaining MUST be non-zero for the body to be processed.
 */
int extract_body_chunked(char *body, int bytes, HTTPMessage msg)
{
    char *body_dup, *line;
    int bytes_dup, bytes_prev, bytes_processed;

    body_dup = body;
    bytes_dup = bytes;
    bytes_processed = 0;
    bytes_prev = bytes_dup;
    while (get_chunk(&body_dup, &bytes_dup, msg) != 0) 
    {
        bytes_processed += (bytes_prev - bytes_dup);
        bytes_prev = bytes_dup;
    }
    bytes_processed += (bytes_prev - bytes_dup);

    if (msg->body_remaining == 0)
    {
        msg->body_remaining = 1;
        bytes_prev = bytes_dup;
        while ((line = get_line(&body_dup, &bytes_dup)) != NULL)
        {
            if (strcmp(line, "\r\n") == 0)
            {
                msg->body_remaining = 0;
                bytes_processed += bytes_prev - bytes_dup;
                FREE(line);
                break;
            }
            else
            {
                msg->trailer = HeaderFieldsList_push(msg->trailer, line);
                bytes_processed += bytes_prev - bytes_dup;
            }
            bytes_prev = bytes_dup;
        }
    }

    return bytes - bytes_processed;
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
 * Determines whether the message associated with the given header contains
 * a chunked body, as specified by the "Transfer-Encoding" header. 
 * (RFC 7230 Section 3.3.1, pg. 28)
 */
bool is_chunked(HeaderFieldsList header)
{
    char *field;
    Directive dirs[5];
    int i, num_dirs;
    bool retval;

    for (i = 0; i < 5; i++)
    {
        dirs[i].key = NULL;
        dirs[i].value = NULL;
    }
    field = HeaderFieldsList_get(header, "Transfer-Encoding");
    if (field != NULL)
    {
        num_dirs = get_directives(field, strlen("transfer-encoding:"), 5, dirs);
        if (num_dirs > 0)
            retval = strcasecmp(dirs[num_dirs-1].key, "chunked") == 0;
        else
            retval = false;
    }
    else
        retval = false;

    for (i = 0; i < 5; i++)
    {
        FREE(dirs[i].key);
        FREE(dirs[i].value);
    }
    return retval;
}

/*
 * Function: header_only
 * ---------------------
 *   Determine whether the given response message only contains a header based
 *   on the response code. Some headers may have body length headers specified
 *   but do not contain a body. It is an checked runtime error to provide
 *   a request message instead of a response message, to provide a response that 
 *   does not contain a startline, or to provide a response with an invalid
 *   start line.
 * 
 *   Codes that MAY contain header fields for body length but do not contain a
 *   body: 1xx, 204, 304 (RFC 7230 pg. 32)
 */
bool header_only(HTTPMessage res)
{
    char *code;
    int code_num;
    assert(res->start_line);
    assert(explode_start_line(res));
    assert(res->type == RESPONSE);
    code = (res->start_line_elts)[1];
    code_num = atoi(code);
    return code_num / 100 == 1 || code_num == 204 || code_num == 304;
}

/*
 * Function: read_sckt
 * -------------------
 *   Reads a request or response from the client at given socket and stores
 *   the message in the given HTTPMessage.
 *   
 *   sckt: the socket to read from
 *   msg: Where to store the read message
 *   no_body: whether the message is expected to not have a body. E.g. a
 *      response to a HEAD req should have no_body set to true.
 *   
 *   Returns false if the read failed and true otherwise;
 */
bool read_sckt(int sckt, HTTPMessage msg, bool no_body)
{
    int bytes, new_bytes;
    char *buf, *buf_dup;
    size_t content_len;
    bool connection_closed;

    assert(msg->type != NOT_SPECIFIED); // critical failure if not specified

    connection_closed = false;
    buf = malloc(BUFF_SIZE);
    if (msg->bytes_unprocessed > 0)
        memcpy(buf, msg->unprocessed, msg->bytes_unprocessed);

    bytes = read(sckt, 
                 buf + msg->bytes_unprocessed, 
                 BUFF_SIZE - msg->bytes_unprocessed);
    if (bytes < 0)
    {
        FREE(buf);
        return false;
    }
    else if(bytes == 0)
        connection_closed = true;
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
        if (msg->type == RESPONSE && !no_body)
            no_body = header_only(msg);
        if (no_body)
        {
            msg->is_complete = true;
            if (bytes > 0)
                return false;
        }
        else if (HeaderFieldsList_get(msg->header, "Transfer-Encoding") != NULL)
        {
            // remove content-length if present to prevent request smuggling
            // RFC 7230, 3.3.3, #3
            msg->header = 
                HeaderFieldsList_remove(msg->header, "Content-Length");
            msg->content_len = 0;
            if (is_chunked(msg->header) && !connection_closed)
            {
                msg->body_remaining = 1;
                new_bytes = extract_body_chunked(buf, bytes, msg);
                if (msg->body_remaining == 0)
                {
                    msg->is_complete = true;
                    if (new_bytes > 0)
                        return false;
                }
                else if (new_bytes > 0)
                {
                    msg->unprocessed = malloc(new_bytes);
                    memcpy(msg->unprocessed, 
                            buf + (bytes - new_bytes),
                            new_bytes);
                    msg->bytes_unprocessed = bytes;
                }
            }
            else if (msg->type == RESPONSE)
            {
                // TODO read until connection is closed
                // just reject the response for now.
                FREE(buf_dup);
                return false;
            }
            else if (msg->type == REQUEST)
            {
                FREE(buf_dup);
                return false;
            }
        }
        else
        {
            if (connection_closed)
            {
                FREE(buf_dup);
                return false;
            }
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
                        return false;
                }
            }
            else
            {
                msg->is_complete = true;
                if (bytes > 0)
                    return false;
            }
        }
    }
    else if (connection_closed)
    {
        FREE(buf_dup);
        return false;
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
        host_start = url;
    else
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
           strcmp(meth, "HEAD") == 0 ||
           strcmp(meth, "POST") == 0 ||
           strcmp(meth, "CONNECT") == 0;
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

    while (!res->is_complete && 
        read_sckt(server, res, strncmp(req->start_line, "HEAD", 4) == 0)) {}

    if (!res->is_complete)
        return false;

    return true;
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
 * Function: should_cache
 * ----------------------
 * Determine if res should be cached and update secs_to_live if applicable
 * 
 * Directives implemented: no-store, public, private, max-age, s-maxage
 * Directives not implemented: must-revalidate, no-cache, no-transform,
 *      proxy-revalidate
 */                         
bool should_cache(HTTPMessage res, int *secs_to_live)
{
    Directive dirs[10];
    int num_dirs, i, code_num;
    char *field, *protocol, *code, *reason;
    bool cacheable, s_max_age_set;

    *secs_to_live = 0;
    field = HeaderFieldsList_get(res->header, "Cache-Control");
    if (field == NULL)
        return true;
    
    for (i = 0; i < 10; i++)
    {
        dirs[i].key = NULL;
        dirs[i].value = NULL;
    }

    cacheable = true;
    s_max_age_set = false;
    num_dirs = get_directives(field, strlen("cache-control:"), 10, dirs);

    if (explode_start_line(res))
    {
        protocol = (res->start_line_elts)[0];
        code = (res->start_line_elts)[1];
        reason = (res->start_line_elts)[2];
        (void)reason; // remove unused variable warning.
        (void)protocol; // remove unused variable warning.
    }
    else
    {
        protocol = NULL;
        code = NULL;
        reason = NULL;
    }

    if (code != NULL)
    {
        code_num = atoi(code);
        if (code_num != 200 && code_num != 203 && code_num != 204 && 
            code_num != 206 && code_num != 300 && code_num != 301 && 
            code_num != 404 && code_num != 405 && code_num != 410 && 
            code_num != 414 && code_num != 501)
            cacheable = false;

        for (i = 0; i < num_dirs; i++)
        {
            if (strcasecmp("no-store", dirs[i].key) == 0)
            {
                cacheable = false;
                break;
            }
            else if (strcasecmp("private", dirs[i].key) == 0)
            {
                cacheable = false;
                break;
            }
            else if (strcasecmp("public", dirs[i].key) == 0)
            {
                cacheable = true;
            }
            else if (strcasecmp("max-age", dirs[i].key) == 0)
            {
                if (!s_max_age_set)
                    *secs_to_live = atoi(dirs[i].value);
            }
            if (strcasecmp("s-maxage", dirs[i].key) == 0)
            {
                s_max_age_set = true;
                *secs_to_live = atoi(dirs[i].value);
            }
        }
    }
    else
        cacheable = false;

    for (i = 0; i < 10; i++)
    {
        FREE(dirs[i].key);
        FREE(dirs[i].value);
    }
    return cacheable;
}

/*
 * Function: tunnel
 * ----------------
 *   Forwards the req to the appropriate server and stores response in res.
 * 
 *   Returns file descriptor for server if successful and -1 otherwise.
 */
bool tunnel(int from, int to)
{
    int read_bytes, write_bytes;
    char *buf;
    bool ret_val;
    
    ret_val = true;
    buf = malloc(BUFF_SIZE);
    read_bytes = read(from, buf, BUFF_SIZE);
    if (read_bytes <= 0)
        ret_val = false;
    else
    {
        write_bytes = write(to, buf, read_bytes);
        if (write_bytes <= 0)
            ret_val = false;
    }

    FREE(buf);
    return ret_val;
}

/*
 * Function: process_req
 * ---------------------
 *   Forwards the req to the appropriate server and stores response in res.
 * 
 *   Returns file descriptor for server if successful and -1 otherwise.
 */
int process_req(HTTPMessage req, int client, ClientList *clients, Cache_T cache,
    bool *https, char **firewall, int firewallsz)
{
    int ret_val;
    char *method, *url, *protocol;
    char *host, *port, *path;
    size_t url_len;
    int server_fd;
    HTTPMessage res;
    int secs_to_live, age;
    char *age_hdr;
    bool res_cached, forbidden;

    ret_val = -1;
    if (explode_start_line(req) && 
        method_is_supported((req->start_line_elts)[0]))
    {
        method = (req->start_line_elts)[0];
        url = (req->start_line_elts)[1];
        protocol = (req->start_line_elts)[2];
        url_len = strlen(url);
        host = calloc(url_len, sizeof(*host));
        port = calloc(url_len, sizeof(*port));
        path = calloc(url_len, sizeof(*path));
        explode_url(url, host, port, path);

        forbidden = false;
        res_cached = false;
        if (host[0] != '\0' && allowaccess(host, firewall, firewallsz))
        {
            if (port[0] == '\0')
                strcpy(port, "80");

            server_fd = build_server_conn(host, port);
            ret_val = server_fd;
            res = NULL;
            if (server_fd >= 0 && (strcmp(method, "GET") == 0 ||
                                   strcmp(method, "HEAD") == 0 ||
                                   strcmp(method, "POST") == 0))
            {
                *https = false;
                process_connection_header(req, protocol, client, clients);
                if (strcmp(method, "GET") == 0)
                    res = Cache_get(cache, url, &age);
                else
                    res = NULL;
                
                if (res == NULL)
                {
                    res = HTTPMessage_new();
                    res->type = RESPONSE;
                    if (!get_res(req, res, server_fd))
                    {
                        close(server_fd);
                        ret_val = -1;
                    }
                    else
                    {
                        secs_to_live = 0;
                        if (should_cache(res, &secs_to_live) &&
                            strcmp(method, "GET") == 0 && res->body)
                        {
                            res_cached = true;
                            Cache_put(cache, url, res, secs_to_live);
                            Cache_write_out(cache);
                        }
                    }
                }
                else
                {
                    res_cached = true;
                    age_hdr = calloc(18, 1);
                    sprintf(age_hdr, "Age: %d\r\n", age);
                    res->header = HeaderFieldsList_remove(res->header, "Age");
                    res->header = HeaderFieldsList_push(res->header, age_hdr);
                }
            }
            else if (strcmp(method, "CONNECT") == 0)
            {
                *https = true;
                process_connection_header(req, protocol, client, clients);
                res = HTTPMessage_new();

                if (server_fd >= 0)
                   res->start_line = strdup("HTTP/1.1 200 OK\r\n");
                else
                {
                    res->start_line = 
                        strdup("HTTP/1.1 503 Service Unavailable\r\n");
                }
                res->has_full_header = true;
                res->is_complete = true;
            }
            else
                ret_val = -1; 
        }
        else if (host[0] != '\0')
        {
            forbidden = true;
            res = HTTPMessage_new();
            res->type = RESPONSE;
            res->start_line = strdup("HTTP/1.1 403 Forbidden\r\n");
            res->header = HeaderFieldsList_push(res->header, 
                strdup("Connection: close\r\n"));
            res->header = HeaderFieldsList_push(res->header,
                strdup("Content-Length: 82\r\n"));
            res->content_len = 82;
            res->body = strdup("<html><body><h1>403 Forbidden</h1><p>Access blocked by firewall!</p></body></html>");
            res->has_full_header = true;
            res->is_complete = true;
            ClientList_set_keepalive(*clients, client, false, -1);
        }

        if (res && (ret_val != -1 || forbidden))
        {
            res->header = 
                HeaderFieldsList_remove(res->header, "Connection");
            if (ClientList_keepalive(*clients, client))
                res->header = HeaderFieldsList_push(res->header, 
                    strdup("Connection: keep-alive\r\n"));
            else
                res->header = HeaderFieldsList_push(res->header, 
                    strdup("Connection: close\r\n"));

            //printf("Response Header:\n");
            //printf("%s", res->start_line);
            //HeaderFieldsList_print(res->header);
            //printf("\n"); // TODO remove
            
            if (!write_msg(res, client))
            {
                ret_val = -1;
                if (server_fd > 0)
                    close(server_fd);
            }
        }

        if (res && !res_cached)
            HTTPMessage_free(&res);

        FREE(host);
        FREE(port);
        FREE(path);
    }

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
    Table_T sckt_to_msg, server2client, client2server;
    int expected_clients; // expected number of concurrent clients
    int server_fd, *server_fd_store, *https_sckt;
    Cache_T cache;
    bool https;

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

    //set up two dimensional pointers for blocked hostnames
    //for now we will have a cap of 10 blocked hostnames
    int firesz = 10;
    char** firewall = malloc(sizeof(char*)*firesz);

    //initialize values
    for(int i = 0; i<firesz; i++)
        firewall[i] = "";
    char* fire1 = "www.cs.cmu.edu";
    firewall[0] = strdup(fire1);

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE    
    clients = ClientList_new(); // initialize clients list
    child = SocketConn_new(); // initialize empty child connection
    sckt_to_msg = Table_new(expected_clients, NULL, NULL);
    server2client = Table_new(expected_clients, NULL, NULL);
    client2server = Table_new(expected_clients, NULL, NULL);
    cache = Cache_new(expected_clients);

    int initsearch = 0;
    pthread_t id;
    while(true)
    {
        if(initsearch==0)
        {
            pthread_create(&id, NULL, launchServe, NULL);
            printf("Launching search engine\n");
            initsearch=1;
        }
        for (sckt = 0; sckt <= fdmax; sckt++) 
            if (!ClientList_keepalive(clients, sckt))
                close_client(&clients, &master_fds, sckt, sckt_to_msg, 
                    server2client, client2server);

        read_fds = master_fds;

        //printf("Waiting for socket to be ready\n"); // TODO remove
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
                    //printf("Accepted Connection %d\n", child->fd); // TODO remove
                }
                else
                {
                    fprintf(stderr, "ERROR on accept\n");
                }
            }
            else // Data arriving from already connected socket
            {
                https_sckt = Table_get(server2client, Atom_int(sckt));
                if (https_sckt)
                {
                    // route from server to client
                    //printf("Tunneling from %d to %d\n", sckt, *https_sckt); // TODO remove
                    if (!tunnel(sckt, *https_sckt))
                    {
                        close_client(&clients, &master_fds, *https_sckt, 
                            sckt_to_msg, server2client, client2server);
                    }
                }
                else if ((https_sckt = Table_get(client2server, Atom_int(sckt))))
                {
                    // route from client to server
                    //printf("Tunneling from %d to %d\n", sckt, *https_sckt); // TODO remove
                    if (!tunnel(sckt, *https_sckt))
                    {
                        close_client(&clients, &master_fds, sckt, 
                            sckt_to_msg, server2client, client2server);
                    }
                }
                else
                {
                    // process as regular request
                    req = Table_get(sckt_to_msg, Atom_int(sckt));
                    if (!req)
                    {
                        req = HTTPMessage_new();
                        Table_put(sckt_to_msg, Atom_int(sckt), req);
                    }
                    req->type = REQUEST;
                    if (!read_sckt(sckt, req, false))
                    {
                        res = HTTPMessage_new();
                        res->start_line = 
                            strdup("HTTP/1.1 400 Bad Request\r\n");
                        res->header = HeaderFieldsList_push(res->header,
                            strdup("Connection: close\r\n"));
                        res->has_full_header = true;
                        res->is_complete = true;
                        write_msg(res, sckt);
                        HTTPMessage_free(&res);
                        close_client(&clients, &master_fds, sckt, sckt_to_msg,
                            server2client, client2server);
                        continue;
                    }
                    else if (req->is_complete)
                    {
                        //printf("\nRequest Header\n"); 
                        //printf("%s", req->start_line);
                        //HeaderFieldsList_print(req->header);
                        //printf("\n"); // TODO remove
                        server_fd = process_req(req, sckt, &clients, cache, 
                            &https, firewall, firesz);
                        if(server_fd <= 0)
                        {
                            close_client(&clients, &master_fds, sckt, 
                                sckt_to_msg, server2client, client2server);
                            continue;
                        }
                        else
                        {
                            //printf("Request processed\n"); // TODO remove
                            Table_put(sckt_to_msg, Atom_int(sckt), 
                                    HTTPMessage_new());
                            HTTPMessage_free(&req);
                            if (https)
                            {
                                NEW0(server_fd_store);
                                memcpy(server_fd_store, &sckt, 
                                    sizeof(*server_fd_store));
                                server_fd_store = Table_put(server2client, 
                                    Atom_int(server_fd), server_fd_store);
                                FREE(server_fd_store);

                                NEW0(server_fd_store);
                                memcpy(server_fd_store, &server_fd, 
                                    sizeof(*server_fd_store));
                                server_fd_store = Table_put(client2server,
                                    Atom_int(sckt), server_fd_store);
                                FREE(server_fd_store);
                                FD_SET(server_fd, &master_fds);
                                fdmax = MAX(server_fd, fdmax);
                                //printf("Added server connection %d\n", server_fd); // TODO remove
                            }
                            else
                            {
                                close(server_fd);
                            }
                        }
                    }
                    if (!ClientList_keepalive(clients, sckt))
                        close_client(&clients, &master_fds, sckt, sckt_to_msg,
                            server2client, client2server);
                }
            }
        }
    }

    for (sckt = 0; sckt <= fdmax; sckt++)
    {
        req = Table_get(sckt_to_msg, Atom_int(sckt));
        if (req)
            HTTPMessage_free(&req);
    }

    Cache_free(&cache);
    Table_free(&sckt_to_msg);
    ClientList_free(&clients);
    SocketConn_close(parent);
    SocketConn_free(&parent);
    SocketConn_free(&child);
    pthread_cancel(id);
    return EXIT_SUCCESS;
}
