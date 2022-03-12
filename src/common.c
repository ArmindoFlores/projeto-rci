#include "common.h"
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct conn_info {
    char *buffer;
    size_t buffer_size;
    int block_size;
    struct sockaddr addr;
    socklen_t addrlen;
};

t_conn_info *new_conn_info(int block_size, struct sockaddr addr, socklen_t addrlen)
{
    t_conn_info *result = (t_conn_info*) malloc(sizeof(t_conn_info)); 
    result->buffer = (char*) calloc(block_size, sizeof(char));
    result->buffer_size = 0;
    result->block_size = block_size;
    result->addr = addr;
    result->addrlen = addrlen;
    return result;
}

void free_conn_info(t_conn_info *ci)
{
    free(ci->buffer);
    free(ci);
}

void set_conn_info(t_conn_info *ci, int block_size, struct sockaddr addr, socklen_t addrlen)
{
    if (ci->block_size != block_size) {
        free(ci->buffer);
        ci->buffer = (char*) calloc(block_size, sizeof(char));
    }

    ci->buffer_size = 0;
    ci->block_size = block_size;
    ci->addr = addr;
    ci->addrlen = addrlen;
}

int has_available_data(t_conn_info *ci)
{
    return ci->buffer_size > 0;
}

void copy_conn_info(t_conn_info **dest, t_conn_info *src)
{
    if (*dest == NULL) {
        // If destination is not initialize, do it
        *dest = new_conn_info(src->block_size, src->addr, src->addrlen);
    }
    else {
        if ((*dest)->block_size != src->block_size) {
            // If both objects have different block_sizes, allocate a new buffer to match
            free((*dest)->buffer);
            (*dest)->buffer = (char*) calloc(src->block_size, sizeof(char));
        }
        (*dest)->block_size = src->block_size;
        (*dest)->addr = src->addr;
        (*dest)->addrlen = src->addrlen;
    }

    (*dest)->buffer_size = src->buffer_size;
    if (src->buffer_size)
        memcpy((*dest)->buffer, src->buffer, src->buffer_size);  // Copy the buffer
}

void reset_conn_buffer(t_conn_info* ci)
{
    ci->buffer_size = 0;
}

int sendall(int sd, char *message, size_t size)
{
    size_t sent_bytes = 0;
    while (sent_bytes < size) {
        int sent = send(sd, message+sent_bytes, size-sent_bytes, 0);
        if (sent < 0)
            return sent;
        if (sent == 0)
            return -1;
        sent_bytes += sent;
    }
    return 0;
}

t_read_out recv_message(int sd, char *buffer, char delim, size_t max_size, t_conn_info* ci)
{
    t_read_out result;
    memset(&result, 0, sizeof(result));
    result.read_type = RO_ERROR;

    if (ci->buffer_size) {
        // There are still some bytes left in the buffer
        char *delim_pos = (char*) memchr(ci->buffer, delim, ci->buffer_size);
        if (delim_pos == NULL) // Not a full message
            delim_pos = ci->buffer + ci->buffer_size - 1;
        
        // Calculate size and copy remaining data to user buffer
        size_t size = (delim_pos - ci->buffer) + 1;
        size_t actual_size = size > max_size ? max_size : size;
        memcpy(buffer, ci->buffer, actual_size);

        if (actual_size < ci->buffer_size) {
            // If the buffer wasn't emptied, move everything to the beginning
            memmove(ci->buffer, ci->buffer+actual_size, ci->buffer_size-actual_size);
        }
        ci->buffer_size -= actual_size;

        result.read_type = RO_SUCCESS;
        result.read_bytes = actual_size;
        return result;
    }

    // No bytes left in the buffer, read from the socket
    ssize_t recvd = recv(sd, buffer, ci->block_size > max_size ? max_size : ci->block_size, 0);
    if (recvd == 0) {
        // Client disconnected
        result.read_type = RO_DISCONNECT;
        return result;
    }
    else if (recvd < 0) {
        // An error occurred while reading
        result.error_code = errno;
        return result;
    }

    // Try to find the message delimiter
    char *delim_pos = (char*) memchr(buffer, delim, recvd);
    if (delim_pos == NULL) {
        // Not a full message
        result.read_bytes = recvd;
    }
    else {
        // Full message, possibly more
        if (delim_pos - buffer > 0) {
            // There's more data after the message, copy it to the buffer
            result.read_bytes = (size_t)(delim_pos-buffer) + 1;
            memcpy(ci->buffer, delim_pos+1, recvd-result.read_bytes);
            // We wrote too much to the user buffer, clear it
            memset(delim_pos+1, 0, recvd-result.read_bytes);
            ci->buffer_size = recvd-result.read_bytes;
        }
        else {
            result.read_bytes = recvd;
        }
    }

    result.read_type = RO_SUCCESS;
    return result;
}