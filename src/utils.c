#include "utils.h"
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

t_msginfotype get_fnd_or_rsp_message_info(char *message, unsigned int *k, unsigned int *n, unsigned int *node_i, char *node_ip, unsigned int *node_port)
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