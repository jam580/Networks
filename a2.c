/*******************************************************************************
*
* Tufts University
* Comp112 A2 - Chat Application
* By: Ramon Fernandes
* Last Edited: October 18, 2020
*                               
* server.c - main program processor for a chat application server
* Command Arguments:
*      port: port to listen to for incoming connections
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>
#include <signal.h>

#define MAXMSG 512
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void check_usage(int argc, const char* argv[]);
void exit_failure(char* msg);

typedef struct SocketConn
{
    int fd; // socket
    struct sockaddr_in addr; // socket connection info
    socklen_t len; // addr length
    int portno; // port number to connect to
} SocketConn;
bool SocketConn_open(SocketConn *sc, int portno, unsigned int s_addr);
bool SocketConn_bind(SocketConn *sc);
bool SocketConn_listen(SocketConn *sc, int n);
bool SocketConn_connect(SocketConn *s);
bool SocketConn_accept(SocketConn *server, SocketConn *client);
void SocketConn_close(SocketConn *sc);
ssize_t Socket_write(int socket, const char *s, size_t n);

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

typedef struct List_T *List_T;
struct List_T {
	List_T rest;
	void *first;
};
List_T List_push   (List_T list, void *x);
List_T List_append (List_T list, List_T tail);
List_T List_copy   (List_T list);
List_T List_pop    (List_T list, void **x);
List_T List_reverse(List_T list);
int    List_length (List_T list);
void   List_free   (List_T *list);
void   List_map    (List_T list, void apply(void **x, void *cl), void *cl);
void **List_toArray(List_T list, void *end);

#define HELLO 1
#define HELLO_ACK 2
#define LIST_REQUEST 3
#define CLIENT_LIST 4
#define CHAT 5
#define EXIT 6
#define ERROR_CLIENT_ALREADY_PRESENT 7
#define ERROR_CANNOT_DELIVER 8
#define MAXDATASIZE 400
#define ClientList List_T
#define MessagesList List_T
#define MessagesByClient List_T
#define SERVERID "Server"
#define SECONDSTOLIVE 60
typedef struct __attribute__((__packed__)) Header {
    unsigned short type;
    char source[20];
    char dest[20];
    unsigned int length;
    unsigned int message_id;
} Header;
typedef struct __attribute__((__packed__)) Message {
    Header *hdr;
    char data[MAXDATASIZE];
} Message;
typedef struct __attribute__((__packed__)) MessageContainer {
    size_t bytes;
    time_t last_recvd;
    Message *msg;
} *MessageContainer;
typedef struct __attribute__((__packed__)) MessagesCollection {
    int socket;
    size_t bytes;
    MessagesList containers;
} *MessagesCollection;
typedef struct ClientInfo {
    char id[20];
    int socket;
} *ClientInfo;
Header *Header_empty();
Header *Header_new(short type, char src[20], char dest[20], int len, int id);
Header *Header_hello(char src[20]);
bool Header_hello_check(Header *hdr);
Header *Header_hello_ack(char dest[20]);
bool Header_hello_ack_check(Header *hdr);
Header *Header_list_request(char src[20]);
bool Header_list_request_check(Header *hdr);
Header *Header_client_list(char dest[20], int len);
bool Header_client_list_check(Header *hdr);
Header *Header_chat(char src[20], char dest[20], int len, int id);
bool Header_chat_check(Header *hdr);
Header *Header_exit(char src[20]);
bool Header_exit_check(Header *hdr);
Header *Header_error_cap(char dest[20]);
bool Header_error_cap_check(Header *hdr);
Header *Header_error_cd(char dest[20], int id);
bool Header_error_cd_check(Header *hdr);
Header *Header_hton(Header *hdr);
Header *Header_ntoh(Header *hdr);
void Header_print(Header *hdr);
void Header_free(Header **hdr);

ClientInfo ClientInfo_new(int socket, char *id);
void ClientInfo_free(ClientInfo *ci);
ClientList ClientList_new();
ClientInfo ClientList_get(ClientList clients, int socket);
ClientInfo ClientList_get_by_id(ClientList clients, char *id);
bool ClientList_exists(ClientList clients, char *id);
ClientList ClientList_push(ClientList clients, int socket, char *id);
ClientList ClientList_remove(ClientList clients, int socket);
size_t ClientList_tostr(ClientList clients, char *ids);
int ClientList_length(ClientList clients);
void ClientList_free(ClientList *clients);

Message *Message_new();
Message *Message_set_hdr(Message *m, Header *hdr);
size_t Message_append(Message *m, char *buf, size_t bytes, size_t curr_bytes);
bool Message_iscomplete(Message *m, size_t curr_bytes);
void Message_free(Message **m);
MessageContainer MessageContainer_new();
size_t MessageContainer_append(MessageContainer mc, char *buf, 
                                         size_t bytes);
bool MessageContainer_isfull(MessageContainer mc);
bool MessageContainer_isold(MessageContainer mc, int seconds);
void MessageContainer_free(MessageContainer *mc);
MessagesList MessagesList_new();
MessagesList MessagesList_push(MessagesList ml, char *buf, size_t bytes);
MessagesList MessagesList_pop(MessagesList ml);
void MessagesList_free(MessagesList *ml);
MessagesCollection MessagesCollection_new(int socket);
MessagesCollection MessagesCollection_push(MessagesCollection mc, char *buf, 
                                           size_t bytes);
MessagesCollection MessagesCollection_pop(MessagesCollection mc);
bool MessagesCollection_hasold(MessagesCollection mc, int seconds);
void MessagesCollection_free(MessagesCollection *mc);
MessagesByClient MessagesByClient_new();
MessagesCollection MessagesByClient_get(MessagesByClient mbc, int socket);
MessagesByClient MessagesByClient_push(MessagesByClient mbc, 
                                       MessagesCollection mc);
MessagesByClient MessagesByClient_remove(MessagesByClient mbc,
                                         int socket);
void MessagesByClient_free(MessagesByClient *mbc);

int ChatApp_read(int socket, MessagesByClient *msgs);
int ChatApp_write(int socket, MessageContainer mc);
int ChatApp_process(int socket, ClientList *clients, MessagesByClient *msgs);
int ChatApp_purgeold(ClientList *clients, MessagesByClient *msgs, fd_set *fds,
                     int seconds);
void ChatApp_close(int socket, ClientList *clients, MessagesByClient *msgs,
                   fd_set *fds);

/*******************************************************************************
 * Main program handling functions.
 ******************************************************************************/
