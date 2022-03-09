#define _POSIX_C_SOURCE 200112L
#include "client.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>

int init_client(const char* addr, const char* port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) 
        return -1;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int errcode = getaddrinfo(addr, port, &hints, &res);
    if (errcode != 0)
        return -1;

    errcode = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if (errcode != 0) 
        return -1;

    return sockfd;
}