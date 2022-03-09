#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>

/**
 * @brief Send an entire message through a socket
 * 
 * @param sd socket file descriptor
 * @param message message to be sent
 * @param size size of the message in bytes
 * @return [ @b int ] 0 if successfull, -1 otherwise
 */
int sendall(int sd, char *message, size_t size);

/**
 * @brief Receive a number of bytes through a socket
 * 
 * @param sd socket file descriptor
 * @param buffer buffer to store the message
 * @param size size of the message to be received
 * @return [ @b int ] 0 if successfull, -1 otherwise
 */
int recvall(int sd, char *buffer, size_t size);

/**
 * @brief Receive a message through a socket until a delimitor character is reached
 * 
 * @param sd socket file descriptor
 * @param buffer buffer to store the message
 * @param delim delimitor character
 * @param max_size size of the buffer
 * @return [ @b int ] number of bytes read if successfull, -1 otherwise
 */
int recvall_delim(int sd, char *buffer, char delim, size_t max_size);

#endif