int main(int argc, const char *argv[])
{
    SocketConn self;
    SocketConn client;
    fd_set master_fds;
    fd_set read_fds;
    ClientList clients;
    MessagesByClient messages;
    int socket; // looping variable
    int read_result, process_result;

    check_usage(argc, argv);
    self.portno = atoi(argv[1]);

    if (!SocketConn_open(&self, self.portno, INADDR_ANY))
        exit_failure("ERROR opening socket\n");
    if (!SocketConn_bind(&self))
        exit_failure("ERROR binding to server port\n");
    if (!SocketConn_listen(&self, 5))
        exit_failure("ERROR listening to server port\n");

    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(self.fd, &master_fds);
    int fdmax = self.fd;
    clients = ClientList_new();
    messages = MessagesByClient_new();

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE

    while (true) 
    {
        read_fds = master_fds;

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0)
            exit_failure("ERROR on select\n");

        ChatApp_purgeold(&clients, &messages, &master_fds, SECONDSTOLIVE);

        for (socket = 0; socket <= fdmax; socket++) 
        {
            if (!FD_ISSET(socket, &read_fds))
                continue;
            
            if (socket == self.fd) 
            {
                /* Incoming connection request */
                if (SocketConn_accept(&self, &client))
                {
                    FD_SET(client.fd, &master_fds);
                    fdmax = MAX(client.fd, fdmax);
                }
                else
                    fprintf(stderr, "ERROR accepting client\n");
            } 
            else 
            {
                /* Data arriving from already connected socket */
                read_result = ChatApp_read(socket, &messages);
                process_result = 0;
                if (read_result == 0)
                {
                    process_result = ChatApp_process(socket, &clients, 
                                                     &messages);
                }

                if (read_result < 0 || process_result < 0)
                    ChatApp_close(socket, &clients, &messages, &master_fds);
                else if (process_result == ERROR_CLIENT_ALREADY_PRESENT)
                    ChatApp_close(socket, &clients, &messages, &master_fds);
                else if (process_result == EXIT)
                    ChatApp_close(socket, &clients, &messages, &master_fds);
            }
        }
    }

    SocketConn_close(&self);
    ClientList_free(&clients);
    MessagesByClient_free(&messages);
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

/*******************************************************************************
 * Functions for Chat Application
 ******************************************************************************/
/*******************************************************************************
 *          Header
 ******************************************************************************/

/* Returns a pointer to an empty Header */
Header *Header_empty()
{
    Header *hdr;
    NEW(hdr);
    memset(hdr, '\0', sizeof(*hdr));
    return hdr;
}

/* Retuns a pointer to a Header containing the specified information. */
Header *Header_new(short type, char src[20], char dest[20], int len, int id)
{
    Header *hdr;
    NEW(hdr);
    hdr->type = type;
    memcpy(hdr->source, src, 20);
    memcpy(hdr->dest, dest, 20);
    hdr->length = len;
    hdr->message_id = id;
    return hdr;
}

/* Retuns a pointer to a HELLO Header. */
Header *Header_hello(char src[20])
{
    return Header_new(HELLO, src, SERVERID, 0, 0);
}

bool Header_hello_check(Header *hdr)
{
    bool source_has_null;
    int i;
    source_has_null = false;
    
    for (i = 0; i < 20; i++)
    {
        if (hdr->source[i] == '\0')
            source_has_null = true;
    }

    return hdr->type == HELLO && strcmp(hdr->source, SERVERID) != 0 && 
           strcmp(hdr->dest, SERVERID) == 0 && hdr->length == 0 && 
           hdr->message_id == 0 && source_has_null;
}

/* Retuns a pointer to a HELLO_ACK Header. */
Header *Header_hello_ack(char dest[20])
{
    return Header_new(HELLO_ACK, SERVERID, dest, 0, 0);
}

