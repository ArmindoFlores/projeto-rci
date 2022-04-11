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
    return create_ring(ni);
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

    return join_ring(pred, ipaddr, port, ni);
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
        return 0;
    }
    else {
        register_udp_message(ni, message, strlen(message), res->ai_addr, res->ai_addrlen, UDPMSG_ENTERING);
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
    puts("");
    
    putchar('{');
    int any = 0;
    for (unsigned int key = 0; key < 32; key++) {
        if (ni->objects[key] != NULL) {
            printf("\n\t%u -> \"%s\",", key, ni->objects[key]);
            any = 1;
        }
    }
    if (any)
        putchar('\n');
    puts("}");

    return 0;
}

int process_command_leave(t_nodeinfo *ni)
{

    if (ni->succ_id != ni->key && ni->succ_fd != -1) {
        char message[64] = "";

        if (ni->pred_fd != -1) {
            for (unsigned int i = 0; i < 32; i++) {
                if (ni->objects[i] != NULL) {
                    sprintf(message, "SET %u %u %u %s\n", i, ni->find_n, ni->key, ni->objects[i]);
                    free(ni->objects[i]);
                    ni->objects[i] = NULL;
                    int result = sendall(ni->pred_fd, message, strlen(message));
                    if (result < 0)
                        return result;
                    ni->find_n++;
                    ni->find_n %= 100;
                }
            }
        }

        sprintf(message, "PRED %u %s %u\n", ni->pred_id, ni->pred_ip, ni->pred_port);
        int result = sendall(ni->succ_fd, message, strlen(message));
        if (result != 0) {
            // Error sending
            return -1;
        }
    }

    if (ni->pred_fd != -1)
        close(ni->pred_fd);
    ni->pred_fd = -1;

    if (ni->succ_fd != -1)
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
    if ((ni->succ_id == ni->key && ni->pred_id == ni->key) || (ni->succ_id && ring_distance(ni->key, key) <= ring_distance(ni->key, ni->succ_id))) {
        printf("Key %u belongs to node %u (%s:%s)\n", key, ni->key, ni->ipaddr, ni->self_port);
        return 0;
    }

    if (register_request(ni->find_n, key, NULL, ni) < 0) {
        puts("Find request queue is full, try again later");
        return 0;
    }

    char message[64] = "";
    sprintf(message, "FND %u %u %u %s %s\n", key, ni->find_n, ni->key, ni->ipaddr, ni->self_port);
    
    int result = send_to_closest(message, key, ni);
    if (result < 0)
        return 0;

    ni->find_n++;
    ni->find_n %= 100;
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

int process_command_get(unsigned int key, t_nodeinfo *ni)
{
    if ((ni->succ_id == ni->key && ni->pred_id == ni->key) || (ni->succ_id && ring_distance(ni->key, key) <= ring_distance(ni->key, ni->succ_id))) {
        char *object = get_object(key, ni);
        if (object == NULL)
            printf("%u -> NULL\n", key);
        else
            printf("%u -> \"%s\"\n", key, object);
        return 0;
    }

    if (register_request(ni->find_n, key, NULL, ni) < 0) {
        puts("Get request queue is full, try again later");
        return 0;
    }

    char message[64] = "";
    sprintf(message, "GET %u %u %u %s %s\n", key, ni->find_n, ni->key, ni->ipaddr, ni->self_port);
    
    int result = send_to_closest(message, key, ni);
    if (result < 0)
        return 0;

    ni->find_n++;
    ni->find_n %= 100;
    return 0;
}

int process_command_set(unsigned int key, char *value, t_nodeinfo *ni)
{
    if ((ni->succ_id == ni->key && ni->pred_id == ni->key) || (ni->succ_id && ring_distance(ni->key, key) <= ring_distance(ni->key, ni->succ_id))) {
        if (strlen(value) == 0)
            set_object(key, NULL, ni);
        else
            set_object(key, value, ni);
        return 0;
    }

    char message[64] = "";
    sprintf(message, "SET %u %u %u %s\n", key, ni->find_n, ni->key, value != NULL ? value : "");
    
    int result = send_to_closest(message, key, ni);
    if (result < 0)
        return 0;

    ni->find_n++;
    ni->find_n %= 100;
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
    if (strncmp(buffer, "get", 3) == 0 || strncmp(buffer, "g ", 2) == 0 || strncmp(buffer, "g\n", 2) == 0) {
        unsigned int key;
        char *start_pos = strchr(buffer, ' ');
        if (!start_pos || sscanf(start_pos+1, "%u", &key) != 1) {
            puts("Invalid format.\nUsage: \x1b[4mg\033[met k");
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
        return process_command_get(key, ni);
    }
    if (strncmp(buffer, "set", 3) == 0 || strncmp(buffer, "se ", 3) == 0 || strncmp(buffer, "se\n", 3) == 0) {
        unsigned int key;
        char value[24] = "";
        char *start_pos = strchr(buffer, ' ');
        if (!start_pos || sscanf(start_pos+1, "%u %16[^\n]", &key, value) != 2) {
            if (sscanf(start_pos+1, "%u", &key) != 1) {
                puts("Invalid format.\nUsage: \x1b[4ms\033[met k [value]");
                return 0;
            }
            else
                value[0] = '\0';
        }
        if (key > 31) {
            puts("Invalid key (maximum is 31)");
            return 0;
        }
        if (ni->main_fd == -1) {
            puts("Node is not in a ring");
            return 0;
        }
        return process_command_set(key, value, ni);
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
    if (buffer_l == 1)
        return 0;
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
    puts("");
    puts("\t\x1b[4mg\033[met \x1b[3mk\033[m                         -> get value associated with key \x1b[3mk\033[m");
    puts("\t\x1b[4mse\033[mt \x1b[3mk\033[m \x1b[3mvalue\033[m                   -> set key \x1b[3mk\033[m's value to \x1b[3mvalue\033[m");

    return 0;
}