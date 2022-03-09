#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "client.h"

int main()
{
    int sockfd = init_client("localhost", "8008");
    if (sockfd == -1) {
        puts("Error initializing client!");
        exit(-1);
    }

    char text[65] = "";
    int size = recvall_delim(sockfd, text, '\n', 64);
    if (size == -1) {
        puts("Error receiving message!");
        exit(-1);
    }

    text[size-1 > 0 ? size-1 : 0] = '\0';
    printf("Received message from server: '%s' (%d bytes)\n", text, size);

    for (int i = 0; i < 3; i++) {
        int errcode = recvall(sockfd, text, 10);
        if (errcode == -1) {
            puts("Error receiving message!");
            exit(-1);
        }
        text[10] = '\0';
        printf("Received exactly 10 bytes: '%s'\n", text);
    }

    sendall(sockfd, "Hello there fellow human, human fellow!", 39);

    close(sockfd);

    puts("Done!");

    return 0;
}