bool Header_hello_ack_check(Header *hdr)
{
    return hdr->type == HELLO_ACK && strcmp(hdr->source, SERVERID) == 0 && 
           hdr->length == 0 && hdr->message_id == 0;
}

/* Retuns a pointer to a LIST_REQUEST Header. */
Header *Header_list_request(char src[20])
{
    return Header_new(LIST_REQUEST, src, SERVERID, 0, 0);
}

bool Header_list_request_check(Header *hdr)
{
    return hdr->type == LIST_REQUEST && strcmp(hdr->dest, SERVERID) == 0 && 
           hdr->length == 0 && hdr->message_id == 0;
}

/* Retuns a pointer to a CLIENT_LIST Header. */
Header *Header_client_list(char dest[20], int len)
{
    return Header_new(CLIENT_LIST, SERVERID, dest, len, 0);
}

bool Header_client_list_check(Header *hdr)
{
    return hdr->type == CLIENT_LIST && strcmp(hdr->source, SERVERID) == 0 &&
           hdr->length <= MAXDATASIZE && hdr->message_id == 0;
}

/* Retuns a pointer to a CHAT Header. */
Header *Header_chat(char src[20], char dest[20], int len, int id)
{
    return Header_new(CHAT, src, dest, len, id);
}

bool Header_chat_check(Header *hdr)
{
    return hdr->type == CHAT && hdr->length <= MAXDATASIZE && 
           hdr->message_id >= 1;
}

/* Retuns a pointer to an EXIT Header. */
Header *Header_exit(char src[20])
{
    return Header_new(EXIT, src, SERVERID, 0, 0);
}

bool Header_exit_check(Header *hdr)
{
    return hdr->type == EXIT && strcmp(hdr->dest, SERVERID) == 0 && 
           hdr->length == 0 && hdr->message_id == 0;
}

/* Retuns a pointer to an ERROR(CLIENT_ALREADY_PRESENT) Header. */
Header *Header_error_cap(char dest[20])
{
    return Header_new(ERROR_CLIENT_ALREADY_PRESENT, SERVERID, dest, 0, 0);
}

bool Header_error_cap_check(Header *hdr)
{
    return hdr->type == ERROR_CLIENT_ALREADY_PRESENT && 
           strcmp(hdr->source, SERVERID) == 0 && hdr->length == 0 && 
           hdr->message_id == 0;
}

/* Retuns a pointer to an ERROR(CANNOT_DELIVER) Header. */
Header *Header_error_cd(char dest[20], int id)
{
    return Header_new(ERROR_CANNOT_DELIVER, SERVERID, dest, 0, id);
}

bool Header_error_cd_check(Header *hdr)
{
    return hdr->type == ERROR_CANNOT_DELIVER && 
           strcmp(hdr->source, SERVERID) == 0 && hdr->length == 0 && 
           hdr->message_id >= 1;
}

/* Converts the appropriate fields in Header from host to network byte order.
 * Occurs in place and returns hdr. */
Header *Header_hton(Header *hdr)
{
    hdr->type = htons(hdr->type);
    hdr->length = htonl(hdr->length);
    hdr->message_id = htonl(hdr->message_id);
    return hdr;
}

/* Converts the appropriate fields in Header from network to host byte order.
 * Occurs in place and returns hdr. */
Header *Header_ntoh(Header *hdr)
{
    hdr->type = ntohs(hdr->type);
    hdr->length = ntohl(hdr->length);
    hdr->message_id = ntohl(hdr->message_id);
    return hdr;
}

/* Print the Header to stdout */
void Header_print(Header *hdr)
{
    printf("Type: %hu\n", hdr->type);
    printf("Source: %s\n", hdr->source);
    printf("Destination: %s\n", hdr->dest);
    printf("Length: %u\n", hdr->length);
    printf("Message_ID: %u\n", hdr->message_id);
}

/* Deallocates memory for Header pointed to by hdr. */
void Header_free(Header **hdr)
{
    FREE(*hdr);
}

/*******************************************************************************
 *          ClientList
 ******************************************************************************/

/* Returns a new Client info for given socket and id */
ClientInfo ClientInfo_new(int socket, char *id)
{
    ClientInfo ci;
    NEW(ci);
    memcpy(ci->id, id, 20);
    ci->socket = socket;
    return ci;
}

void ClientInfo_free(ClientInfo *ci)
{
    FREE(*ci);
    ci = NULL;
}

/* Returns a new client list */
ClientList ClientList_new()
{
    return NULL;
}

/* Returns the client information for socket or NULL if it is not present */
ClientInfo ClientList_get(ClientList clients, int socket)
{
    ClientList head;
    ClientInfo ci;
    head = clients;
    while (head != NULL)
    {
        ci = (ClientInfo)head->first;
        if (ci->socket == socket)
            return ci;
        head = head->rest;
    }
    return NULL;
}

ClientInfo ClientList_get_by_id(ClientList clients, char *id)
{
    ClientList head;
    ClientInfo ci;

    head = clients;
    while (head != NULL)
    {
        ci = head->first;
        if (strcmp(ci->id, id) == 0)
            return ci;
        head = head->rest;
    }
    return NULL;
}

