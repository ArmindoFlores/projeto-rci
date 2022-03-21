#ifndef SERVER_H
#define SERVER_H

#include "common.h"

/**
 * @brief Creates a new server listening on the specified port
 * 
 * @param ni necessary information about the node
 * @return [ @b int ] 0 if successfull, -1 otherwise
 */
int init_server(t_nodeinfo *ni);

/**
 * @brief Closes the server and frees associated memory
 * 
 * @param ni necessary information about the node */
void close_server(t_nodeinfo *ni);

/**
 * @brief Process an incoming connection
 * 
 * @param ni necessary information about the node
 *  @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_incoming_connection(t_nodeinfo *ni);

/**
 * @brief Process an incoming message from a temporary connection
 * 
 * @param ni necessary information about the node 
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_temp(t_nodeinfo *ni);

/**
 * @brief Process an incoming message from the UDP socket
 * 
 * @param ni necessary information about the node 
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_udp(t_nodeinfo *ni);

/**
 * @brief Process an incoming message from this node's successor
 * 
 * @param ni necessary information about the node 
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_successor(t_nodeinfo *ni);

/**
 * @brief Process an incoming message from this node's predecessor
 * 
 * @param ni necessary information about the node 
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_predecessor(t_nodeinfo *ni);

#endif