#include "common.h"
#include <sys/socket.h>
#include <string.h>

static char *INTERNAL_BUFFER = NULL;
static size_t BUFFER_SIZE = 0;

int sendall(int sd, char *message, size_t size)
{
    size_t sent_bytes = 0;
    while (sent_bytes < size) {
        int sent = send(sd, message+sent_bytes, size-sent_bytes, 0);
        if (sent < 0)
            return sent;
        sent_bytes += sent;
    }
    return 0;
}

int recvall(int sd, char *buffer, size_t size)
{
    size_t received_bytes = 0;
    // First unload from the buffer
    if (BUFFER_SIZE) {
        received_bytes += BUFFER_SIZE > size ? size : BUFFER_SIZE;
        memcpy(buffer, INTERNAL_BUFFER, received_bytes);
        BUFFER_SIZE -= received_bytes;
        if (BUFFER_SIZE > 0) {
            // If the buffer wasn't emptied, move everything to the start
            memmove(INTERNAL_BUFFER, INTERNAL_BUFFER+received_bytes, BUFFER_SIZE);
        }
    }

    // Receive until we have gathered *size* bytes
    while (received_bytes < size) {
        int recvd = recv(sd, buffer+received_bytes, size-received_bytes, 0);
        if (recvd < 0) // Stop in case of an error
            return recvd;
        received_bytes += recvd;
    }
    return 0;
}

int recvall_delim(int sd, char *buffer, char delim, size_t max_size)
{
    int BLOCK_SIZE = 2048;
    if (!INTERNAL_BUFFER) {
        INTERNAL_BUFFER = (char*) malloc(BLOCK_SIZE * sizeof(char));
        BUFFER_SIZE = 0;
    }

    size_t received_bytes = 0;
    int recvd = BUFFER_SIZE < max_size ? BUFFER_SIZE : max_size;

    if (BUFFER_SIZE) {
        // Some data was left last time the function was run
        memcpy(buffer, INTERNAL_BUFFER, recvd);
        BUFFER_SIZE -= recvd;
        if (BUFFER_SIZE > 0) {
            // If the buffer wasn't emptied, move everything to the start
            memmove(INTERNAL_BUFFER, INTERNAL_BUFFER+recvd, BUFFER_SIZE);
        }
    }

    int found = 0;
    do {
        // Try to find the delimiter
        for (size_t offset = 0; offset < recvd; offset++) {
            if (buffer[received_bytes+offset] == delim) {
                // Found the delimiter
                if (offset < recvd-1) {
                    // We've read too much, store it
                    BUFFER_SIZE = recvd-offset-1;
                    memcpy(INTERNAL_BUFFER, buffer+received_bytes+offset+1, BUFFER_SIZE);
                    memset(buffer+received_bytes+offset+1, 0, BUFFER_SIZE);
                }
                found = 1;
                received_bytes += offset + 1;
                break;
            }
        }
        if (!found) {
            received_bytes += recvd;
            // Read more data from socket FD
            recvd = recv(sd, buffer+received_bytes, received_bytes+BLOCK_SIZE > max_size ? max_size : received_bytes+BLOCK_SIZE, 0);
            if (recvd < 0)
                return recvd;
        }
    } while (!found && received_bytes < max_size);
    return (int) received_bytes;
}