/* Determines wheter clientid is already in list */
bool ClientList_exists(ClientList clients, char *id)
{
    ClientList head;
    ClientInfo ci;
    bool exists;

    head = clients;
    exists = false;
    while (head != NULL && !exists)
    {
        ci = head->first;
        if (strcmp(ci->id, id) == 0)
        {
            exists = true;
            break;
        }
        head = head->rest;
    }
    return exists;
}

ClientList ClientList_push(ClientList clients, int socket, char *id)
{
    ClientInfo ci = ClientInfo_new(socket, id);
    return List_append(clients, List_push(NULL, ci));
}

/* Removes the client information for socket from clients if it is present */
ClientList ClientList_remove(ClientList clients, int socket)
{
    ClientList head, new_clients, prev;
    ClientInfo ci;
    head = clients;
    new_clients = clients;
    prev = NULL;

    while (head != NULL)
    {
        ci = head->first;
        if (ci->socket == socket)
        {
            if (prev == NULL)
                new_clients = head->rest;
            else
                prev->rest = head->rest;
            head->rest = NULL;
            ClientList_free(&head);
            break;
        }
        prev = head;
        head = head->rest;
    }

    return new_clients;
}

/* 
 * Stores the null-terminated client ids currently connected in ids.
 * ids should be long enough to hold ClientList_length(clients) * 20 bytes.
 * Returns total number of bytes changed in ids.
 */
size_t ClientList_tostr(ClientList clients, char *ids)
{
    ClientList head;
    ClientInfo ci;
    size_t id_len, total_len;

    head = clients;
    total_len = 0;
    while (head != NULL)
    {
        ci = head->first;
        id_len = strlen(ci->id) + 1;
        memcpy(ids + total_len, ci->id, id_len);
        total_len += id_len;
        head = head->rest;
    }
    return total_len;
}

/* Returns number of clients connected */
int ClientList_length(ClientList clients)
{
    return List_length(clients);
}

/* Deallocates memory associated with ClientList pointed to by clients */
void ClientList_free(ClientList *clients)
{
    ClientList head;
    ClientInfo ci;
    head = *clients;
    while (head != NULL)
    {
        ci = head->first;
        ClientInfo_free(&ci);
        head = head->rest;
    }
    List_free(clients);
}

/*******************************************************************************
 *           Message, MessageContainer, MessagesList, MessageCollection, 
 *           MessagesByClientList
 ******************************************************************************/

/* Returns a new Message with empty data */
Message *Message_new()
{
    Message *m;
    NEW(m);
    m->hdr = Header_empty();
    memset(m->data, '\0', MAXDATASIZE);
    return m;
}

/* Updates the header in message to the given header */
Message *Message_set_hdr(Message *m, Header *hdr)
{
    Header *old;
    old = m->hdr;
    Header_free(&old);
    m->hdr = hdr;
    return m;
}

/* Appends buf to message m starting at curr_bytes. Returns the number of bytes
 * actually appended. The return value may be less than bytes when the message
 * is full or we have already received the full message. Expects any part of the
 * Header in buf to be in network byte order.
 */
size_t Message_append(Message *m, char *buf, size_t bytes, size_t curr_bytes)
{
    Header *hdr;
    size_t max_bytes_to_copy;
    size_t rem_hdr_bytes;
    size_t hdr_len;

    hdr = m->hdr;
    hdr_len = sizeof(*hdr);

    if (curr_bytes == hdr_len + MAXDATASIZE || bytes == 0)
        return 0;
    
    rem_hdr_bytes = curr_bytes >= hdr_len ? 0 : hdr_len - curr_bytes;
    if (rem_hdr_bytes == 0)
    {
        max_bytes_to_copy = MIN((hdr_len + hdr->length) - curr_bytes, bytes);
        if (max_bytes_to_copy == 0)
            return 0;
        memcpy(m->data + (curr_bytes - hdr_len), buf, max_bytes_to_copy);
        return max_bytes_to_copy;
    }
    else
    {
        if (bytes >= rem_hdr_bytes)
        {
            memcpy((char*)(m->hdr) + curr_bytes, buf, rem_hdr_bytes);
            m->hdr = Header_ntoh(m->hdr);
            max_bytes_to_copy = 
                MIN((hdr_len + hdr->length - curr_bytes - rem_hdr_bytes), 
                    bytes - rem_hdr_bytes);
            memcpy(m->data, buf + rem_hdr_bytes, max_bytes_to_copy);
            return max_bytes_to_copy + rem_hdr_bytes;
        }
        else
        {
            memcpy((char*)(m->hdr) + curr_bytes, buf, bytes);
            return bytes;
        }
    }
}

/* Checks if Message is complete */
bool Message_iscomplete(Message *m, size_t curr_bytes)
{
    size_t hdr_len = sizeof(*(m->hdr));
    return curr_bytes >= hdr_len && curr_bytes == hdr_len + m->hdr->length;
}

/* Deallocates memory associated with Message pointed to by m */
void Message_free(Message **m)
{
    Header *hdr = (*m)->hdr;
    Header_free(&hdr);
    FREE(*m);
    m = NULL;
}

