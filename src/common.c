#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <errno.h>
#define MAX(x, y) (x > y ? x : y)

struct conn_info {
    char *buffer;
    size_t buffer_size;
    int block_size;
};

t_conn_info *new_conn_info(int block_size)
{
    t_conn_info *result = (t_conn_info*) malloc(sizeof(t_conn_info)); 
    result->buffer = (char*) calloc(block_size, sizeof(char));
    result->buffer_size = 0;
    result->block_size = block_size;
    return result;
}

void free_conn_info(t_conn_info *ci)
{
    if (ci) {
        free(ci->buffer);
        free(ci);
    }
}

void set_conn_info(t_conn_info *ci, int block_size)
{
    if (ci->block_size != block_size) {
        free(ci->buffer);
        ci->buffer = (char*) calloc(block_size, sizeof(char));
    }

    ci->buffer_size = 0;
    ci->block_size = block_size;
}

int has_available_data(t_conn_info *ci)
{
    return ci->buffer_size > 0;
}

void copy_conn_info(t_conn_info **dest, t_conn_info *src)
{
    if (*dest == NULL) {
        // If destination is not initialize, do it
        *dest = new_conn_info(src->block_size);
    }
    else {
        if ((*dest)->block_size != src->block_size) {
            // If both objects have different block_sizes, allocate a new buffer to match
            free((*dest)->buffer);
            (*dest)->buffer = (char*) calloc(src->block_size, sizeof(char));
        }
        (*dest)->block_size = src->block_size;
    }

    (*dest)->buffer_size = src->buffer_size;
    if (src->buffer_size)
        memcpy((*dest)->buffer, src->buffer, src->buffer_size);  // Copy the buffer
}

void reset_conn_buffer(t_conn_info* ci)
{
    ci->buffer_size = 0;
}

t_nodeinfo *new_nodeinfo(int id, char *ipaddr, char *port)
{
    t_nodeinfo *ni = (t_nodeinfo*) malloc(sizeof(t_nodeinfo));
    ni->key = id;
    strncpy(ni->self_port, port, sizeof(ni->self_port)-1);
    strncpy(ni->ipaddr, ipaddr, sizeof(ni->ipaddr)-1);
    ni->main_fd = -1;
    ni->pred_fd = -1;
    ni->succ_fd = -1;
    ni->temp_fd = -1;
    ni->predecessor = NULL;
    ni->successor = NULL;
    ni->temp = NULL;
    memset(ni->pred_ip, 0, sizeof(ni->pred_ip));
    memset(ni->succ_ip, 0, sizeof(ni->succ_ip));
    ni->pred_port = 0;
    ni->succ_port = 0;
    ni->pred_id = 0;
    ni->succ_id = 0;
    ni->n = 0;
    return ni;
}

int maxfd(t_nodeinfo *si)
{
    int mx = MAX(si->main_fd, si->pred_fd);
    mx = MAX(mx, si->succ_fd);
    mx = MAX(mx, si->temp_fd);
    return mx;
}

void free_nodeinfo(t_nodeinfo *ni)
{
    if (ni) {
        free_conn_info(ni->predecessor);
        free_conn_info(ni->successor);
        free_conn_info(ni->temp);
        free(ni);
    }
}

int sendall(int sd, char *message, size_t size)
{
    char *buf = (char*) malloc(size + 1);
    memcpy(buf, message, size);
    buf[size] = '\0';
    printf("\x1b[34m[+] %s\033[m\n", buf);
    free(buf);
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