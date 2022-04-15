#include "utils.h"
#include "server.h"
#include "client.h"
#include <string.h>
#include <stdio.h>
#include <netdb.h>

unsigned int strtoui(const char *str)
{
    size_t len = strlen(str);
    unsigned int result = 0;
    unsigned int power = 1;
    for (size_t i = 0; i < len; i++, power *= 10)
        result += (unsigned int)(str[len-i-1] - '0') * power;
    return result;
}

int strisui(const char *str)
{
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] > '9' || str[i] < '0')
            return 0;
    }
    return 1;
}

int isipaddr(const char *str)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, str, &(sa.sin_addr));
    return result > 0;
}

int generate_udp_addrinfo(char *ipaddr, unsigned int port, struct addrinfo **res)
{
    struct addrinfo hints;
    char port_str[6] = "";
    sprintf(port_str, "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if(getaddrinfo(ipaddr, port_str, &hints, res) != 0)
        return -1;
    return 0;
}

t_msginfotype get_self_or_pred_message_info(char *message, unsigned int *node_i, char *node_ip, unsigned int *node_port)
{
    if (sscanf(message+5, "%u %15s %u", node_i, node_ip, node_port) != 3) {
        // Invalid message
        return MI_INVALID;
    }

    if (*node_i > 32) {
        // Node key is invalid
        return MI_INVALID_ID;
    }

    if (*node_port > 65535) {
        // Message is invalid
        return MI_INVALID_PORT;
    }

    if (!isipaddr(node_ip)) {
        // IP address is invalid
        return MI_INVALID_IP;
    }

    return MI_SUCCESS;
}

t_msginfotype get_fnd_or_rsp_or_get_message_info(char *message, unsigned int *k, unsigned int *n, unsigned int *node_i, char *node_ip, unsigned int *node_port)
{
    if (sscanf(message+4, "%u %u %u %15s %u", k, n, node_i, node_ip, node_port) != 5) {
        // Invalid message
        return MI_INVALID;
    }

    if (*k > 32) {
        // Search key / result is invalid
        return MI_INVALID_K;
    }

    if (*n > 99) {
        // Serial number is invalid
        return MI_INVALID_N;
    }

    if (*node_i > 32) {
        // Node key is invalid
        return MI_INVALID_ID;
    }

    if (*node_port > 65535) {
        // Message is invalid
        return MI_INVALID_PORT;
    }

    if (!isipaddr(node_ip)) {
        // IP address is invalid
        return MI_INVALID_IP;
    }

    return MI_SUCCESS;
}

t_msginfotype get_rget_or_set_message_info(char *message, unsigned int *k, unsigned int *n, unsigned int *node_i, char *value)
{
    if (sscanf(message, "%u %u %u %16[^\n]", k, n, node_i, value) != 4) {
        if (sscanf(message, "%u %u %u", k, n, node_i) != 3) {
            // Invalid message
            return MI_INVALID;
        }
        else
            value[0] = '\0';
    }

    if (*k > 32) {
        // Search key / result is invalid
        return MI_INVALID_K;
    }

    if (*n > 99) {
        // Serial number is invalid
        return MI_INVALID_N;
    }

    if (*node_i > 32) {
        // Node key is invalid
        return MI_INVALID_ID;
    }

    return MI_SUCCESS;
}

void ipaddr_from_sockaddr(struct sockaddr *sa, char *dest)
{
    struct in_addr addr = ((struct sockaddr_in*)sa)->sin_addr;
    inet_ntop(AF_INET, &addr, dest, INET_ADDRSTRLEN);
}

unsigned int ring_distance(unsigned int key1, unsigned int key2)
{
    // This works because both key1 and key2 are unsigned integers, so 
    // key2 - key1 wraps around and thus the result is always correct
    return (key2 - key1) % 32;
}

int create_ring(t_nodeinfo *ni)
{
    // Create server
    int result = init_server(ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return -1;
    }

    // Connect to self
    result = init_client(ni->ipaddr, ni->self_port, ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return 0;
    }
    ni->pred_id = ni->key;

    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);

    // Accept our own connection
    ni->succ_fd = accept(ni->main_fd, &addr, &addrlen);
    if (ni->succ_fd == -1) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return 0;
    }
    if (ni->successor == NULL) {
        ni->successor = new_conn_info(2048);
        if (ni->successor == NULL)
            return -1;
    }
    else {
        if (set_conn_info(ni->successor, 2048) != 0)
            return -1;
    }
    ni->succ_id = ni->key;
    strcpy(ni->succ_ip, ni->ipaddr);
    sscanf(ni->self_port, "%u", &ni->succ_port);

    return 0;
}

int join_ring(unsigned int pred_key, char *pred_ipaddr, unsigned int pred_port, t_nodeinfo *ni)
{
    char portstr[6] = "";
    snprintf(portstr, sizeof(portstr), "%d", pred_port);
    int result = init_client(pred_ipaddr, portstr, ni);

    if (result == -1)
        return -1;

    char message[64] = "";
    sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->self_port);
    if (sendall(ni->pred_fd, message, strlen(message)) != 0) {
        puts("\x1b[31m[!] Error sending message to predecessor\033[m");
        return -1;
    }

    ni->pred_id = pred_key;
    
    return 0;
}

int cmp_addr(struct sockaddr *a1, struct sockaddr *a2)
{
    struct sockaddr_in *addr1 = (struct sockaddr_in*) a1, *addr2 = (struct sockaddr_in*) a2;
    return addr1->sin_addr.s_addr == addr2->sin_addr.s_addr
        && addr1->sin_port == addr2->sin_port;
}