/* Returns a new MessageContainer with empty data */
MessageContainer MessageContainer_new()
{
    MessageContainer mc;
    NEW(mc);
    mc->bytes = 0;
    mc->last_recvd = time(NULL);
    mc->msg = Message_new();
    return mc;
}

/* Appends buf to the mc. Returns the number of bytes succesfuly added.
 * mc must be non-null
 */
size_t MessageContainer_append(MessageContainer mc, char *buf, 
                                         size_t bytes)
{
    Message *m;
    int bytes_copied;
    if (mc == NULL)
        return 0;
    m = mc->msg;
    mc->last_recvd = time(NULL);
    bytes_copied = Message_append(m, buf, bytes, mc->bytes);
    mc->bytes += bytes_copied;
    return bytes_copied;
}

/* Checks if the given message container is full */
bool MessageContainer_isfull(MessageContainer mc)
{
    return Message_iscomplete(mc->msg, mc->bytes);
}

/* Checks if given message container is older than given seconds */
bool MessageContainer_isold(MessageContainer mc, int seconds)
{
    time_t curr_time = time(NULL);
    if (mc->last_recvd + seconds <= curr_time)
        return true;
    return false;
}

/* Deallocates memory associated with MessageContainer pointed to by mc */
void MessageContainer_free(MessageContainer *mc)
{
    Message *m = (*mc)->msg;
    Message_free(&m);
    FREE(*mc);
    mc = NULL;
}

/* Returns an empty message list */
MessagesList MessagesList_new()
{
    return NULL;
}

MessagesList MessagesList_push(MessagesList ml, char *buf, size_t bytes)
{
    MessagesList ml_node;
    MessageContainer container;
    size_t bytes_copied;

    ml_node = ml;
    bytes_copied = 0;
    while (bytes_copied < bytes)
    {
        while (ml_node != NULL && MessageContainer_isfull(ml_node->first))
            ml_node = ml_node->rest;
            
        if (ml_node == NULL)
            container = MessageContainer_new();
        else
            container = ml_node->first;

        bytes_copied += MessageContainer_append(container, 
                            buf+bytes_copied, bytes - bytes_copied);
        if (ml_node == NULL)
            ml = List_append(ml, List_push(NULL, container));
    }

    return ml;
}

MessagesList MessagesList_pop(MessagesList ml)
{
    MessagesList head, rest;
    head = ml;
    rest = head->rest;
    head->rest = NULL;
    MessagesList_free(&head);
    return rest;
}

bool MessagesList_hasold(MessagesList ml, int seconds)
{
    MessagesList head;
    MessageContainer mc;

    head = ml;
    while (head != NULL)
    {
        mc = head->first;
        if (MessageContainer_isold(mc, seconds))
            return true;
        head = head->rest;
    }
    return false;
}

/* Deallocates memory associated with MessagesList pointed to by ml */
void MessagesList_free(MessagesList *ml)
{
    MessagesList head;
    MessageContainer container;
    head = *ml;
    while (head != NULL)
    {
        container = head->first;
        MessageContainer_free(&container);
        head = head->rest;
    }
    List_free(ml);
}

/* Retuns a message collection for the socket */
MessagesCollection MessagesCollection_new(int socket)
{
    MessagesCollection mc;
    NEW(mc);
    mc->socket = socket;
    mc->bytes = 0;
    mc->containers = MessagesList_new();
    return mc;
}

/* 
 * Pushes the message onto the appropriate location in the collection.
 * Will push onto the last message not yet fully received or will create a new
 * message.
 */
MessagesCollection MessagesCollection_push(MessagesCollection mc, char *buf, 
                                           size_t bytes)
{
    mc->containers = MessagesList_push(mc->containers, buf, bytes);
    mc->bytes += bytes;
    return mc;
}

/* Removes the first message in the collection and returns the collection */
MessagesCollection MessagesCollection_pop(MessagesCollection mc)
{
    MessagesList containers;
    MessageContainer to_pop;

    containers = mc->containers;
    to_pop = containers->first;
    mc->bytes -= to_pop->bytes;
    mc->containers = MessagesList_pop(mc->containers);
    return mc;
}

bool MessagesCollection_hasold(MessagesCollection mc, int seconds)
{
    return MessagesList_hasold(mc->containers, seconds);
}

/* Deallocated memory associated with MessageCollection pointed to by mc */
void MessagesCollection_free(MessagesCollection *mc)
{
    MessagesList msgs = (*mc)->containers;
    MessagesList_free(&msgs);
    FREE(*mc);
    mc = NULL;
}

/* Retuns a new MessagesByClientList */
MessagesByClient MessagesByClient_new()
{
    return NULL;
}

/* Returns the MessagesCollection in mbc for client at socket or NULL if not
 * found. 
 */
MessagesCollection MessagesByClient_get(MessagesByClient mbc, int socket)
{
    MessagesByClient head;
    MessagesCollection mc;
    head = mbc;
    while (head != NULL)
    {
        mc = head->first;
        if (mc->socket == socket)
            return mc;
        head = head->rest;
    }
    return NULL;
}

/* Push mc onto mbc. Returns the new mbc. It is a checked runtime error to
 * provide an mc with for a socket already in mbc
 */
