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
#include <limits.h>
#include <time.h>

int init_server(t_nodeinfo *ni)
{
    // Try to create a socket for TCP connections
    int main_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (main_fd == -1) {
        return -1;
    }
    ni->main_fd = main_fd;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get address info
    if (getaddrinfo(ni->ipaddr, ni->self_port, &hints, &res) != 0) {
        return -1;
    }

    // Bind this address
    if (bind(main_fd, res->ai_addr, res->ai_addrlen) == -1) {
        freeaddrinfo(res);
        return -1;
    }

    // Start listening for connections
    if (listen(main_fd, 5) == -1) {
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    hints.ai_socktype = SOCK_STREAM;

    // Get address info
    if (getaddrinfo(NULL, ni->self_port, &hints, &res) != 0) {
        return -1;
    }

    // Create UDP socket
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == -1) {
        close(main_fd);
        freeaddrinfo(res);
        return -1;
    }

    // Bind this address
    if (bind(udp_fd, res->ai_addr, res->ai_addrlen) == -1) {
        close(main_fd);
        freeaddrinfo(res);
        return -1;
    }

    ni->udp_fd = udp_fd;

    freeaddrinfo(res);

    return 0;
}

int send_to_closest(char *message, unsigned int key, t_nodeinfo *ni)
{
    unsigned int distance_succ = ring_distance(ni->succ_id, key);
    unsigned int distance_shcut = ni->shcut_info != NULL ? ring_distance(ni->shcut_id, key) : UINT_MAX;
    if (distance_shcut < distance_succ && !find_udp_message_from(ni, ni->shcut_info->ai_addr)) {
        // Search key is closer to shortcut than to successor
        puts("Trying to send message through shortcut");
        int result = udpsend(ni->udp_fd, message, strlen(message)-1, ni->shcut_info);
        if (result != 0) {
            // Couldn't resend the message
            puts("\x1b[31m[!] Couldn't resend the message\033[m");
            return -1;
        }
        return register_udp_message(ni, message, strlen(message)-1, ni->shcut_info->ai_addr, ni->shcut_info->ai_addrlen, UDPMSG_CHORD);
    }
    else {
        // Forward the message to successor
        int result = sendall(ni->succ_fd, message, strlen(message));
        if (result != 0) {
            // Couldn't resend the message
            puts("\x1b[31m[!] Couldn't resend the message\033[m");
            return -1;
        }
        return 0;
    }
    return 0;
}

int process_found_key(unsigned int search_key, unsigned int n, char *ipaddr, unsigned int port, t_nodeinfo* ni)
{
    int request_key = get_associated_key(n, ni);
    if (request_key == -1) {
        puts("\x1b[33m[!] Received \"RSP\" message without requesting it\033[m");
    }
    else {
        struct sockaddr sa;
        socklen_t sa_len;
        if (get_associated_addrinfo(n, &sa, &sa_len, ni) == 0) {
            // Find request was initiated by an EFND message
            char message[64] = "";
            sprintf(message, "EPRED %u %s %u", search_key, ipaddr, port);

            struct addrinfo ai;
            ai.ai_addr = &sa;
            ai.ai_addrlen = sa_len;
            if (udpsend(ni->udp_fd, message, strlen(message), &ai) != 0) {
                puts("\x1b[31m[!] Error sending EPRED message\033[m");
            }
            else {
                if (register_udp_message(ni, message, strlen(message), &sa, sa_len, UDPMSG_ENTERING) == -1)
                    return -1; 
            }
        }
        else  // Find request was initiated by the user
            printf("Key %u belongs to node %u (%s:%u)\n", request_key, search_key, ipaddr, port);

        drop_request(n, ni);
    }

    return 0;
}

