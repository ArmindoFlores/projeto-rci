#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
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

int main()
{
    // Create the server
    t_error_or_nodeinfo *esi = init_server("8008");
    if (is_error(esi)) {
        printf("Error initializing server!\n");
        free(esi);
        exit(1);
    }

    t_nodeinfo *si = get_nodeinfo(esi);
    free(esi);

    // Main loop
    while (1) {
        printf("Waiting for events...\n");

        // This calls select() and may block
        // Returns after an event happens
        t_event e = select_event(si);
        printf("Got new event '%s'\n", get_event_string(e));

        int result = 0;
        // Act based on what event just occurred
        switch (e) {
            case E_INCOMING_CONNECTION:
                // A client is trying to connect to us
                result = process_incoming_connection(si);
                break;

            case E_MESSAGE_TEMP:
                // A new connection has sent a message
                result = process_message_temp(si);
                break;

            case E_MESSAGE_SUCCESSOR:
                // This node's successor sent a message
                result = process_message_successor(si);
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

    close_server(si);
    return 0;
}