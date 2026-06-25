// client.cpp — Client
// TxChain, EE 450 final project.
// Connects to the Main Server over TCP.
//   ./client <username>                     -> CHECK WALLET
//   ./client <username1> <username2> <amt>  -> TXCOINS
//
// Phase 1: boot, connect to Server-M over TCP, send the request, print the
// "sent" message. The response is received in Phase 2.
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
#define LOCALHOST        "127.0.0.1"
// Main Server's TCP port for the client = 25000 + last 3 digits of USC ID (875).
#define TCP_PORT_CLIENT  25875
#define BUF_SIZE         4096

int main(int argc, char *argv[]) {
    // 1 argument  -> CHECK WALLET (<username>)
    // 3 arguments -> TXCOINS (<sender> <receiver> <amount>)
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <username>  |  %s <sender> <receiver> <amount>\n",
                argv[0], argv[0]);
        exit(1);
    }

    printf("The client is up and running.\n");
    fflush(stdout);

    // Create the TCP socket and connect to the Main Server.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("client: socket");
        exit(1);
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(TCP_PORT_CLIENT);
    serv.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("client: connect");
        exit(1);
    }

    // Build and send the request, then print the corresponding "sent" line.
    char msg[BUF_SIZE];
    if (argc == 2) {
        // CHECK WALLET
        snprintf(msg, sizeof(msg), "CHECK_WALLET %s", argv[1]);
        if (send(sockfd, msg, strlen(msg), 0) < 0) {
            perror("client: send");
            exit(1);
        }
        printf("\"%s\" sent a balance enquiry request to the main server.\n", argv[1]);
    } else {
        // TXCOINS: argv[1]=sender, argv[2]=receiver, argv[3]=amount
        snprintf(msg, sizeof(msg), "TXCOINS %s %s %s", argv[1], argv[2], argv[3]);
        if (send(sockfd, msg, strlen(msg), 0) < 0) {
            perror("client: send");
            exit(1);
        }
        printf("%s has requested to transfer %s txcoins to %s\n",
               argv[1], argv[3], argv[2]);
    }
    fflush(stdout);

    // Phase 1 ends here. Phase 2: wait for and print the Main Server's response.
    close(sockfd);
    return 0;
}
