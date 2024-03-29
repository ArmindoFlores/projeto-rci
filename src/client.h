#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

/**
 * @brief Creates a socket and uses it to connect to the specified server.
 * Also sets ni->pred_fd to the resulting socket.
 * 
 * @param addr hostname / IP of the server
 * @param port server port number
 * @param ni necessary information about the node
 * @return [ @b int ] socket file descriptor if successfull, -1 otherwise 
 */
int init_client(const char* addr, const char* port, t_nodeinfo *ni);

#endif