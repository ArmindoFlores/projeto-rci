#ifndef SERVER_H
#define SERVER_H

#include "common.h"

typedef struct error_or_nodeinfo t_error_or_nodeinfo;
typedef enum event {
    E_INCOMING_CONNECTION,
    E_MESSAGE_PREDECESSOR,
    E_MESSAGE_SUCCESSOR,
    E_MESSAGE_TEMP,
    E_ERROR
} t_event;

/**
 * @brief Verifies if the struct contains an error
 * 
 * @param esi the t_error_or_nodeinfo structure
 * @return [ @b int ] 1 if true, 0 if false 
 */
int is_error(t_error_or_nodeinfo *esi);

/**
 * @brief Get the nodeinfo object from a t_error_or_nodeinfo structure
 * 
 * @param esi the t_error_or_nodeinfo structure
 * @return [ @b t_nodeinfo* ] the t_nodeinfo object
 */
t_nodeinfo* get_nodeinfo(t_error_or_nodeinfo *esi);

/**
 * @brief Creates a new server listening on the specified port
 * 
 * @param port the port to listen on
 * @return [ @b t_error_or_nodeinfo ] if !is_error(&result), then get_nodeinfo(&result)
 * contains all information about the server
 */
t_error_or_nodeinfo* init_server(const char* port);

/**
 * @brief Blocks until an event occurrs, then returns it
 * 
 * @param si necessary information about the server
 * @return [ @b t_event ] what event has occurred
 */
t_event select_event(t_nodeinfo* si);

/**
 * @brief Closes the server and frees associated memory
 * 
 * @param si necessary information about the server
 */
void close_server(t_nodeinfo* si);

/**
 * @brief Process an incoming connection
 * 
 * @param si necessary information about the server
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_incoming_connection(t_nodeinfo *si);

/**
 * @brief Process an incoming message from a temporary connection
 * 
 * @param si necessary information about the server
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_temp(t_nodeinfo *si);

/**
 * @brief Process an incoming message from this node's successor
 * 
 * @param si necessary information about the server
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_message_successor(t_nodeinfo *si);

#endif