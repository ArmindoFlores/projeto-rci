#define _POSIX_C_SOURCE 200112L
#include "common.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#define MAX(x, y) (x > y ? x : y)

struct conn_info {
    char *buffer;
    size_t buffer_size;
    int block_size;
};

t_conn_info *new_conn_info(int block_size)
{
    t_conn_info *result = (t_conn_info*) malloc(sizeof(t_conn_info)); 
    if (result == NULL)
        return NULL;
    result->buffer = (char*) calloc(block_size, sizeof(char));
    if (result->buffer == NULL)
        return NULL;
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

int set_conn_info(t_conn_info *ci, int block_size)
{
    if (ci->block_size != block_size) {
        free(ci->buffer);
        ci->buffer = (char*) calloc(block_size, sizeof(char));
        if (ci->buffer == NULL)
            return -1;
    }

    ci->buffer_size = 0;
    ci->block_size = block_size;

    return 0;
}

int has_available_data(t_conn_info *ci)
{
    return ci->buffer_size > 0;
}

int copy_conn_info(t_conn_info **dest, t_conn_info *src)
{
    if (*dest == NULL) {
        // If destination is not initialize, do it
        *dest = new_conn_info(src->block_size);
        if (*dest == NULL)
            return -1;
    }
    else {
        if ((*dest)->block_size != src->block_size) {
            // If both objects have different block_sizes, allocate a new buffer to match
            free((*dest)->buffer);
            (*dest)->buffer = (char*) calloc(src->block_size, sizeof(char));
            if ((*dest)->buffer == NULL)
                return -1;
        }
        (*dest)->block_size = src->block_size;
    }

    (*dest)->buffer_size = src->buffer_size;
    if (src->buffer_size)
        memcpy((*dest)->buffer, src->buffer, src->buffer_size);  // Copy the buffer
    return 0;
}

void reset_conn_buffer(t_conn_info* ci)
{
    ci->buffer_size = 0;
}

t_nodeinfo *new_nodeinfo(int id, char *ipaddr, char *port)
{
    t_nodeinfo *ni = (t_nodeinfo*) calloc(1, sizeof(t_nodeinfo));
    if (ni == NULL)
        return NULL;
    ni->key = id;
    strncpy(ni->self_port, port, sizeof(ni->self_port)-1);
    strncpy(ni->ipaddr, ipaddr, sizeof(ni->ipaddr)-1);
    ni->main_fd = -1;
    ni->udp_fd = -1;
    ni->pred_fd = -1;
    ni->succ_fd = -1;
    ni->temp_fd = -1;
    ni->predecessor = NULL;
    ni->successor = NULL;
    ni->temp = NULL;
    memset(ni->pred_ip, 0, sizeof(ni->pred_ip));
    memset(ni->succ_ip, 0, sizeof(ni->succ_ip));
    memset(ni->shcut_ip, 0, sizeof(ni->shcut_ip));
    ni->pred_port = 0;
    ni->succ_port = 0;
    ni->shcut_port = 0;
    ni->pred_id = 0;
    ni->succ_id = 0;
    ni->shcut_id = 0;
    ni->find_n = 0;
    for (size_t i = 0; i < sizeof(ni->requests) / sizeof(int); i++)
        ni->requests[i] = -1;
    memset(ni->request_addr, 0, sizeof(ni->request_addr));
    memset(ni->request_addr_len, 0, sizeof(ni->request_addr_len));
    ni->shcut_info = NULL;
    ni->udp_message_list = NULL;
    return ni;
}

int register_request(unsigned int n, unsigned int key, struct addrinfo *info, t_nodeinfo *ni)
{
    if (n >= sizeof(ni->requests) / sizeof(int))
        return -1;
    if (ni->requests[n] != -1)
        return -1;

    ni->requests[n] = key;
    if (info) {
        memcpy(&ni->request_addr[n], info->ai_addr, sizeof(struct addrinfo));
        ni->request_addr_len[n] = info->ai_addrlen;
    }
    return 0;
}

int get_associated_key(unsigned int n, t_nodeinfo *ni)
{
    if (n >= sizeof(ni->requests) / sizeof(int))
        return -1;
    return ni->requests[n];
}

int get_associated_addrinfo(unsigned int n, struct sockaddr *dest, socklen_t *dest_len, t_nodeinfo *ni)
{
    if (n >= sizeof(ni->requests) / sizeof(int) || ni->request_addr_len[n] == 0)
        return -1;
    *dest_len= ni->request_addr_len[n];
    memcpy(dest, &ni->request_addr[n], sizeof(struct sockaddr));
    return 0;
}

void drop_request(unsigned int n, t_nodeinfo *ni)
{
    if (n < sizeof(ni->requests) / sizeof(int)) {
        ni->requests[n] = -1;
        ni->request_addr_len[n] = 0;
    }
}

int maxfd(t_nodeinfo *si)
{
    int mx = MAX(si->main_fd, si->pred_fd);
    mx = MAX(mx, si->succ_fd);
    mx = MAX(mx, si->temp_fd);
    mx = MAX(mx, si->udp_fd);
    return mx;
}

int register_udp_message(t_nodeinfo *ni, char *message, size_t size, struct sockaddr *recipient, socklen_t recipient_size, t_udp_message_type msgtype)
{
    t_ongoing_udp_message *aux = NULL, *prev = NULL;
    for (aux = ni->udp_message_list; aux != NULL; prev = aux, aux = aux->next) {
        if (cmp_addr(&aux->recipient, recipient)) {
            // There is already an ongoing message to this recipient
            return 1;
        }
    }
    aux = prev;
    if (aux == NULL) {
        ni->udp_message_list = (t_ongoing_udp_message*) malloc(sizeof(t_ongoing_udp_message));
        if (ni->udp_message_list == NULL)
            return -1;
        aux = ni->udp_message_list;
    }
    else {
        aux->next = (t_ongoing_udp_message*) malloc(sizeof(t_ongoing_udp_message));
        if (aux->next == NULL)
            return -1;
        aux = aux->next;
    }
    gettimeofday(&aux->timestamp, NULL);
    aux->nretries = 3;
    memcpy(aux->body, message, size);
    aux->length = size;
    memcpy(&aux->recipient, recipient, sizeof(aux->recipient));
    aux->recipient_size = recipient_size;
    aux->type = msgtype;
    aux->next = NULL;
    return 0;
}

t_ongoing_udp_message *pop_udp_message_from(t_nodeinfo *ni, struct sockaddr *recipient)
{
    for (t_ongoing_udp_message *aux = ni->udp_message_list, *prev = NULL; aux != NULL; prev = aux, aux = aux->next) {
        if (cmp_addr(&aux->recipient, recipient)) {
            if (prev != NULL)
                prev->next = aux->next;
            else 
                ni->udp_message_list = aux->next;
            aux->next = NULL;
            return aux;
        }
    }
    return NULL;
}

char *get_object(unsigned int key, t_nodeinfo* ni)
{
    if (key < 32)
        return ni->objects[key];
    return NULL;
}

int set_object(unsigned int key, char *value, t_nodeinfo *ni)
{
    if (key >= 32)
        return 1;

    if (ni->objects[key] != NULL)
        free(ni->objects[key]);

    if (value == NULL)
        ni->objects[key] = NULL;
    else {
        ni->objects[key] = (char*) calloc(strlen(value)+1, sizeof(char));
        if (ni->objects[key] == NULL)
            return -1;
        strcpy(ni->objects[key], value);
    }

    return 0;
}

t_ongoing_udp_message *find_udp_message_from(t_nodeinfo *ni, struct sockaddr *recipient)
{
    for (t_ongoing_udp_message *aux = ni->udp_message_list; aux != NULL; aux = aux->next) {
        if (cmp_addr(&aux->recipient, recipient))
            return aux;
    }
    return NULL;
}

void free_nodeinfo(t_nodeinfo *ni)
{
    if (ni) {
        free_conn_info(ni->predecessor);
        free_conn_info(ni->successor);
        free_conn_info(ni->temp);
        if (ni->shcut_info != NULL)
            freeaddrinfo(ni->shcut_info);
        free_udp_message_list(ni->udp_message_list);
        for (unsigned int i = 0; i < 32; i++) 
            free(ni->objects[i]);
        free(ni);
    }
}

int sendall(int sd, char *message, size_t size)
{
    size_t sent_bytes = 0;
    while (sent_bytes < size) {
        ssize_t sent = send(sd, message+sent_bytes, size-sent_bytes, 0);
        if (sent < 0)
            return sent;
        if (sent == 0)
            return -1;
        sent_bytes += sent;
    }
    return 0;
}

int udpsend(int sd, char *message, size_t size, struct addrinfo *to)
{
    size_t sent_bytes = 0;
    while (sent_bytes < size) {
        ssize_t sent = sendto(sd, message+sent_bytes, size-sent_bytes, 0, to->ai_addr, to->ai_addrlen);
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
        buffer[actual_size] = '\0';

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

void close_sockets(t_nodeinfo *ni)
{
    if (ni->main_fd >= 0)
        close(ni->main_fd);
    if (ni->pred_fd >= 0)
        close(ni->pred_fd);
    if (ni->succ_fd >= 0)
        close(ni->succ_fd);
    if (ni->temp_fd >= 0)
        close(ni->temp_fd);
    if (ni->udp_fd >= 0)
        close(ni->udp_fd);
}

void free_udp_message_list(t_ongoing_udp_message *oum)
{
    free(oum);
}