int process_incoming_connection(t_nodeinfo *ni)
{
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    char ipaddr[INET_ADDRSTRLEN] = "";

    // Accept the connection
    int newfd = accept(ni->main_fd, &addr, &addrlen);
    if (newfd == -1)
        return -1;

    ipaddr_from_sockaddr(&addr, ipaddr);    

    if (ni->temp_fd >= 0) {
        // We already have a connection being established, reject this one
        printf("\x1b[33m[!] Rejected connection from %s\033[m\n", ipaddr);
        close(newfd);
        return 0;
    }

    printf("\x1b[32m[*] Accepted connection from %s\033[m\n", ipaddr);

    // Save connection information in ni->temp and the socket fd in ni->temp_fd
    ni->temp_fd = newfd;
    if (ni->temp == NULL) {
        ni->temp = new_conn_info(2048);
        if (ni->temp == NULL)
            return -1;
    }
    else {
        if (set_conn_info(ni->temp, 2048) != 0)
            return -1;
    }
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
    if (*fd >= 0)
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
    // Receive message (either from socket or internal buffers)
    t_read_out ro = recv_message(*sfd, buffer+(*buffer_size), '\n', max_buffer_size-(*buffer_size)-1, ci);
    if (ro.read_type == RO_SUCCESS) {
        // Successfully read, update buffer
        *buffer_size += ro.read_bytes;
        if (*buffer_size >= max_buffer_size-1 && buffer[max_buffer_size-1] != '\n') {
            // This is already an invalid message (too big)
            reset_pmt(buffer_size, sfd);
            printf("-> %s\n", buffer);
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

int process_fnd_message(char *buffer, size_t buffer_size, t_nodeinfo *ni)
{
    unsigned int search_key, n, key, port;
    char ipaddr[INET_ADDRSTRLEN] = "";
    t_msginfotype mi = get_fnd_or_rsp_or_get_message_info(buffer, &search_key, &n, &key, ipaddr, &port);
    if (mi != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
        return -1;
    }
    // Calculate distance to self, successor, and shortcut
    unsigned int distance_self = ring_distance(ni->key, search_key);
    unsigned int distance_succ = ring_distance(ni->succ_id, search_key);
    if (distance_self <= distance_succ) {
        // Search key belongs to this node
        puts("\x1b[33m[*] Found the key!\033[m");
        char message[64] = "";
        sprintf(message, "RSP %u %u %u %s %s\n", key, n, ni->key, ni->ipaddr, ni->self_port);
        int result = send_to_closest(message, key, ni);
        if (result != 0)
            return -1;
    }
    else {
        int result = send_to_closest(buffer, search_key, ni);
        if (result < 0)
            return -1;
    }
    return 0;
}

int process_rsp_message(char *buffer, size_t buffer_size, t_nodeinfo *ni)
{
    unsigned int search_key, n, key, port;
    char ipaddr[INET_ADDRSTRLEN] = "";
    t_msginfotype mi = get_fnd_or_rsp_or_get_message_info(buffer, &key, &n, &search_key, ipaddr, &port);
    if (mi != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
        return -1;
    }
    if (key == ni->key) {
        // This message is meant for this node. Process it
        if (process_found_key(search_key, n, ipaddr, port, ni) != 0)
            return -1;
    }
    else {
        // This message is not meant for this node. Forward it.
        int result = send_to_closest(buffer, key, ni);
        if (result < 0)
            return -1;
    }
    return 0;
}

int process_get_message(char *buffer, size_t buffer_size, t_nodeinfo *ni)
{
    unsigned int search_key, n, key, port;
    char ipaddr[INET_ADDRSTRLEN] = "";
    t_msginfotype mi = get_fnd_or_rsp_or_get_message_info(buffer, &search_key, &n, &key, ipaddr, &port);
    if (mi != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
        return -1;
    }
    if (ring_distance(ni->key, search_key) <= ring_distance(ni->succ_id, search_key)) {
        // This node has this object; forward its value
        char message[64] = "";
        char *object = get_object(search_key, ni);

        puts("\x1b[33m[*] Found the object!\033[m");
        sprintf(message, "RGET %u %u %u %s\n", key, n, search_key, object != NULL ? object : "");
        int result = send_to_closest(message, key, ni);
        if (result < 0)
            return -1;
    }
    else {
        // This message is not meant for this node. Forward it.
        int result = send_to_closest(buffer, search_key, ni);
        if (result < 0)
            return -1;
    }
    return 0;
}

int process_rget_message(char *buffer, size_t buffer_size, t_nodeinfo *ni)
{
    unsigned int search_key, n, key;
    char value[24] = "";
    t_msginfotype mi = get_rget_or_set_message_info(buffer+5, &key, &n, &search_key, value);
    if (mi != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
        return -1;
    }
    if (key == ni->key) {
        // This message is meant for this node. Process it
        int key = get_associated_key(n, ni);
        if (key == -1) {
            puts("\x1b[33m[!] Received \"RSP\" message without requesting it\033[m");
            return -1;
        }
        if (strlen(value) == 0)
            printf("%d -> NULL\n", key);
        else
            printf("%d -> \"%s\"\n", key, value);
        drop_request(n, ni);
    }
    else {
        // This message is not meant for this node. Forward it.
        int result = send_to_closest(buffer, key, ni);
        if (result < 0)
            return -1;
    }
    return 0;
}

int process_set_message(char *buffer, size_t buffer_size, int from_successor, t_nodeinfo *ni)
{
    unsigned int search_key, n, key;
    char value[24] = "";
    t_msginfotype mi = get_rget_or_set_message_info(buffer+4, &search_key, &n, &key, value);
    if (mi != MI_SUCCESS) {
        printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
        return -1;
    }
    // printf("search_key=%u key=%u\n", search_key, key);
    if (ni->succ_fd == -1 || from_successor || ring_distance(ni->key, search_key) <= ring_distance(ni->succ_id, search_key)) {
        // This node has this object; set its value
        if (strlen(value) == 0) {
            if (set_object(search_key, NULL, ni) == -1)
                return -1;
        }
        else {
            if (set_object(search_key, value, ni) == -1)
                return -1;
        }
    }
    else {
        // This message is not meant for this node. Forward it.
        int result = send_to_closest(buffer, search_key, ni);
        if (result < 0)
            return -1;
    }
    return 0;
}

int redestribute_objects(t_nodeinfo *ni)
{
    char message[64] = "";
    for (unsigned int i = 0; i < 32; i++) {
        if (ni->objects[i] != NULL && ring_distance(ni->key, i) > ring_distance(ni->succ_id, i)) {
            sprintf(message, "SET %u %u %u %s\n", i, ni->find_n, ni->key, ni->objects[i]);
            free(ni->objects[i]);
            ni->objects[i] = NULL;
            int result = send_to_closest(message, i, ni);
            if (result < 0)
                return result;
            ni->find_n++;
            ni->find_n %= 100;
        }
    }
    return 0;
}

int process_message_successor(t_nodeinfo *ni)
{
    // This is the internal buffer that keep track of what has been
    // sent through ni->succ_fd
    static char buffer[64];
    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    t_read_out ro = process_incoming(&ni->succ_fd, buffer, &buffer_size, sizeof(buffer), ni->successor);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        // !! Successor has disconnected!
        puts("\x1b[33m[!] Successor has disconnected abruptly (ring may be broken)\033[m");
        if (ni->succ_fd == ni->pred_fd) {
            // This is a two-node network
            ni->pred_fd = -1;
        }
        reset_pmt(&buffer_size, &ni->succ_fd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    if (strncmp(buffer, "SET ", 4) == 0) {
        if (process_set_message(buffer, buffer_size, 1, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else {
        // Message is invalid
        reset_pmt(&buffer_size, &ni->pred_fd);
        printf("\x1b[31m[!] Discarded message: length, termination or header ('%s')\033[m\n", buffer);
        return 0;
    }
    
    return 0;
}

int process_message_predecessor(t_nodeinfo *ni)
{
    // This is the internal buffer that keep track of what has been
    // sent through ni->pred_fd
    static char buffer[64];
    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    t_read_out ro = process_incoming(&ni->pred_fd, buffer, &buffer_size, sizeof(buffer), ni->predecessor);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        // !! Predecessor has disconnected!
        if (ni->pred_id != ni->key) {
            // This is not a one-node network
            puts("\x1b[31m[!] Predecessor has disconnected abruptly (ring is broken)\033[m");
            if (ni->succ_fd == ni->pred_fd) {
                // This is a two-node network
                ni->succ_fd = -1;
            }
            reset_pmt(&buffer_size, &ni->pred_fd);
        }
        else {
            puts("I disconnected");
            reset_pmt(&buffer_size, &ni->pred_fd);
        }
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    if (strncmp(buffer, "PRED ", 5) == 0) {
        unsigned int node_i, node_port;
        char node_ip[INET_ADDRSTRLEN] = "";
        t_msginfotype mi = get_self_or_pred_message_info(buffer, &node_i, node_ip, &node_port);
        if (mi != MI_SUCCESS) {
            printf("\x1b[31m[!] Received malformatted message from %s:%d (predecessor): '%s'\033[m\n", ni->pred_ip, ni->pred_port, buffer);
            reset_pmt(&buffer_size, &ni->temp_fd);
            return 0;
        }

        printf("\x1b[32m[*] Received \"PRED\" message from %s:%d, setting node %d (%s:%d) as predecessor\033[m\n", ni->pred_ip, ni->pred_port, node_i, node_ip, node_port);

        char portstr[6] = "";
        snprintf(portstr, sizeof(portstr), "%d", node_port);

        // Open a connection to the new predecessor
        int result = init_client(node_ip, portstr, ni);
        if (result != 0) {
            // An error occurred
            return -1;
        }

        buffer_size = 0;
        ni->pred_id = node_i;

        // Message to be sent
        char message[64] = "";

        // Let the new predecessor know it has a new successor
        sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->self_port);

        result = sendall(ni->pred_fd, message, strlen(message));
        if (result > 0) {
            // An error occurred
            if (ni->pred_fd >= 0)
                close(ni->pred_fd);
            return -1;
        }
        if (result == -1) {
            // Client disconnected
            puts("\x1b[31m[!] New predecessor has disconnected abruptly (ring is broken)\033[m");
            if (ni->pred_fd >= 0)
                reset_pmt(&buffer_size, &ni->pred_fd);
            return 0;
        }
    }
    else if (strncmp(buffer, "FND ", 4) == 0) {
        if (process_fnd_message(buffer, buffer_size, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else if (strncmp(buffer, "RSP ", 4) == 0) {
        if (process_rsp_message(buffer, buffer_size, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else if (strncmp(buffer, "GET ", 4) == 0) {
        if (process_get_message(buffer, buffer_size, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else if (strncmp(buffer, "SET ", 4) == 0) {
        if (process_set_message(buffer, buffer_size, 0, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else if (strncmp(buffer, "RGET ", 5) == 0) {
        if (process_rget_message(buffer, buffer_size, ni) != 0)
            reset_pmt(&buffer_size, &ni->pred_fd);
        buffer_size = 0;
        return 0;
    }
    else {
        // Message is invalid
        reset_pmt(&buffer_size, &ni->pred_fd);
        printf("\x1b[31m[!] Discarded message: length, termination or header ('%s')\033[m\n", buffer);
        return 0;
    }

    return 0;
}

int process_message_temp(t_nodeinfo *ni)
{
    // This is the internal buffer that keep track of what has been
    // sent through ni->temp_fd
    static char buffer[64];

    // This is the current size of the internal buffer
    static size_t buffer_size = 0;

    // Receive message and update buffer
    t_read_out ro = process_incoming(&ni->temp_fd, buffer, &buffer_size, sizeof(buffer), ni->temp);
    if (ro.read_type == RO_ERROR)
        return ro.error_code;
    if (ro.read_type == RO_DISCONNECT) {
        reset_pmt(&buffer_size, &ni->temp_fd);
        return 0;
    }

    if (buffer[buffer_size-1] != '\n') {
        // We might not have received the whole message yet
        return 0;
    }

    // Message should be of the format <SELF i i.IP i.port\n>
    if (strncmp(buffer, "SELF ", 5) != 0) {
        // Message is invalid
        reset_pmt(&buffer_size, &ni->temp_fd);
        puts("\x1b[31m[!] Discarded message: length, termination or header\033[m");
        return 0;
    }

    unsigned int node_i, node_port;
    char node_ip[INET_ADDRSTRLEN] = "";
    t_msginfotype mi = get_self_or_pred_message_info(buffer, &node_i, node_ip, &node_port);
    if (mi != MI_SUCCESS) {
        puts("\x1b[31m[!] Received malformatted message\033[m");
        reset_pmt(&buffer_size, &ni->temp_fd);
        return 0;
    }

    printf("\x1b[32m[*] Received \"SELF\" message, setting node %d (%s:%d) as successor\033[m\n", node_i, node_ip, node_port);
    buffer_size = 0;

    // Message to be sent
    char message[64] = "";
    // Only send PRED message if we have a successor and the node is entering 
    if (ni->succ_id != ni->key && ni->succ_fd != -1 && ring_distance(ni->key, node_i) <= ring_distance(ni->key, ni->succ_id)) {
        // There are already more than two nodes in this ring
        // Let the current successor know it has a new predecessor
        sprintf(message, "PRED %d %s %d\n", node_i, node_ip, node_port);
        int result = sendall(ni->succ_fd, message, strlen(message));
        if (result < 0) {
            //! Successor disconnected
            puts("\x1b[33m[!] Successor has disconnected abruptly (ring may be broken)\033[m");
            if (ni->succ_fd >= 0)
                close(ni->succ_fd);
            ni->succ_fd = -1;
        }
        else if (result > 0) {
            // An error occurred
            if (ni->succ_fd >= 0)
                close(ni->succ_fd);
            ni->succ_fd = -1;
            reset_pmt(&buffer_size, &ni->temp_fd);
            return -1;
        }
    }
    else if (ni->pred_id == ni->key) {
        // Make sure we don't keep re-sending this message to ourselves
        if (ni->succ_id == ni->key && ni->pred_id == ni->key) {
            // These are the first two nodes in the ring, and they're still not connected
            // Let the new node know its predecessor will also be its successor
            // This skips one step (sending PRED to ourselves)
            int result;

            char portstr[6] = "";
            snprintf(portstr, sizeof(portstr), "%d", node_port);

            // Set the connecting node to also be this node's predecessor
            result = init_client(node_ip, portstr, ni);
            if (result != 0) {
                // Error: couldn't establish connection
                // This is still recoverable as there is only one node in the ring
                printf("\x1b[33m[!] Couldn't establish connection to new predecessor\033[m\n");
                reset_pmt(&buffer_size, &ni->temp_fd);
                return 0;
            }
            
            ni->pred_id = node_i;

            
            sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->self_port);
            result = sendall(ni->pred_fd, message, strlen(message));
            if (result < 0) {
                // Temporary connection is over
                // This isn't a problem and won't break any rings
                printf("\x1b[33[!] Client (%s:%d) disconnected before joining the ring\033[m\n", node_ip, node_port);
                if (ni->pred_fd >= 0)
                    close(ni->pred_fd);
                ni->pred_fd = -1;
                reset_pmt(&buffer_size, &ni->temp_fd);
                return 0;
            }
            else if (result > 0) {
                // An error occurred
                if (ni->pred_fd >= 0)
                    close(ni->pred_fd);
                ni->pred_fd = -1;
                reset_pmt(&buffer_size, &ni->temp_fd);
                return -1;
            }
        }
    }
    else {
        if (ni->succ_fd == -1) {
            // Ring was temporarily opened
            puts("\x1b[32m[*] Ring status has been restored\033[m");
        }
    }

    // Set this node's successor to be the current connection
    if (ni->succ_fd != -1)
        close(ni->succ_fd);
    ni->succ_fd = ni->temp_fd;
    ni->succ_id = node_i;
    strcpy(ni->succ_ip, node_ip);
    ni->succ_port = node_port;

    if (copy_conn_info(&ni->successor, ni->temp) != 0)
        return -1;

    reset_conn_buffer(ni->temp);   
    ni->temp_fd = -1;

    if (redestribute_objects(ni) < 0)
        return -1;

    return 0;
}

int process_message_udp(t_nodeinfo *ni)
{
    struct addrinfo sender;
    struct sockaddr_in addr;
    sender.ai_addr = (struct sockaddr*) &addr;
    sender.ai_addrlen = sizeof(addr);

    char buffer[64] = "";
    
    ssize_t recvd_bytes = recvfrom(ni->udp_fd, buffer, sizeof(buffer) - 1, 0, sender.ai_addr, &sender.ai_addrlen);
    if (recvd_bytes == -1) {
        puts("\x1b[31m[!] Error in recvfrom\033[m");
        return -1;
    }
    if (recvd_bytes >= 63) {
        // Invalid message
        puts("\x1b[33m[!] Received invalid UDP message\033[m");
    }
    buffer[recvd_bytes] = '\0';

    if (strcmp(buffer, "ACK") == 0) {
        t_ongoing_udp_message *msg = pop_udp_message_from(ni, sender.ai_addr);
        if (msg == NULL) {
            // Random ACK message??
            puts("\x1b[33m[!] Received an unprompted \"ACK\" message");
        }
        // Received an ACK and so removed message from list
        puts("[*] Received ACK message, removing from list");
        free_udp_message_list(msg);
        return 0;
    }
    else {
        if (udpsend(ni->udp_fd, "ACK", 3, &sender) != 0) {
            puts("\x1b[33[!] Error acknowledging message\033[m");
            return 0;
        }
        if (strncmp(buffer, "FND ", 4) == 0) {
            strcat(buffer, "\n");
            process_fnd_message(buffer, recvd_bytes+1, ni);
            return 0;
        }
        else if (strncmp(buffer, "RSP ", 4) == 0) {
            strcat(buffer, "\n");
            process_rsp_message(buffer, recvd_bytes+1, ni);
            return 0;
        }
        else if (strncmp(buffer, "GET ", 4) == 0) {
            strcat(buffer, "\n");
            process_get_message(buffer, recvd_bytes+1, ni);
            return 0;
        }
        else if (strncmp(buffer, "SET ", 4) == 0) {
            strcat(buffer, "\n");
            process_set_message(buffer, recvd_bytes+1, 0, ni);
            return 0;
        }
        else if (strncmp(buffer, "RGET ", 5) == 0) {
            strcat(buffer, "\n");
            process_rget_message(buffer, recvd_bytes+1, ni);
            return 0;
        }
        else if (strncmp(buffer, "EFND ", 5) == 0) {
            unsigned int key;
            if ((sscanf(buffer+5, "%u", &key) != 1) || key > 31) {
                // Malformatted message
                puts("\x1b[33[!] Received malformatted \"EFND\" message\033[m");
                return 0;
            }

            if (register_request(ni->find_n, key, &sender, ni) < 0) {
                puts("\x1b[33[!] Find request queue is full\033[m");
                return 0;
            }

            if ((ni->succ_id == ni->key && ni->pred_id == ni->key) || (ni->succ_id && ring_distance(ni->key, key) <= ring_distance(ni->key, ni->succ_id))) {
                unsigned int self_port;
                sscanf(ni->self_port, "%u", &self_port);
                if (process_found_key(ni->key, ni->find_n, ni->ipaddr, self_port, ni) != 0)
                    return -1;
                return 0;
            }

            char message[64] = "";
            sprintf(message, "FND %u %u %u %s %s\n", key, ni->find_n, ni->key, ni->ipaddr, ni->self_port);
            
            int result = send_to_closest(message, key, ni);
            if (result < 0) {
                drop_request(ni->find_n, ni);
                return 0;
            }

            ni->find_n++;
            ni->find_n %= 100;
            return 0;
        }
        else if (strncmp(buffer, "EPRED ", 6) == 0) {
            unsigned int key, port;
            char ipaddr[INET_ADDRSTRLEN] = "";
            t_msginfotype mi = get_self_or_pred_message_info(buffer+1, &key, ipaddr, &port);
            if (mi != MI_SUCCESS) {
                puts("\x1b[33[!] Received malformatted \"EPRED\" message\033[m");
                return 0;
            }
            puts("\x1b[32m[*] Received \"EPRED\" message, joining the ring\033[m");
            return join_ring(key, ipaddr, port, ni);
        }
    }

    printf("\x1b[33m[!] Received invalid UDP message: '%s'\033[m\n", buffer);

    return 0;
}