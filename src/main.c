#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "server.h"

char* get_event_string(t_event e)
{
    static char strings[][32] = {"E_INCOMING_CONNECTION", "E_MESSAGE_PREDECESSOR", "E_MESSAGE_SUCCESSOR", "E_MESSAGE_TEMP", "E_ERROR"};
    return strings[e];
}

int main()
{
    t_error_or_serverinfo *esi = init_server("8008");
    if (is_error(esi)) {
        printf("Error initializing server!\n");
        free(esi);
        exit(1);
    }

    t_serverinfo *si = get_serverinfo(esi);
    free(esi);

    while (1) {
        printf("Waiting for events...\n");
        t_event e = select_event(si);
        printf("Got new event '%s'\n", get_event_string(e));

        switch (e) {
            case E_INCOMING_CONNECTION:
                process_incoming_connection(si);
                break;

            case E_MESSAGE_TEMP:
                process_message_temp(si);
                break;

            default:
                break;
        }
    }

    close_server(si);
    return 0;
}