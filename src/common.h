#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <sys/socket.h>

/**
 * @brief An object that holds information about a network connection
 * 
 */
typedef struct conn_info t_conn_info;

typedef struct read_out {
    enum type {
        RO_SUCCESS,
        RO_ERROR,
        RO_DISCONNECT
    } read_type;
    size_t read_bytes;
    int error_code;
} t_read_out;

/**
 * @brief Creates a new t_conn_info object
 * 
 * @param block_size how many bytes to read at a time
 * @param addr connection address
 * @param addrlen lenth of @b addr
 * @return [ @b t_conn_info* ] the t_conn_info object 
 */
t_conn_info *new_conn_info(int block_size, struct sockaddr addr, socklen_t addrlen);

/**
 * @brief Copies src into dest
 * 
 * @param dest destination object
 * @param src source object
 */
void copy_conn_info(t_conn_info **dest, t_conn_info *src);

/**
 * @brief Reset this connection's internal buffer
 * 
 * @param ci the t_conn_info object
 */
void reset_conn_buffer(t_conn_info* ci);

/**
 * @brief Frees a t_conn_info object
 * 
 * @param ci the t_conn_info object
 */
void free_conn_info(t_conn_info *ci);

/**
 * @brief Set properties of the t_conn_info object
 * 
 * @param ci the t_conn_info object
 * @param block_size how many bytes to read at a time
 * @param addr connection address
 * @param addrlen length of @b addr
 */
void set_conn_info(t_conn_info *ci, int block_size, struct sockaddr addr, socklen_t addrlen);

/**
 * @brief Checks whether there's pending data to read
 * 
 * @param ci the t_conn_info object
 * @return [ @b int ] 1 if true, 0 if false 
 */
int has_available_data(t_conn_info *ci);

/**
 * @brief Send an entire message through a socket
 * 
 * @param sd socket file descriptor
 * @param message message to be sent
 * @param size size of the message in bytes
 * @return [ @b int ] 0 if successfull, -1 if the connection was closed, and
 * the number of bytes sent if another error occurred
 */
int sendall(int sd, char *message, size_t size);

/**
 * @brief Receive a message through a socket
 * 
 * @param sd socket file descriptor
 * @param buffer buffer to store the message
 * @param delim delimitor character
 * @param max_size size of the buffer
 * @param ci necessary information about the connection
 * @return [ @b t_read_out ] structure describing the result
 */
t_read_out recv_message(int sd, char *buffer, char delim, size_t max_size, t_conn_info *ci);

#endif