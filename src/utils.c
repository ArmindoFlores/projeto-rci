#include "utils.h"
#include <string.h>


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

t_msginfo get_message_info(char *message, size_t message_size)
{
    t_msginfo result;
    memset(&result, 0, sizeof(result));

    // Pointer to the ' ' character before the node identifier
    char *i_start = strchr(message, ' ');
    if (i_start == NULL)  {
        result.type = MI_NO_ID;
        return result;
    }

    // Pointer to the ' ' character before the node IP
    char *ip_start = strchr(i_start+1, ' ');
    if (ip_start == NULL) {
        // Message is invalid
        result.type = MI_NO_IP;
        return result;
    }
    // Pointer to the ' ' character before the node port
    char *port_start = strchr(ip_start+1, ' ');
    if (port_start == NULL) {
        // Message is invalid
        result.type = MI_NO_PORT;
        return result;
    }

    // This buffer will be used to convert strings to other data types
    char convert_buffer[64];

    // Find node_i
    memcpy(convert_buffer, i_start+1, ip_start-i_start-1);
    convert_buffer[ip_start-i_start-1] = '\0';
    if (!strisui(convert_buffer)) {
        // Message is invalid
        result.type = MI_INVALID_ID;
        return result;
    }
    // Node identifier
    result.node_i = strtoui(convert_buffer);
    if (result.node_i > 32) {
        result.type = MI_INVALID_ID;
        return result;
    }

    // Find node_ip
    memcpy(convert_buffer, ip_start+1, port_start-ip_start-1);
    convert_buffer[port_start-ip_start-1] = '\0';
    if (!isipaddr(convert_buffer)) {
        // Message is invalid
        result.type = MI_INVALID_IP;
        return result;
    }
    // Node IP address
    strcpy(result.node_ip, convert_buffer);

    // Find node_port
    memcpy(convert_buffer, port_start+1, message_size-(int)(port_start-message)-2);
    convert_buffer[message_size-(int)(port_start-message)-2] = '\0';
    if (!strisui(convert_buffer)) {
        // Message is invalid
        result.type = MI_INVALID_PORT;
        return result;
    }
    // Node port
    result.node_port = strtoui(convert_buffer);
    if (result.node_port > 65535) {
        // Message is invalid
        result.type = MI_INVALID_PORT;
        return result;
    }

    result.type = MI_SUCCESS;
    return result;
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