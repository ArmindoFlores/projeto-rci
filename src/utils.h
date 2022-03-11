#ifndef UTILS_H
#define UTILS_H

/**
 * @brief Converts a string to an unsigned integer
 * 
 * @param str the string to convert
 * @return [ @b unsigned @b int ] the resulting number 
 */
unsigned int strtoui(char *str);

/**
 * @brief Checks whether a string is a valid unsigned integer
 * 
 * @param str the string to check
 * @return [ @b int ] 1 if true, 0 if false 
 */
int strisui(char *str);

/**
 * @brief Checks whether a string is a valid IPv4 address
 * 
 * @param str the string to check
 * @return [ @b int ] 1 if true, 0 if false 
 */
int isipaddr(char *str);

#endif