#include "user.h"
#include "server.h"
#include "client.h"
#include "utils.h"
#include <stdio.h>
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
    sprintf(message, "SELF %d %s %s\n", ni->key, ni->ipaddr, ni->tcpserverport);
    if (sendall(ni->prevfd, message, strlen(message)) != 0) {
        puts("\x1b[31m[!] Error sending message to predecessor\033[m");
        return -1;
    }
    
    return 0;
}

int process_command_show(t_nodeinfo *ni)
{
    puts("\x1b[34m[*] Node Status\033[m");
        
    // Node info
    printf("This node info:\n* Key: %u\n* IP: %s\n* Port: %s\n", ni->key, ni->ipaddr, ni->tcpserverport);

    // Node successor info
    if (ni->nextfd == -1)
        puts("\x1b[31mThis node doesn't have a successor!\033[m");
    else
        printf("Successor node info (%d):\n* Key: MISSING\n* IP: %s\n* Port: %u\n",ni->nextfd, ni->succ_ip, ni->succ_port);

    // Node predecessor info
    if (ni->prevfd == -1)
        puts("\x1b[31mThis node doesn't have a predecessor!\033[m");
    else
        printf("Predecessor node info (%d):\n* Key: MISSING\n* IP: %s\n* Port: %u\n", ni->prevfd, ni->pred_ip, ni->pred_port);

    // TODO (iv) Shortcut info
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
        if (ni->mainfd != -1) {
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
        if (ni->mainfd != -1) {
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
    return 0;
}