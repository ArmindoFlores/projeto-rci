#define _POSIX_C_SOURCE 200112L
#include "server.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>

struct error_or_nodeinfo {
    int is_error;
    t_nodeinfo *si;
};

int is_error(t_error_or_nodeinfo *esi)
{
    return esi->is_error < 0;
}

t_nodeinfo* get_nodeinfo(t_error_or_nodeinfo *esi)
{
    return esi->si;
}

t_error_or_nodeinfo* init_server(const char* port)
{
    t_error_or_nodeinfo *result = (t_error_or_nodeinfo*) malloc(sizeof(t_error_or_nodeinfo));
    result->is_error = 1; // Assume it's an error
    result->si = new_nodeinfo();

    // Try to create a socket for TCP connections
    int mainfd = socket(AF_INET, SOCK_STREAM, 0);
    if (mainfd == -1) {
        free_nodeinfo(result->si);
        return result;
    }
    result->si->mainfd = mainfd;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get address info
    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        free_nodeinfo(result->si);
        return result;
    }

    // Bind this address
    if (bind(mainfd, res->ai_addr, res->ai_addrlen) == -1) {
        free_nodeinfo(result->si);
        return result;
    }

    // Start listening for connections
    if (listen(mainfd, 5) == -1) {
        free_nodeinfo(result->si);
        return result;
    }
    
    result->is_error = 0;
    return result;

    //! Missing: also init UDP server
}

t_event select_event(t_nodeinfo* si)
{
    // If there's pending reads in any of the connections, do them first
    if (si->nextfd > 0 && has_available_data(si->successor))
        return E_MESSAGE_SUCCESSOR;
    if (si->prevfd > 0 && has_available_data(si->predecessor))
        return E_MESSAGE_PREDECESSOR;
    if (si->tempfd > 0 && has_available_data(si->temp))
        return E_MESSAGE_TEMP;

    // Initialize file descriptor set
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int fdmax = maxfd(si);

    // Add currently in-use file descriptors to the set
    FD_SET(si->mainfd, &read_fds);
    if (si->nextfd > 0)
        FD_SET(si->nextfd, &read_fds);
    if (si->prevfd > 0)
        FD_SET(si->prevfd, &read_fds);
    if (si->tempfd > 0)
        FD_SET(si->tempfd, &read_fds);

    int count = select(fdmax+1, &read_fds, NULL, NULL, NULL);
    if (count < 0)
        exit(1);
    while (count--) {
        // Check for ready fd's with FD_ISSET
        if (FD_ISSET(si->mainfd, &read_fds)) {
            // Incoming connection
            FD_CLR(si->mainfd, &read_fds);
            return E_INCOMING_CONNECTION;
        }
        else if (FD_ISSET(si->nextfd, &read_fds)) {
            // Incoming message from successor
            FD_CLR(si->nextfd, &read_fds);
            return E_MESSAGE_SUCCESSOR;
        }
        else if (FD_ISSET(si->prevfd, &read_fds)) {
            // Incoming message from predecessor
            FD_CLR(si->prevfd, &read_fds);
            return E_MESSAGE_PREDECESSOR;
        }
        else if (FD_ISSET(si->tempfd, &read_fds)) {
            // Incoming message from somewhere else
            FD_CLR(si->tempfd, &read_fds);
            return E_MESSAGE_TEMP;
        }
    }
    return E_ERROR;
}

void close_server(t_nodeinfo* si)
{
    if (si->mainfd >= 0)
        close(si->mainfd);
    if (si->prevfd >= 0)
        close(si->prevfd);
    if (si->nextfd >= 0)
        close(si->nextfd);
    if (si->tempfd >= 0)
        close(si->tempfd);

    free_nodeinfo(si);
}

int process_incoming_connection(t_nodeinfo *si)
{
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);

    // Accept the connection
    int newfd = accept(si->mainfd, &addr, &addrlen);
    if (newfd == -1)
        return -1;

    if (si->tempfd >= 0 || si->nextfd > 0) {
        // We already have a connection being established, reject this one
        puts("Rejected connection!");
        close(newfd);
        return 0;
    }

    puts("Accepted connection");

    // Save connection information in si->temp and the socket fd in si->tempfd
    si->tempfd = newfd;
    if (si->temp == NULL)
        si->temp = new_conn_info(2048, addr, addrlen);
    else
        set_conn_info(si->temp, 2048, addr, addrlen);
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
            puts("Message is too big");
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
        puts("Client disconnected");
        return ro;
    }

    // Make sure the buffer is a null terminated string
    buffer[*buffer_size] = '\0';
    
    printf("Buffer: %s (rb=%ld, bs=%ld)\n", buffer, ro.read_bytes, *buffer_size);

    return ro;
}