MessagesByClient MessagesByClient_push(MessagesByClient mbc, 
                                       MessagesCollection mc)
{
    MessagesByClient head, last, new_mbc;
    MessagesCollection existing_collection;
    head = mbc;
    last = NULL;
    while (head != NULL)
    {
        existing_collection = head->first;
        assert(existing_collection->socket != mc->socket);
        last = head;
        head = head->rest;
    }
    if (last == NULL)
    {
        new_mbc = List_push(NULL, mc);
        return new_mbc;
    }
    else
    {
        last->rest = List_push(NULL, mc);
        return mbc;
    }
}

/* Removes the MessageCollection for socket from mbc if one exists. */
MessagesByClient MessagesByClient_remove(MessagesByClient mbc,
                                         int socket)
{
    MessagesByClient head, new_mbc, prev;
    MessagesCollection existing_collection;
    head = mbc;
    new_mbc = mbc;
    prev = NULL;
    while (head != NULL)
    {
        existing_collection = head->first;
        if (existing_collection->socket == socket) 
        {
            if (prev == NULL)
                new_mbc = head->rest;
            else
                prev->rest = head->rest;
            head->rest = NULL;
            MessagesByClient_free(&head);
            break;
        }
        prev = head;
        head = head->rest;
    }
    return new_mbc;
}

/* Deallocates memory associated with MessagesByClient pointed to by mbc */
void MessagesByClient_free(MessagesByClient *mbc)
{
    MessagesByClient head;
    MessagesCollection collection;
    head = *mbc;
    while (head != NULL)
    {
        collection = head->first;
        MessagesCollection_free(&collection);
        head = head->rest;
    }
    List_free(mbc);
}

/*******************************************************************************
 *           ChatApp
 ******************************************************************************/

/*
 * Function: ChatApp_read
 * ----------------------
 *   Reads message from client at given socket.
 *   It is an unchecked runtime error to provide a socket not released by
 *   select() as ready with data. It is an unchecked runtime error to read from
 *   master socket.
 * 
 *   socket: the client socket to read
 *   msgs: a list containing the current messages from each client
 * 
 *   Returns an int < 0 if there was an error during read,
 *              int = 0 if there was no error
*/
int ChatApp_read(int socket, MessagesByClient *msgs)
{
    int bytes;
    char buf[MAXMSG];
    MessagesCollection c_messages;
    int fail, succ;

    fail = -1;
    succ = 0;
    c_messages = MessagesByClient_get(*msgs, socket);

    if ((bytes = read(socket, buf, MAXMSG-1)) <= 0)
        return fail;
    if (c_messages == NULL)
    {
        c_messages = MessagesCollection_new(socket);
        c_messages = MessagesCollection_push(c_messages, buf, bytes);
        *msgs = MessagesByClient_push(*msgs, c_messages);
    }
    else
    {
        c_messages = MessagesCollection_push(c_messages, buf, bytes);
    }

    return succ;
}

/*
 * Writes the message in the message containter to the socket. Returns the 
 * total number of bytes written OR -1 if there was an error
 */
int ChatApp_write(int socket, MessageContainer mc)
{
    int bytes;
    size_t sent;
    char buf[sizeof(Header) + MAXDATASIZE];
    Message *m;

    m = mc->msg;
    m->hdr = Header_hton(m->hdr);

    memset(buf, '\0', sizeof(Header) + MAXDATASIZE);
    memcpy(buf, m->hdr, sizeof(Header));
    memcpy(buf + sizeof(Header), m->data, mc->bytes - sizeof(Header));
    m->hdr = Header_ntoh(m->hdr);
    
    sent = 0;
    while (sent < mc->bytes)
    {
        bytes = write(socket, buf + sent, mc->bytes - sent);
        if (bytes < 0)
            return bytes;
        sent += bytes;
    }
        
    return sent;
}

