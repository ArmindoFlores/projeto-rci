#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"
#include "client.h"
#include "server.h"

char* get_event_string(t_event e)
{
    static char strings[][32] = {"E_INCOMING_CONNECTION", "E_MESSAGE_PREDECESSOR", "E_MESSAGE_SUCCESSOR", "E_MESSAGE_TEMP", "E_MESSAGE_USER", "E_ERROR"};
    return strings[e];
}

void read_from_stdin()
{
    char buffer[128] = "";
    char *result = fgets(buffer, sizeof(buffer), stdin);
    if (buffer == result) {
        printf("Received message: %s", buffer);
    }
    else {
        puts("Error calling fgets()");
    }
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

    t_nodeinfo *ni = new_nodeinfo(strtoui(argv[1]));

    // Create the server
    int err = init_server(argv[3], ni);
    if (err != 0) {
        printf("Error initializing server!\n");
        free_nodeinfo(ni);
        exit(1);
    }

    // Main loop
    while (1) {
        printf("Waiting for events...\n");

        // This calls select() and may block
        // Returns after an event happens
        t_event e = select_event(ni);
        printf("Got new event '%s'\n", get_event_string(e));

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

            case E_MESSAGE_USER:
                read_from_stdin();
                break;

            // TODO: We're missing E_MESSAGE_PREDECESSOR

            default:
                break;
        }
        if (result != 0) {
            puts("An error has occurred!");
            break;
        }
    }

    close_server(ni);
    free_nodeinfo(ni);
    return 0;
}