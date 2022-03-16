#ifndef USER_H
#define USER_H

#include "common.h"

/**
 * @brief Process a user-sent message
 * 
 * @param ni necessary information about the node
 * @return [ @b int ] 0 if successfull, -1 otherwise 
 */
int process_user_message(t_nodeinfo *ni);

#endif