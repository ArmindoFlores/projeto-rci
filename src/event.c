#include "event.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>


t_event select_event(t_nodeinfo* ni)
{
    struct timeval SELECT_TIMEOUT = { .tv_sec = 0, .tv_usec = 1000 };

    // If there's pending reads in any of the connections, do them first
    if (ni->succ_fd > 0 && has_available_data(ni->successor))
        return E_MESSAGE_SUCCESSOR;
    if (ni->pred_fd > 0 && has_available_data(ni->predecessor))
        return E_MESSAGE_PREDECESSOR;
    if (ni->temp_fd > 0 && has_available_data(ni->temp))
        return E_MESSAGE_TEMP;

    // Initialize file descriptor set
    fd_set read_fds;
    FD_ZERO(&read_fds); 
    int fdmax = maxfd(ni);
    fdmax = fdmax > STDIN_FILENO ? fdmax : STDIN_FILENO;

    // Add currently in-use file descriptors to the set
    FD_SET(STDIN_FILENO, &read_fds);
    if (ni->main_fd > 0)
        FD_SET(ni->main_fd, &read_fds);
    if (ni->succ_fd > 0)
        FD_SET(ni->succ_fd, &read_fds);
    if (ni->pred_fd > 0)
        FD_SET(ni->pred_fd, &read_fds);
    if (ni->temp_fd > 0)
        FD_SET(ni->temp_fd, &read_fds);
    if (ni->udp_fd > 0)
        FD_SET(ni->udp_fd, &read_fds);

    int count = select(fdmax+1, &read_fds, NULL, NULL, &SELECT_TIMEOUT);
    if (count < 0) {
        printf("\x1b[31m[!] Select error (%d)!\033[m\n", errno);
        exit(1);
    }

    while (count--) {
        // Check for ready fd's with FD_ISSET
        if (ni->main_fd != -1 && FD_ISSET(ni->main_fd, &read_fds)) {
            // Incoming connection
            FD_CLR(ni->main_fd, &read_fds);
            return E_INCOMING_CONNECTION;
        }
        else if (ni->udp_fd != -1 && FD_ISSET(ni->udp_fd, &read_fds)) {
            // Incoming message from UDP socket
            FD_CLR(ni->udp_fd, &read_fds);
            return E_MESSAGE_UDP;
        }
        else if (ni->succ_fd != -1 && FD_ISSET(ni->succ_fd, &read_fds)) {
            // Incoming message from successor
            FD_CLR(ni->succ_fd, &read_fds);
            return E_MESSAGE_SUCCESSOR;
        }
        else if (ni->pred_fd != -1 && FD_ISSET(ni->pred_fd, &read_fds)) {
            // Incoming message from predecessor
            FD_CLR(ni->pred_fd, &read_fds);
            return E_MESSAGE_PREDECESSOR;
        }
        else if (ni->temp_fd != -1 && FD_ISSET(ni->temp_fd, &read_fds)) {
            // Incoming message from somewhere else
            FD_CLR(ni->temp_fd, &read_fds);
            return E_MESSAGE_TEMP;
        }
        else if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            FD_CLR(STDIN_FILENO, &read_fds);
            return E_MESSAGE_USER;
        }
    }

    return E_TIMEOUT;
}