/*
 * Function: ChatApp_process
 * -------------------------
 *   Process the first message from client at given socket.
 *   Reads message from client at given socket.
 *   It is an unchecked runtime error to provide a socket not released by
 *   select() as ready with data. It is an unchecked runtime error to read from
 *   master socket. Validates message format and ordering. Removes processed
 *   message from pending messages.
 * 
 *   socket: the client socket to read
 *   clients: a list of ClientInfo for all clients current connected
 *   msgs: a list containing the current messages from each client
 * 
 *   Returns an int < 0 if there was an error during read,
 *              int = 0 if there was no error and no message processed,
 *              int > 0 for message type processed or error code
*/
int ChatApp_process(int socket, ClientList *clients, MessagesByClient *msgs)
{
    ClientInfo c_info, chat_dest_info;
    MessagesCollection c_messages;
    MessagesList ml_head;
    MessageContainer container;
    Message *m;
    MessageContainer send;
    int fail, succ, bytes, num_clients;
    bool valid;
    short type;
    char *clientids;
    size_t clientids_len, clientbytessent;

    fail = -1;
    succ = 0;
    c_info = ClientList_get(*clients, socket);
    c_messages = MessagesByClient_get(*msgs, socket);
    ml_head = c_messages->containers;

    if (ml_head == NULL)
        return succ;
    
    container = ml_head->first;
    m = container->msg;
    if (container->bytes < sizeof(Header))
        return succ;
    
    if (c_info == NULL)
    {
        // Expecting a Hello
        valid = Header_hello_check(m->hdr);
        if (!valid)
            return fail;
        if (ClientList_exists(*clients, m->hdr->source))
        {
            type = ERROR_CLIENT_ALREADY_PRESENT;
            send = MessageContainer_new();
            send->bytes = sizeof(Header);
            Message_set_hdr(send->msg, Header_error_cap(m->hdr->source));
            bytes = ChatApp_write(socket, send);
        }
        else
        {
            *clients = ClientList_push(*clients, socket, m->hdr->source);
            type = m->hdr->type;
            send = MessageContainer_new();
            send->bytes = sizeof(Header);
            Message_set_hdr(send->msg, Header_hello_ack(m->hdr->source));
            bytes = ChatApp_write(socket, send);
            
            clientbytessent = 0;
            num_clients = ClientList_length(*clients);
            clientids = CALLOC(num_clients, 20);
            clientids_len = ClientList_tostr(*clients, clientids);
            while (clientbytessent < clientids_len && bytes >= 0)
            {
                send->bytes = sizeof(Header) + 
                    MIN(MAXDATASIZE, clientids_len - clientbytessent);
                Message_set_hdr(send->msg, 
                    Header_client_list(m->hdr->source, 
                        (send->bytes - sizeof(Header))));
                memcpy(send->msg->data, clientids + clientbytessent, 
                       send->bytes - sizeof(Header));
                bytes = ChatApp_write(socket, send);
                clientbytessent = bytes;
            }
            free(clientids);
        }
        MessageContainer_free(&send);
        MessagesCollection_pop(c_messages);
        if (bytes < 0)
            return fail;
        return type;
    }
    else
    {
        if (m->hdr->type == HELLO)
        {
            if (!Header_hello_check(m->hdr))
                return fail;
            if (strcmp(m->hdr->source, c_info->id) == 0)
            {
                send = MessageContainer_new();
                send->bytes = sizeof(Header);
                Message_set_hdr(send->msg, Header_error_cap(m->hdr->source));
                bytes = ChatApp_write(socket, send);
                MessageContainer_free(&send);
                MessagesCollection_pop(c_messages);
                return ERROR_CLIENT_ALREADY_PRESENT;
            }
            else
                return fail;
        }
        else if (m->hdr->type == LIST_REQUEST)
        {
            if (!Header_list_request_check(m->hdr))
                return fail;
            if (strcmp(m->hdr->source, c_info->id) != 0)
                return fail;
            send = MessageContainer_new();
            clientbytessent = 0;
            num_clients = ClientList_length(*clients);
            clientids = CALLOC(num_clients, 20);
            clientids_len = ClientList_tostr(*clients, clientids);
            bytes = 0;
            while (clientbytessent < clientids_len && bytes >= 0)
            {
                send->bytes = sizeof(Header) + 
                    MIN(MAXDATASIZE, clientids_len - clientbytessent);
                Message_set_hdr(send->msg, 
                    Header_client_list(m->hdr->source, 
                        (send->bytes - sizeof(Header))));
                memcpy(send->msg->data, clientids + clientbytessent, 
                       send->bytes - sizeof(Header));
                bytes = ChatApp_write(socket, send);
                clientbytessent += bytes;
            }
            free(clientids);
            MessageContainer_free(&send);
            MessagesCollection_pop(c_messages);
            return LIST_REQUEST;
        }
        else if (m->hdr->type == CHAT)
        {
            if (!Header_chat_check(m->hdr))
                return fail;
            if (!Message_iscomplete(m, container->bytes))
                return succ;
            if (strcmp(m->hdr->source, c_info->id) != 0)
                return fail;
            if (strcmp(m->hdr->source, m->hdr->dest) == 0)
                return fail;
            chat_dest_info = ClientList_get_by_id(*clients, m->hdr->dest);
            if (chat_dest_info == NULL)
            {
                send = MessageContainer_new();
                send->bytes = sizeof(Header);
                Message_set_hdr(send->msg, 
                    Header_error_cd(m->hdr->source, m->hdr->message_id));
                bytes = ChatApp_write(socket, send);
                MessageContainer_free(&send);
                MessagesCollection_pop(c_messages);
                return ERROR_CANNOT_DELIVER;
            }

            send = container;
            bytes = ChatApp_write(chat_dest_info->socket, send);
            MessagesCollection_pop(c_messages);
            return CHAT;
        }
        else if (m->hdr->type == EXIT)
        {
            if (!Header_exit_check(m->hdr))
                return fail;
            if (strcmp(m->hdr->source, c_info->id) != 0)
                return fail;
            return EXIT;
        }
        else
            return fail;
    }
}

/* 
 * Close sockets with messages older than given seconds 
 * Return 0 if no socket closed and 1 otherwise
*/
int ChatApp_purgeold(ClientList *clients, MessagesByClient *msgs, fd_set *fds,
                     int seconds)
{
    MessagesByClient head;
    MessagesCollection curr_collection;
    bool removed;

    head = *msgs;
    removed = false;
    while (head != NULL)
    {
        curr_collection = head->first;
        if (MessagesCollection_hasold(curr_collection, seconds))
        {
            head = head->rest;
            ChatApp_close(curr_collection->socket, clients, msgs, fds);
            removed = true;
        }
        else
            head = head->rest;
    }
    return removed;
}   

