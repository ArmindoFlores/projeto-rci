#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "user.h"
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
        "E_MESSAGE_UDP",
        "E_TIMEOUT", 
        "E_ERROR"
    };
    return strings[e];
}

void usage(char *name) {
    printf("Usage: ./%s ID IPADDR PORT\n", name);
}

void check_for_lost_udp_messages(t_nodeinfo *ni)
{
    if (ni->waiting_for_chord_ack) {
        clock_t now = clock();
        double time_taken = ((double) (now - ni->req_start)) / CLOCKS_PER_SEC;
        if (time_taken > 0.005) {
            // Timeout
            puts("Found timed out UDP message, sending through TCP");
            ni->waiting_for_chord_ack = 0;
            int result = sendall(ni->succ_fd, ni->ongoing_udp_message, strlen(ni->ongoing_udp_message));
            if (result < 0) {
                close(ni->succ_fd);
                ni->succ_fd = -1;
            }
            else if (result > 0) {
                // An error occurred
                close(ni->succ_fd);
                ni->succ_fd = -1;
            }
        }
    }
}

int main(int argc, char *argv[])
{    
    // Ignore SIGPIPE
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &act, NULL) == -1)
        return -1;

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
    printf(">>> ");
    fflush(stdout);
    while (1) {
        // printf("[*] Waiting for events...\n");

        // This calls select() and may block
        // Returns after an event happens
        t_event e = select_event(ni);

        // First of all, check for lost UDP messages
        check_for_lost_udp_messages(ni);

        if (e != E_MESSAGE_USER && e != E_TIMEOUT)
            printf("\x08\x08\x08\x08");

        if (e != E_TIMEOUT)
            printf("[*] Got new event '%s'\n", get_event_string(e));

        int result = 0;
        // Act based on what event just occurred
        switch (e) {
            case E_INCOMING_CONNECTION:
                // A client is trying to connect to us
                result = process_incoming_connection(ni);
                break;

            case E_MESSAGE_UDP:
                result = process_message_udp(ni);
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
                result = process_user_message(ni);
                break;

            default:
                break;
        }
        if (result < 0) {
            puts("\x1b[31m[!] An error has occurred!\033[m");
            break;
        }
        if (result > 0)
            break;

                    
        if (e != E_TIMEOUT) {
            printf(">>> ");
            fflush(stdout);
        }
    }

    close_sockets(ni);
    free_nodeinfo(ni);
    return 0;
}