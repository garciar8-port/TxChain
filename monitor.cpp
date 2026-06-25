// monitor.cpp — Monitor
// TxChain, EE 450 final project.
// Connects to the Main Server over TCP.
//   ./monitor TXLIST  -> request a sorted, decrypted transaction list (txchain.txt)
//
// Phase 1: boot, connect to Server-M over TCP, send the TXLIST request, print
// the "sent" message. The confirmation is received in Phase 3.
//
// TCP client setup adapted from Beej's Guide to Network Programming
// (stream sockets / client): http://www.beej.us/guide/bgnet/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>

// Host is always the loopback address (required by the spec).
#define LOCALHOST         "127.0.0.1"
// Main Server's TCP port for the monitor = 26000 + last 3 digits of USC ID (875).
#define TCP_PORT_MONITOR  26875
#define BUF_SIZE          4096

int main(int argc, char *argv[]) {
    // The only valid invocation is: ./monitor TXLIST
    if (argc != 2 || strcmp(argv[1], "TXLIST") != 0) {
        fprintf(stderr, "Usage: %s TXLIST\n", argv[0]);
        exit(1);
    }

    printf("The monitor is up and running.\n");
    fflush(stdout);

    // Create the TCP socket and connect to the Main Server.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("monitor: socket");
        exit(1);
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(TCP_PORT_MONITOR);
    serv.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("monitor: connect");
        exit(1);
    }

    // Send the TXLIST request and print the "sent" line.
    const char *msg = "TXLIST";
    if (send(sockfd, msg, strlen(msg), 0) < 0) {
        perror("monitor: send");
        exit(1);
    }
    printf("Monitor sent a sorted list request to the main server.\n");
    fflush(stdout);

    // Phase 1 ends here. Phase 3: wait for and print the confirmation.
    close(sockfd);
    return 0;
}