int process_message_successor(t_nodeinfo *si)
{
    //! This function is a work-in-progress
    // This is the internal buffer that keep track of what has been
    // sent through si->nextfd
    static char buffer[64];
    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    t_read_out ro = process_incoming(&si->nextfd, buffer, &buffer_size, sizeof(buffer), si->successor);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        // !! Successor has disconnected!
        if (si->nextfd == si->prevfd) {
            // This is a two-node network
            si->prevfd = -1;
        }
        reset_pmt(&buffer_size, &si->nextfd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        puts("Waiting for more bytes...");
        return 0;
    }

    printf("Received from successor: '%s'\n", buffer);
    return 0;
}

int process_message_temp(t_nodeinfo *si)
{
    // This is the internal buffer that keep track of what has been
    // sent through si->tempfd
    static char buffer[64];

    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    // Receive message and update buffer
    t_read_out ro = process_incoming(&si->tempfd, buffer, &buffer_size, sizeof(buffer), si->temp);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        reset_pmt(&buffer_size, &si->tempfd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        puts("Waiting for more bytes...");
        return 0;
    }

    // Message should be of the format <SELF i i.IP i.port\n>
    if (strncmp(buffer, "SELF ", 5) != 0) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        puts("Discarded message: length, termination or header");
        return 0;
    }

    // Find the node identifier, IP and port  
    
    // Pointer to the ' ' character before the node identifier
    char *i_start = strchr(buffer, ' ');
    if (i_start == NULL) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        puts("Discarded message: no identifier specified");
        return 0;
    }
    // Pointer to the ' ' character before the node IP
    char *ip_start = strchr(i_start+1, ' ');
    if (ip_start == NULL) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        puts("Discarded message: no IP specified");
        return 0;
    }
    // Pointer to the ' ' character before the node port
    char *port_start = strchr(ip_start+1, ' ');
    if (port_start == NULL) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        puts("Discarded message: no port specified");
        return 0;
    }

    // This buffer will be used to convert strings to other data types
    char convert_buffer[64];

    // Find node_i
    memcpy(convert_buffer, i_start+1, ip_start-i_start-1);
    convert_buffer[ip_start-i_start-1] = '\0';
    if (!strisui(convert_buffer)) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        printf("Discarded message: no node_i is not a number (%s)\n", convert_buffer);
        return 0;
    }
    // Node identifier
    int node_i = strtoui(convert_buffer);

    // Find node_ip
    memcpy(convert_buffer, ip_start+1, port_start-ip_start-1);
    convert_buffer[port_start-ip_start-1] = '\0';
    if (!isipaddr(convert_buffer)) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        printf("Discarded message: no node_ip is not an ip (%s)\n", convert_buffer);
        return 0;
    }
    // Node IP address
    char node_ip[16];
    strcpy(node_ip, convert_buffer);

    // Find node_port
    memcpy(convert_buffer, port_start+1, buffer_size-(int)(port_start-buffer)-2);
    convert_buffer[buffer_size-(int)(port_start-buffer)-2] = '\0';
    if (!strisui(convert_buffer)) {
        // Message is invalid
        reset_pmt(&buffer_size, &si->tempfd);
        printf("Discarded message: no node_port is not a number (%s)\n", convert_buffer);
        return 0;
    }
    // Node port
    int node_port = strtoui(convert_buffer);

    // reset_pmt(&buffer_size, &si->tempfd);
    printf("Received valid message: i=%d IP=%s port=%d\n", node_i, node_ip, node_port);
    buffer_size = 0;

    if (si->nextfd != -1) {
        // Message to be sent
        char message[64] = "";

        // Let the current successor know it has a new predecessor
        sprintf(message, "PRED %d %s %d\n", node_i, node_ip, node_port);
        int result = sendall(si->nextfd, message, strlen(message));
        if (result < 0) {
            // Successor disconnected
            close(si->nextfd);
            si->nextfd = -1;
        }
        else if (result > 0) {
            // An error occurred
            close(si->nextfd);
            si->nextfd = -1;
            reset_pmt(&buffer_size, &si->tempfd);
            return -1;
        }
    }

    // Set this node's successor to be the current connection
    si->nextfd = si->tempfd;
    copy_conn_info(&si->successor, si->temp);

    if (si->prevfd == -1) {
        // This node is the first node in the ring -> close the cycle
        si->prevfd = si->tempfd;
        copy_conn_info(&si->predecessor, si->temp);
    }

    reset_conn_buffer(si->temp);   
    si->tempfd = -1;

    return 0;
}