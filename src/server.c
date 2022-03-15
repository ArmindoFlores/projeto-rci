#define _POSIX_C_SOURCE 200112L
#include "server.h"
#include "client.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>

int init_server(t_nodeinfo *ni)
{
    // Try to create a socket for TCP connections
    int mainfd = socket(AF_INET, SOCK_STREAM, 0);
    if (mainfd == -1) {
        return -1;
    }
    ni->mainfd = mainfd;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get address info
    if (getaddrinfo(NULL, ni->tcpserverport, &hints, &res) != 0) {
        return -1;
    }

    // Bind this address
    if (bind(mainfd, res->ai_addr, res->ai_addrlen) == -1) {
        return -1;
    }

    // Start listening for connections
    if (listen(mainfd, 5) == -1) {
        return -1;
    }

    return 0;
}

void close_server(t_nodeinfo *ni)
{
    if (ni->mainfd >= 0)
        close(ni->mainfd);
    if (ni->prevfd >= 0)
        close(ni->prevfd);
    if (ni->nextfd >= 0)
        close(ni->nextfd);
    if (ni->tempfd >= 0)
        close(ni->tempfd);
}

int process_incoming_connection(t_nodeinfo *ni)
{
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    char ipaddr[INET_ADDRSTRLEN] = "";

    // Accept the connection
    int newfd = accept(ni->mainfd, &addr, &addrlen);
    if (newfd == -1)
        return -1;

    ipaddr_from_sockaddr(&addr, ipaddr);    

    if (ni->tempfd >= 0) {
        // We already have a connection being established, reject this one
        printf("\x1b[33m[*] Rejected connection from %s\033[m\n", ipaddr);
        close(newfd);
        return 0;
    }

    printf("\x1b[32m[*] Accepted connection from %s\033[m\n", ipaddr);

    // Save connection information in ni->temp and the socket fd in ni->tempfd
    ni->tempfd = newfd;
    if (ni->temp == NULL)
        ni->temp = new_conn_info(2048);
    else
        set_conn_info(ni->temp, 2048);
    return 0;
}

/**
 * @brief Reset buffer size, close and set fd to -1
 * 
 * @param buffer_size 
 * @param fd 
 */
void reset_pmt(size_t *buffer_size, int *fd)
{
    close(*fd);
    *fd = -1;
    *buffer_size = 0;
}

/**
 * @brief Process an incoming message and update internal buffers
 * 
 * @param sfd socket file descriptor
 * @param buffer buffer to store the received message in
 * @param buffer_size current size of buffer
 * @param max_buffer_size maximum possible buffer size
 * @param ci necessary information about the connection
 * @return [ @b t_read_out ] structure describing the result 
 */
t_read_out process_incoming(int *sfd, char *buffer, size_t *buffer_size, size_t max_buffer_size, t_conn_info *ci)
{
    // Receive message (either from socker or internal buffers)
    t_read_out ro = recv_message(*sfd, buffer+(*buffer_size), '\n', max_buffer_size-(*buffer_size)-1, ci);
    if (ro.read_type == RO_SUCCESS) {
        // Successfully read, update buffer
        *buffer_size += ro.read_bytes;
        if (*buffer_size >= max_buffer_size-1 && buffer[max_buffer_size-1] != '\n') {
            // This is already an invalid message (too big)
            reset_pmt(buffer_size, sfd);
            puts("\x1b[31m[!] Received a message with invalid size\033[m");
            ro.read_type = RO_DISCONNECT;
            return ro;
        }
    }
    else if (ro.read_type == RO_ERROR) {
        // An error occurred while receiving
        reset_pmt(buffer_size, sfd);
        return ro;
    }
    else if (ro.read_type == RO_DISCONNECT) {
        // Client closed the connection
        puts("[*] Client disconnected");
        return ro;
    }

    // Make sure the buffer is a null terminated string
    buffer[*buffer_size] = '\0';

    return ro;
}

