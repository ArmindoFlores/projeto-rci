#define _POSIX_C_SOURCE 200112L
#include "user.h"
#include "server.h"
#include "client.h"
#include "utils.h"
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>

int process_command_new(t_nodeinfo *ni) {
    int result = init_server(ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        return -1;
    }
    return 0;
}

int process_command_pentry(int pred, int port, char *ipaddr, t_nodeinfo *ni)
{
    if (pred > 31 || pred < 0) {
        printf("Invalid predecessor '%d'\n", pred);
        return 0;
    }
    if (!isipaddr(ipaddr)) {
        printf("Invalid IP address '%s'\n", ipaddr);
        return 0;
    }
    if (port > 65535 || port < 0) {
        printf("Invalid port number '%d'\n", port);
        return 0;
    }

    int result = init_server(ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        return -1;
    }

    char portstr[6] = "";
    snprintf(portstr, sizeof(portstr), "%d", port);
    result = init_client(ipaddr, portstr, ni);

    if (result == -1)
        return -1;

    char message[64] = "";
    sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->self_port);
    if (sendall(ni->pred_fd, message, strlen(message)) != 0) {
        puts("\x1b[31m[!] Error sending message to predecessor\033[m");
        return -1;
    }

    ni->pred_id = pred;
    
    return 0;
}

int process_command_show(t_nodeinfo *ni)
{
    puts("\x1b[34m[*] Node Status\033[m");
        
    // Node info
    printf("This node info:\n* Key: %u\n* IP: %s\n* Port: %s\n", ni->key, ni->ipaddr, ni->self_port);

    // Node successor info
    if (ni->succ_fd == -1)
        puts("\x1b[31mThis node doesn't have a successor!\033[m");
    else
        printf("Successor node info (%d):\n* Key: %u\n* IP: %s\n* Port: %u\n", ni->succ_fd, ni->succ_id, ni->succ_ip, ni->succ_port);

    // Node predecessor info
    if (ni->pred_fd == -1)
        puts("\x1b[31mThis node doesn't have a predecessor!\033[m");
    else
        printf("Predecessor node info (%d):\n* Key: %u\n* IP: %s\n* Port: %u\n", ni->pred_fd, ni->pred_id, ni->pred_ip, ni->pred_port);

    // TODO (iv) Shortcut info
    return 0;
}

int process_command_leave(t_nodeinfo *ni)
{
    // Node is the only one on the ring
    if (ni->succ_fd == -1)
        return 1;

    if (ni->pred_fd != -1)
        close(ni->pred_fd);

    char message[64] = "";
    sprintf(message, "PRED %u %s %u\n", ni->pred_id, ni->pred_ip, ni->pred_port);
    int result = sendall(ni->succ_fd, message, strlen(message));
    if (result != 0) {
        // Error sending
        return -1;
    }
    return 1;
}

int process_command_find(unsigned int key, t_nodeinfo *ni)
{
    if (ni->succ_id && ring_distance(ni->key, key) < ring_distance(ni->key, ni->succ_id)) {
        printf("Key %u belongs to node %u (%s:%s)\n", key, ni->key, ni->ipaddr, ni->self_port);
        return 0;
    }

    register_request(ni->n, key, ni);

    char message[64] = "";
    sprintf(message, "FND %u %u %u %s %s\n", key, ni->n, ni->key, ni->ipaddr, ni->self_port);
    if (sendall(ni->succ_fd, message, strlen(message)) != 0) {
        puts("\x1b[31m[!] Error sending \"find\" message to successor\033[m");
        return -1;
    }
    ni->n++;
    ni->n %= 100;
    return 0;
}

int process_user_message(t_nodeinfo *ni)
{
    char buffer[128] = "";
    char *result = fgets(buffer, sizeof(buffer), stdin);
    if (buffer != result) {
        puts("\x1b[31m[!] Error calling fgets()\033[m");
        return -1;
    }
    if (strcmp(buffer, "new\n") == 0) {
        if (ni->main_fd != -1) {
            // Server is already running
            puts("Node already in a ring");
            return 0;
        }
        if (process_command_new(ni) != 0)
            return -1;
        puts("\x1b[32m[*] Created new ring\033[m");
        return 0;
    }
    if (strncmp(buffer, "pentry", 6) == 0) {
        if (ni->main_fd != -1) {
            // Server is already running
            puts("Node already in a ring");
            return 0;
        }
        int pred, port;
        char ipaddr[INET_ADDRSTRLEN] = "";
        if (sscanf(buffer+6, " %u %16s %u\n", &pred, ipaddr, &port) == 3) {
            ipaddr[15] = '\0';            
            return process_command_pentry(pred, port, ipaddr, ni);
        }
        else {
            puts("Invalid format. Usage: pentry pred pred.IP pred.port");
        }
    }
    if (strcmp(buffer, "show\n") == 0) {
        return process_command_show(ni);
    }    
    if (strcmp(buffer, "leave\n") == 0) {
        if (ni->main_fd == -1) {
            puts("Node is not a member of any ring");
        }
        return process_command_leave(ni);
    }
    if (strncmp(buffer, "find ", 5) == 0) {
        unsigned int key;
        if (sscanf(buffer+5, "%u", &key) != 1) {
            puts("Invalid format. Usage: find k");
            return 0;
        }
        if (key > 31) {
            puts("Invalid key (maximum is 31)");
            return 0;
        }
        return process_command_find(key, ni);
    }
    if (strncmp(buffer, "chord ", 6) == 0) {
        unsigned int shcut_id, shcut_port;
        char shcut_ipaddr[INET_ADDRSTRLEN] = "";
        if (sscanf(buffer+6, "%u %16s %u", &shcut_id, shcut_ipaddr, &shcut_port) == 3) {   
            if (shcut_id > 31 || shcut_id < 0) {
                printf("Invalid shortcut node '%d'\n", shcut_id);
                return 0;
            }
            if (!isipaddr(shcut_ipaddr)) {
                printf("Invalid IP address '%s'\n", shcut_ipaddr);
                return 0;
            }
            if (shcut_port > 65535 || shcut_port < 0) {
                printf("Invalid port number '%d'\n", shcut_port);
                return 0;
            }    
            if (ni->shcut_info != NULL)
                freeaddrinfo(ni->shcut_info);
            if (generate_udp_addrinfo(shcut_ipaddr, shcut_port, &ni->shcut_info) != 0) {
                ni->shcut_info = NULL;
                puts("[!] Couldn't create chord!");
                return 0;
            }
            ni->shcut_id = shcut_id;
            strcpy(ni->shcut_ip, shcut_ipaddr);
            ni->shcut_port = shcut_port;
            puts("\x1b[32m[*] Success!\033[m");
        }
        else {
            puts("Invalid format. Usage: chord i i.IP i.port");
        }
    }
    return 0;
}