#ifndef UTILS_H
#define UTILS_H

#define _POSIX_C_SOURCE 200112L
#include "common.h"
#include <stddef.h>
#include <netdb.h>
#include <arpa/inet.h>

typedef enum msginfotype {
    MI_SUCCESS,
    MI_INVALID_PORT,
    MI_INVALID_ID,
    MI_INVALID_IP,
    MI_INVALID_K,
    MI_INVALID_N,
    MI_INVALID
} t_msginfotype;

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
 * @brief Generate address information 
 * 
 * @param ipaddr the IP address
 * @param port the port number
 * @param res structure to store the result in
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int generate_udp_addrinfo(char *ipaddr, unsigned int port, struct addrinfo **res);

/**
 * @brief Get the node identifier, IP address and port from a SELF/PRED message
 * 
 * @param message the message containing the information
 * @param node_i where to store the node identifier
 * @param node_ip where to store the node IP address
 * @param node_port where to store the node port
 * @return [ @b t_msginfotype ] type of result
 */
t_msginfotype get_self_or_pred_message_info(char *message, unsigned int *node_i, char *node_ip, unsigned int *node_port);

/**
 * @brief Get the search key/result, serial number, node identifier, IP address and port from a FND/RSP message
 * 
 * @param message the message containing the information
 * @param k where to store the search key/result
 * @param n where to store the search serial number
 * @param node_i where to store the node identifier
 * @param node_ip where to store the node IP address
 * @param node_port where to store the node port
 * @return [ @b t_msginfotype ] type of result
 */
t_msginfotype get_fnd_or_rsp_message_info(char *message, unsigned int *k, unsigned int *n, unsigned int *node_i, char *node_ip, unsigned int *node_port);

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

/**
 * @brief Create a new ring
 * 
 * @param ni necessary information about the node
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int create_ring(t_nodeinfo *ni);

/**
 * @brief Join an existing ring
 * 
 * @param pred_key key of the node's predecessor
 * @param pred_ipaddr IP address of the node's predecessor
 * @param pred_port network port of the node's predecessor
 * @param ni necessary information about the node
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int join_ring(unsigned int pred_key, char *pred_ipaddr, unsigned int pred_port, t_nodeinfo *ni);

/**
 * @brief Compare addresses a1 and a2
 * 
 * @param a1 first address to compare
 * @param a2 second address to compare
 * @return [ @b int ] 1 if both are equal, 0 otherwise
 */
int cmp_addr(struct sockaddr *a1, struct sockaddr *a2);

#endif