int process_message_successor(t_nodeinfo *ni)
{
    //! This function is a work-in-progress
    // This is the internal buffer that keep track of what has been
    // sent through ni->nextfd
    static char buffer[64];
    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    t_read_out ro = process_incoming(&ni->nextfd, buffer, &buffer_size, sizeof(buffer), ni->successor);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        // !! Successor has disconnected!
        puts("\x1b[31m[!] Successor has disconnected abruptly (ring may be broken)!\033[m");
        if (ni->nextfd == ni->prevfd) {
            // This is a two-node network
            ni->prevfd = -1;
        }
        reset_pmt(&buffer_size, &ni->nextfd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    printf("[*] Received from successor: '%s'\n", buffer);
    return 0;
}

int process_message_predecessor(t_nodeinfo *ni)
{
    //! This function is a work-in-progress
    // This is the internal buffer that keep track of what has been
    // sent through ni->prevfd
    static char buffer[64];
    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    t_read_out ro = process_incoming(&ni->prevfd, buffer, &buffer_size, sizeof(buffer), ni->predecessor);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        // !! Predecessor has disconnected!
        puts("\x1b[31m[!] Predecessor has disconnected abruptly (ring may be broken)!\033[m");
        if (ni->nextfd == ni->prevfd) {
            // This is a two-node network
            ni->nextfd = -1;
        }
        reset_pmt(&buffer_size, &ni->prevfd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    if (strncmp(buffer, "PRED ", 5) != 0) {
        // Message is invalid
        reset_pmt(&buffer_size, &ni->tempfd);
        puts("\x1b[31m[!] Discarded message: length, termination or header\033[m");
        return 0;
    }

    t_msginfo mi = get_message_info(buffer, buffer_size);
    if (mi.type != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor)\033[m", ni->pred_ip, ni->pred_port);
        reset_pmt(&buffer_size, &ni->tempfd);
        return 0;
    }

    printf("\x1b[32m[*] Received PRED message, setting node %d (%s:%d) as predecessor\033[m\n", mi.node_i, mi.node_ip, mi.node_port);

    char portstr[6] = "";
    snprintf(portstr, sizeof(portstr), "%d", mi.node_port);

    // Open a connection to the new predecessor
    int result = init_client(mi.node_ip, portstr, ni);
    if (result != 0) {
        // An error occurred
        return -1;
    }

    // Message to be sent
    char message[64] = "";

    // Let the new predecessor know it has a new successor
    sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->tcpserverport);

    result = sendall(ni->prevfd, message, strlen(message));
    if (result > 0) {
        // An error occurred
        close(ni->prevfd);
        return -1;
    }
    if (result == -1) {
        // Client disconnected
        puts("\x1b[31m[!] New predecessor has disconnected abruptly (ring may be broken)!\033[m");
        close(ni->prevfd);
        return 0;
    }

    return 0;
}

int process_message_temp(t_nodeinfo *ni)
{
    // This is the internal buffer that keep track of what has been
    // sent through ni->tempfd
    static char buffer[64];

    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    // Receive message and update buffer
    t_read_out ro = process_incoming(&ni->tempfd, buffer, &buffer_size, sizeof(buffer), ni->temp);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        reset_pmt(&buffer_size, &ni->tempfd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    // Message should be of the format <SELF i i.IP i.port\n>
    if (strncmp(buffer, "SELF ", 5) != 0) {
        // Message is invalid
        reset_pmt(&buffer_size, &ni->tempfd);
        puts("\x1b[31m[!] Discarded message: length, termination or header\033[m");
        return 0;
    }

    t_msginfo mi = get_message_info(buffer, buffer_size);
    if (mi.type != MI_SUCCESS) {
        puts("\x1b[31m[!] Received malformatted message\033[m");
        reset_pmt(&buffer_size, &ni->tempfd);
        return 0;
    }

    printf("\x1b[32m[*] Received SELF message, setting node %d (%s:%d) as successor\033[m\n", mi.node_i, mi.node_ip, mi.node_port);
    buffer_size = 0;

    if (ni->nextfd != -1) {
        // Message to be sent
        char message[64] = "";

        // Let the current successor know it has a new predecessor
        sprintf(message, "PRED %d %s %d\n", mi.node_i, mi.node_ip, mi.node_port);
        int result = sendall(ni->nextfd, message, strlen(message));
        if (result < 0) {
            // Successor disconnected
            close(ni->nextfd);
            ni->nextfd = -1;
        }
        else if (result > 0) {
            // An error occurred
            close(ni->nextfd);
            ni->nextfd = -1;
            reset_pmt(&buffer_size, &ni->tempfd);
            return -1;
        }
    }

    // Set this node's successor to be the current connection
    ni->nextfd = ni->tempfd;
    strcpy(ni->succ_ip, mi.node_ip);
    ni->succ_port = mi.node_port; 
    copy_conn_info(&ni->successor, ni->temp);

    if (ni->prevfd == -1) {
        // This node is the first node in the ring -> close the cycle
        ni->prevfd = ni->tempfd;
        strcpy(ni->pred_ip, mi.node_ip);
        ni->pred_port = mi.node_port;
        copy_conn_info(&ni->predecessor, ni->temp);
    }

    reset_conn_buffer(ni->temp);   
    ni->tempfd = -1;

    return 0;
}