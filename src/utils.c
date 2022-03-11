#include "utils.h"
#include <arpa/inet.h>
#include <string.h>

unsigned int strtoui(char *str)
{
    size_t len = strlen(str);
    unsigned int result = 0;
    unsigned int power = 1;
    for (size_t i = 0; i < len; i++, power *= 10)
        result += (unsigned int)(str[len-i-1] - '0') * power;
    return result;
}

int strisui(char *str)
{
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] > '9' || str[i] < '0')
            return 0;
    }
    return 1;
}

int isipaddr(char *str)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, str, &(sa.sin_addr));
    return result > 0;
}