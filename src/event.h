#ifndef EVENT_H
#define EVENT_H

#include "common.h"

typedef enum event {
    E_INCOMING_CONNECTION,
    E_MESSAGE_PREDECESSOR,
    E_MESSAGE_SUCCESSOR,
    E_MESSAGE_TEMP,
    E_MESSAGE_USER,
    E_TIMEOUT,
    E_ERROR
} t_event;

/**
 * @brief Blocks until an event occurrs, then returns it
 * 
 * @param ni necessary information about the node 
 * @return [ @b t_event ] what event has occurred
 */
t_event select_event(t_nodeinfo *ni);

#endif