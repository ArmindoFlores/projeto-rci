#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <arpa/inet.h>

enum msginfotype {
    MI_SUCCESS,
    MI_INVALID_PORT,
    MI_INVALID_ID,
    MI_INVALID_IP,
    MI_NO_PORT,
    MI_NO_ID,
    MI_NO_IP
};

typedef struct msginfo {
    // Node identifier
    unsigned int node_i;
    // Node port
    unsigned int node_port;
    // Node IP address
    char node_ip[INET_ADDRSTRLEN];
    // Type of the message
    enum msginfotype type;
} t_msginfo;

/**
 * @brief Converts a string to an unsigned integer
 * 
 * @param str the string to convert
 * @return [ @b unsigned @b int ] the resulting number 
 */
unsigned int strtoui(const char *str);

/**
 * @brief Checks whether a string is a valid unsigned integer
 * 
 * @param str the string to check
 * @return [ @b int ] 1 if true, 0 if false 
 */
int strisui(const char *str);

/**
 * @brief Checks whether a string is a valid IPv4 address
 * 
 * @param str the string to check
 * @return [ @b int ] 1 if true, 0 if false 
 */
int isipaddr(const char *str);

/**
 * @brief Get the node identifier, IP address and port from a SELF/PRED message
 * 
 * @param message the message containing the information
 * @param message_size size of the message
 * @return [ @b t_msginfo ] object containing the requested information 
 */
t_msginfo get_message_info(char *message, size_t message_size);

/**
 * @brief Fills @b dest with the IP address contained in @b sa
 * 
 * @param sa the struct sockaddr object 
 * @param dest destination string buffer
 */
void ipaddr_from_sockaddr(struct sockaddr *sa, char *dest);

/**
 * @brief Calculates the ring distance between key1 and key2
 * 
 * @param key1 first key
 * @param key2 second key
 */
unsigned int ring_distance(unsigned int key1, unsigned int key2);

#endif