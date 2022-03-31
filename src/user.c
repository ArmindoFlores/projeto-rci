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
    // Create server
    int result = init_server(ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return -1;
    }

    // Connect to self
    result = init_client(ni->ipaddr, ni->self_port, ni);
    if (result != 0) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return 0;
    }
    ni->pred_id = ni->key;

    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);

    // Accept our own connection
    ni->succ_fd = accept(ni->main_fd, &addr, &addrlen);
    if (ni->succ_fd == -1) {
        puts("\x1b[31m[!] Error creating node\033[m");
        close_sockets(ni);
        return 0;
    }
    if (ni->successor == NULL)
        ni->successor = new_conn_info(2048);
    else
        set_conn_info(ni->successor, 2048);
    ni->succ_id = ni->key;
    strcpy(ni->succ_ip, ni->ipaddr);
    sscanf(ni->self_port, "%u", &ni->succ_port);

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

int process_command_bentry(int boot, int port, char *ipaddr, t_nodeinfo *ni)
{
    if (boot > 31 || boot < 0) {
        printf("Invalid boot node '%d'\n", boot);
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

    struct addrinfo *res;
    if (generate_udp_addrinfo(ipaddr, port, &res) != 0) {
        puts("\x1b[31m[!] Error generating information\033[m");
        return -1;
    }

    char message[64] = "";
    sprintf(message, "EFND %u", ni->key);
    if (udpsend(ni->udp_fd, message, strlen(message), res) != 0) {
        puts("\x1b[31m[!] Error sending EFND message\033[m");
        close_sockets(ni);
    }
    freeaddrinfo(res);
    return 0;
}

void print_space(unsigned int n)
{
    for (unsigned int i = 0; i < n; i++)
        putchar(' ');
}

void print_info(char *name, unsigned int key, char *ipaddr, unsigned int port, int exists)
{
    size_t name_len = strlen(name), ipaddr_len = strlen(ipaddr);
    char key_str[4], port_str[6];

    // Print name
    print_space((13 - name_len) / 2 + (13 - name_len) % 2);
    if (exists)
        printf("%s", name);
    else
        printf("\x1b[31m%s\033[m", name);
    print_space((13 - name_len) / 2);

    if (exists) {
        // Get sizes
        sprintf(key_str, "%u", key);
        sprintf(port_str, "%u", port);
        size_t key_len = strlen(key_str), port_len = strlen(port_str);

        // Print key
        print_space((5 - key_len) / 2 + (5 - key_len) % 2);
        printf("%s", key_str);
        print_space((5 - key_len) / 2);

        // Print ipaddr
        print_space((19 - ipaddr_len) / 2 + (19 - ipaddr_len) % 2);
        printf("%s", ipaddr);
        print_space((19 - ipaddr_len) / 2);

        // Print port
        print_space((10 - port_len) / 2 + (10 - port_len) % 2);
        printf("%s", port_str);
        print_space((10 - port_len) / 2);
    }
    if (!exists)
        printf("\x1b[31m N/D         N/D           N/D    \033[m");
    puts("");
}

int process_command_show(t_nodeinfo *ni)
{
    unsigned int self_port;
    sscanf(ni->self_port, "%u", &self_port);
    puts("     Node     Key      IP Address       Port   ");
    print_info("Predecessor", ni->pred_id, ni->pred_ip, ni->pred_port, ni->pred_fd != -1);
    print_info("Self", ni->key, ni->ipaddr, self_port, 1);
    print_info("Successor", ni->succ_id, ni->succ_ip, ni->succ_port, ni->succ_fd != -1);
    print_info("Shortcut", ni->shcut_id, ni->shcut_ip, ni->shcut_port, ni->shcut_info != NULL);
    return 0;
}

int process_command_leave(t_nodeinfo *ni)
{
    if (ni->succ_fd == -1)
        return 0;

    if (ni->pred_fd != -1)
        close(ni->pred_fd);
    ni->pred_fd = -1;

    if (ni->succ_id != ni->key) {
        char message[64] = "";
        sprintf(message, "PRED %u %s %u\n", ni->pred_id, ni->pred_ip, ni->pred_port);
        int result = sendall(ni->succ_fd, message, strlen(message));
        if (result != 0) {
            // Error sending
            return -1;
        }
    }

    close(ni->succ_fd);
    ni->succ_fd = -1;
    
    close(ni->main_fd);
    ni->main_fd = -1;

    close(ni->udp_fd);
    ni->udp_fd = -1;

    return 0;
}

int process_command_exit(t_nodeinfo *ni)
{
    if (ni->main_fd == -1)
        return 1;

    int result = process_command_leave(ni);
    if (result != 0)
        return result;
    return 1;
}

int process_command_find(unsigned int key, t_nodeinfo *ni)
{
    if ((ni->succ_id == ni->key && ni->pred_id == ni->key) || (ni->succ_id && ring_distance(ni->key, key) < ring_distance(ni->key, ni->succ_id))) {
        printf("Key %u belongs to node %u (%s:%s)\n", key, ni->key, ni->ipaddr, ni->self_port);
        return 0;
    }

    if (register_request(ni->n, key, ni) < 0) {
        puts("Find request queue is full, try again later");
        return 0;
    }

    char message[64] = "";
    sprintf(message, "FND %u %u %u %s %s\n", key, ni->n, ni->key, ni->ipaddr, ni->self_port);
    
    int result = send_to_closest(message, key, ni);
    if (result < 0)
        return 0;

    ni->n++;
    ni->n %= 100;
    return 0;
}

int process_command_chord(unsigned int key, char *ipaddr, unsigned int port, t_nodeinfo *ni)
{    
    if (ni->shcut_info != NULL)
        freeaddrinfo(ni->shcut_info);
    if (generate_udp_addrinfo(ipaddr, port, &ni->shcut_info) != 0) {
        ni->shcut_info = NULL;
        puts("Couldn't create chord");
        return 0;
    }
    ni->shcut_id = key;
    strcpy(ni->shcut_ip, ipaddr);
    ni->shcut_port = port;
    puts("Successfully created chord");
    return 0;
}

int process_command_echord(t_nodeinfo *ni)
{    
    if (ni->shcut_info != NULL) {
        puts("Deleted existing shortcut");
        freeaddrinfo(ni->shcut_info);
    }
    else
        puts("No shortcut to delete");
    ni->shcut_info = NULL;
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
    if (strcmp(buffer, "new\n") == 0 || strcmp(buffer, "n\n") == 0) {
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
    if (strncmp(buffer, "bentry", 6) == 0 || strncmp(buffer, "b ", 2) == 0 || strncmp(buffer, "b\n", 2) == 0) {
        if (ni->main_fd != -1) {
            // Server is already running
            puts("Node already in a ring");
            return 0;
        }
        int boot, port;
        char ipaddr[INET_ADDRSTRLEN] = "";
        char *start_pos = strchr(buffer, ' ');
        if (start_pos && sscanf(start_pos+1, "%u %15s %u\n", &boot, ipaddr, &port) == 3) {
            ipaddr[15] = '\0';            
            return process_command_bentry(boot, port, ipaddr, ni);
        }
        else {
            puts("Invalid format.\nUsage: \x1b[4mb\033[mentry boot boot.IP boot.port");
        }
        return 0;
    }
    if (strncmp(buffer, "pentry", 6) == 0 || strncmp(buffer, "p ", 2) == 0 || strncmp(buffer, "p\n", 2) == 0) {
        if (ni->main_fd != -1) {
            // Server is already running
            puts("Node already in a ring");
            return 0;
        }
        int pred, port;
        char ipaddr[INET_ADDRSTRLEN] = "";
        char *start_pos = strchr(buffer, ' ');
        if (start_pos && sscanf(start_pos+1, "%u %15s %u\n", &pred, ipaddr, &port) == 3) {
            ipaddr[15] = '\0';            
            return process_command_pentry(pred, port, ipaddr, ni);
        }
        else {
            puts("Invalid format.\nUsage: \x1b[4mp\033[mentry pred pred.IP pred.port");
        }
        return 0;
    }
    if (strcmp(buffer, "show\n") == 0 || strcmp(buffer, "s\n") == 0) {
        return process_command_show(ni);
    }    
    if (strcmp(buffer, "leave\n") == 0 || strcmp(buffer, "l\n") == 0) {
        if (ni->main_fd == -1) {
            puts("Node is not a member of any ring");
        }
        return process_command_leave(ni);
    }
    if (strcmp(buffer, "exit\n") == 0 || strcmp(buffer, "ex\n") == 0) {
        return process_command_exit(ni);
    }
    if (strncmp(buffer, "find", 4) == 0 || strncmp(buffer, "f ", 2) == 0 || strncmp(buffer, "f\n", 2) == 0) {
        unsigned int key;
        char *start_pos = strchr(buffer, ' ');
        if (!start_pos || sscanf(start_pos+1, "%u", &key) != 1) {
            puts("Invalid format.\nUsage: \x1b[4mf\033[mind k");
            return 0;
        }
        if (key > 31) {
            puts("Invalid key (maximum is 31)");
            return 0;
        }
        if (ni->main_fd == -1) {
            puts("Node is not in a ring");
            return 0;
        }
        return process_command_find(key, ni);
    }
    if (strncmp(buffer, "chord", 5) == 0 || strncmp(buffer, "c ", 2) == 0 || strncmp(buffer, "c\n", 2) == 0) {
        unsigned int shcut_id, shcut_port;
        char shcut_ipaddr[INET_ADDRSTRLEN] = "";
        char *start_pos = strchr(buffer, ' ');
        if (start_pos && sscanf(start_pos+1, "%u %15s %u", &shcut_id, shcut_ipaddr, &shcut_port) == 3) {   
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
        }
        else {
            puts("Invalid format.\nUsage: \x1b[4mc\033[mhord i i.IP i.port");
            return 0;
        }
        return process_command_chord(shcut_id, shcut_ipaddr, shcut_port, ni);
    }
    if (strcmp(buffer, "echord\n") == 0 || strcmp(buffer, "ec\n") == 0) {
        return process_command_echord(ni);
    }
    if (strncmp(buffer, "e", 1) == 0) {
        puts("Ambiguous command. Did you mean:");
        puts("\t\x1b[4mex\033[mit");
        puts("\t\x1b[4mec\033[mhord");
        return 0;
    }
    size_t buffer_l = strlen(buffer);
    if (buffer_l > 0)
        buffer[buffer_l-1] = '\0';
    printf("Invalid command \"%s\".\nAvailable commands:\n", buffer);
    puts("");
    puts("\t\x1b[4mb\033[mentry \x1b[3mboot boot.IP boot.port\033[m -> join \x1b[3mboot\033[m's ring");
    puts("\t\x1b[4mc\033[mhord \x1b[3mi i.IP i.port\033[m           -> create a shortcut to \x1b[3mi\033[m");
    puts("\t\x1b[4mec\033[mhord                        -> delete current shortcut");
    puts("\t\x1b[4mex\033[mit                          -> exit application");
    puts("\t\x1b[4mf\033[mind \x1b[3mk\033[m                        -> find the the owner of key/object \x1b[3mk\033[m");
    puts("\t\x1b[4ml\033[meave                         -> leave the ring");
    puts("\t\x1b[4mn\033[mew                           -> create new ring");
    puts("\t\x1b[4mp\033[mentry \x1b[3mpred pred.IP pred.port\033[m -> join a ring and set \x1b[3mpred\033[m as predecessor");
    puts("\t\x1b[4ms\033[mhow                          -> show current node state");
    return 0;
}