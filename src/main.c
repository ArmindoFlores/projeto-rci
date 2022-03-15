#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"
#include "client.h"
#include "server.h"
#include "event.h"

char* get_event_string(t_event e)
{
    static char strings[][32] = {
        "E_INCOMING_CONNECTION", 
        "E_MESSAGE_PREDECESSOR", 
        "E_MESSAGE_SUCCESSOR", 
        "E_MESSAGE_TEMP", 
        "E_MESSAGE_USER", 
        "E_TIMEOUT", 
        "E_ERROR"
    };
    return strings[e];
}

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

int read_from_stdin(t_nodeinfo *ni)
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
    return 0;
}

void usage(char *name) {
    printf("Usage: ./%s ID IPADDR PORT\n", name);
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        usage(argv[0]);
        exit(1);
    }

    if (!strisui(argv[1])) {
        fprintf(stderr, "ID must be a number (was '%s')\n", argv[1]);
        usage(argv[0]);
        exit(1);
    }

    if (!strisui(argv[3])) {
        fprintf(stderr, "PORT must be a number (was '%s')\n", argv[3]);
        usage(argv[0]);
        exit(1);
    }    

    t_nodeinfo *ni = new_nodeinfo(strtoui(argv[1]), argv[2], argv[3]);

    // Create the server
    // if (err != 0) {
    //     printf("Error initializing server!\n");
    //     free_nodeinfo(ni);
    //     exit(1);
    // }

    // Main loop
    while (1) {
        // printf("[*] Waiting for events...\n");

        // This calls select() and may block
        // Returns after an event happens
        t_event e = select_event(ni);

        if (e != E_TIMEOUT)
            printf("[*] Got new event '%s'\n", get_event_string(e));

        int result = 0;
        // Act based on what event just occurred
        switch (e) {
            case E_INCOMING_CONNECTION:
                // A client is trying to connect to us
                result = process_incoming_connection(ni);
                break;

            case E_MESSAGE_TEMP:
                // A new connection has sent a message
                result = process_message_temp(ni);
                break;

            case E_MESSAGE_SUCCESSOR:
                // This node's successor sent a message
                result = process_message_successor(ni);
                break;

            case E_MESSAGE_PREDECESSOR:
                // This node's predecessor sent a message
                result = process_message_predecessor(ni);
                break;

            case E_MESSAGE_USER:
                result = read_from_stdin(ni);
                break;

            default:
                break;
        }
        if (result != 0) {
            puts("\x1b[31m[!] An error has occurred!\033[m");
            break;
        }
    }

    close_server(ni);
    free_nodeinfo(ni);
    return 0;
}