/*
 * Function: ChatApp_close
 * -----------------------
 *   Closes the socket connection and removes any stored information
*/
void ChatApp_close(int socket, ClientList *clients, MessagesByClient *msgs,
                   fd_set *fds)
{
    close(socket);
    FD_CLR(socket, fds);
    *clients = ClientList_remove(*clients, socket);
    *msgs = MessagesByClient_remove(*msgs, socket);
}

/*******************************************************************************
 * Functions to handle Sockets
 ******************************************************************************/
/*
 * Function: SocketConn_open
 * -------------------------
 *   Initializes fd in sckt using the AF_INET address domain, a SOCK_STREAM 
 *   type, and a 0 protocol (dynamically decide based on type). Sets fd to
 *   < 0 if it could not be initialized. Sets opt vals to allow for the reuse
 *   of local addresses. Sets the connection details to given portno and IP
 *
 *   sc: contains fd to initialize
 *   portno: the port number to connect to
 *   sin_addr: IP address of the host
 * 
 *   Returns false if failed and true otherwise
 */
bool SocketConn_open(SocketConn *sc, int portno, unsigned int s_addr)
{
    int optval;
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
bool SocketConn_bind(SocketConn *sc) 
{
    return bind(sc->fd, (struct sockaddr *) &(sc->addr), sc->len) >= 0;
}

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
bool SocketConn_listen(SocketConn *sc, int n) 
{
    return listen(sc->fd, n) >= 0;
}

/*
 * Function: SocketConn_connect
 * ----------------------------
 *   Connects server.
 *
 *   s: contains the server socket details
 * 
 *   Returns false if failed and true otherwise
 */
bool SocketConn_connect(SocketConn *s)
{
    return connect(s->fd, (struct sockaddr *)&(s->addr), s->len) >= 0;
}

/*
 * Function: SocketConn_accept
 * ---------------------------
 *   Accepts an incoming connection to server. Stores client connection in 
 *   client. It is an unchecked runtime error to call SocketConn_accept before
 *   a successful SocketConn_listen. 
 *
 *   server: contains the server socket details
 *   client: contains the client socket details
 * 
 *   Returns false if failed and true otherwise
 */
bool SocketConn_accept(SocketConn *server, SocketConn *client)
{
    client->len = sizeof(client->addr);
    client->fd = accept(server->fd, (struct sockaddr *) &(client->addr), 
                        &(client->len));
    client->portno = ntohs(client->addr.sin_port);
    return client->fd >= 0;
}

/*
 * Function: SocketConn_close
 * --------------------------
 *   Closes the socket connection.
 */
void SocketConn_close(SocketConn *sc)
{
    close(sc->fd);
}

ssize_t Socket_write(int socket, const char *s, size_t n)
{
    size_t sent;
    int bytes;
    sent = 0;
    do
    {
        bytes = write(socket, s + sent, n - sent);
        if (bytes < 0)
            return bytes;
        sent += bytes;
    } while (sent < n);
    return sent;
}

/*******************************************************************************
 * Functions to handle Mem
 * Adapted from C Interfaces and Implementations by David T. Hanson
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
 * Functions to handle Lists
 ******************************************************************************/
List_T List_push(List_T list, void *x) {
	List_T p;
	NEW(p);
	p->first = x;
	p->rest  = list;
	return p;
}
List_T List_append(List_T list, List_T tail) {
	List_T *p = &list;
	while (*p)
		p = &(*p)->rest;
	*p = tail;
	return list;
}
List_T List_copy(List_T list) {
	List_T head, *p = &head;
	for ( ; list; list = list->rest) {
		NEW(*p);
		(*p)->first = list->first;
		p = &(*p)->rest;
	}
	*p = NULL;
	return head;
}
List_T List_pop(List_T list, void **x) {
	if (list) {
		List_T head = list->rest;
		if (x)
			*x = list->first;
		FREE(list);
		return head;
	} else
		return list;
}
List_T List_reverse(List_T list) {
	List_T head = NULL, next;
	for ( ; list; list = next) {
		next = list->rest;
		list->rest = head;
		head = list;
	}
	return head;
}
int List_length(List_T list) {
	int n;
	for (n = 0; list; list = list->rest)
		n++;
	return n;
}
void List_free(List_T *list) {
	List_T next;
	assert(list);
	for ( ; *list; *list = next) {
		next = (*list)->rest;
		FREE(*list);
	}
}
void List_map(List_T list,
	void apply(void **x, void *cl), void *cl) {
	assert(apply);
	for ( ; list; list = list->rest)
		apply(&list->first, cl);
}
void **List_toArray(List_T list, void *end) {
	int i, n = List_length(list);
	void **array = ALLOC((n + 1)*sizeof (*array));
	for (i = 0; i < n; i++) {
		array[i] = list->first;
		list = list->rest;
	}
	array[i] = end;
	return array;
}
