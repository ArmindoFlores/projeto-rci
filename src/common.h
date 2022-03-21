#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/**
 * @brief An object that holds information about a network connection
 * 
 */
typedef struct conn_info t_conn_info;
typedef struct nodeinfo {
    // Node key
    unsigned int key;
    // TCP server's port
    char self_port[6];
    // Server IP
    char ipaddr[INET_ADDRSTRLEN];
    // Server socket file descriptor
    int main_fd;
    // Predecessor's connection's socket file descriptor (-1 if a connection does not exist)
    int pred_fd;
    // Successor's connection's socket file descriptor (-1 if a connection does not exist)
    int succ_fd;
    // Socket file descriptor of a temporary connection (-1 if a connection does not exist)
    int temp_fd;
    // Connection information pertaining to this node's predecessor (NULL if a connection does not exist)
    t_conn_info *predecessor;
    // Connection information pertaining to this node's successor (NULL if a connection does not exist)
    t_conn_info *successor;
    // Temporary connection information (NULL if a connection does not exist)
    t_conn_info *temp;
    // Predecessor IP address
    char pred_ip[INET_ADDRSTRLEN];
    // Successor IP address
    char succ_ip[INET_ADDRSTRLEN];
    // Predecessor port
    unsigned int pred_port;
    // Successor port
    unsigned int succ_port;
    // Predecessor ID
    unsigned int pred_id;
    // Successor ID
    unsigned int succ_id;
    // Search sequence number
    unsigned int n;
    // Search requests
    unsigned int *requests;
    // Number of ongoing requests
    size_t ongoing_requests;
    // Size of requests
    size_t requests_size;
} t_nodeinfo;

enum type {
    RO_SUCCESS,
    RO_ERROR,
    RO_DISCONNECT
};

typedef struct read_out {
    enum type read_type;
    size_t read_bytes;
    int error_code;
} t_read_out;

/**
 * @brief Creates a new t_conn_info object
 * 
 * @param block_size how many bytes to read at a time
 * @return [ @b t_conn_info* ] the t_conn_info object 
 */
t_conn_info *new_conn_info(int block_size);

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
 */
void set_conn_info(t_conn_info *ci, int block_size);

/**
 * @brief Checks whether there's pending data to read
 * 
 * @param ci the t_conn_info object
 * @return [ @b int ] 1 if true, 0 if false 
 */
int has_available_data(t_conn_info *ci);

/**
 * @brief Creates a new t_nodeinfo object
 * 
 * @param id the node's key
 * @param ipaddr the node's IP address
 * @param port the tcp server's port
 */
t_nodeinfo *new_nodeinfo(int id, char *ipaddr, char *port);

/**
 * @brief Returns the largest file descriptor used by the object
 * 
 * @param ni the t_nodeinfo object
 * @return int the largest used file descriptor
 */
int maxfd(t_nodeinfo *ni);

/**
 * @brief Frees a t_nodeinfo object
 * 
 * @param ni the t_nodeinfo object
 */
void free_nodeinfo(t_nodeinfo *ni);

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

/**
 * @brief Register a new "find" request
 * 
 * @param n request sequence number
 * @param key the key that's being searched
 * @param ni the t_nodeinfo object
 */
void register_request(unsigned int n, unsigned int key, t_nodeinfo *ni);

/**
 * @brief Get the key associated with a sequence number
 * 
 * @param n request sequence number
 * @param ni the t_nodeinfo object
 * @return [ @b int ] the key if it is found, -1 otherwise 
 */
int get_associated_key(unsigned int n, t_nodeinfo *ni);

/**
 * @brief Drop a "find" request
 * 
 * @param n request sequence number
 * @param ni the t_nodeinfo object
 */
void drop_request(unsigned int n, t_nodeinfo *ni);

#endif