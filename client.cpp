// client.cpp — Client
// TxChain, EE 450 final project.
// Connects to the Main Server over TCP.
//   ./client <username>                     -> CHECK WALLET
//   ./client <username1> <username2> <amt>  -> TXCOINS
//
// Phase 2: after sending the request, the client receives the Main Server's
// reply and prints the corresponding on-screen result message.
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
    if (sockfd < 0) { perror("client: socket"); exit(1); }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(TCP_PORT_CLIENT);
    serv.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("client: connect");
        exit(1);
    }

    char msg[BUF_SIZE];
    char reply[BUF_SIZE];

    if (argc == 2) {
        // ---- CHECK WALLET ----
        snprintf(msg, sizeof(msg), "CHECK_WALLET %s", argv[1]);
        if (send(sockfd, msg, strlen(msg), 0) < 0) { perror("client: send"); exit(1); }
        printf("\"%s\" sent a balance enquiry request to the main server.\n", argv[1]);
        fflush(stdout);

        ssize_t n = recv(sockfd, reply, sizeof(reply) - 1, 0);
        if (n <= 0) { close(sockfd); exit(1); }
        reply[n] = '\0';

        if (strncmp(reply, "FOUND", 5) == 0) {
            long balance = strtol(reply + 5, NULL, 10);
            printf("The current balance of \"%s\" is : %ld txcoins.\n", argv[1], balance);
        } else { // NOTFOUND
            printf("\"%s\" is not a part of the network.\n", argv[1]);
        }
        fflush(stdout);
    } else {
        // ---- TXCOINS ---- argv[1]=sender, argv[2]=receiver, argv[3]=amount
        snprintf(msg, sizeof(msg), "TXCOINS %s %s %s", argv[1], argv[2], argv[3]);
        if (send(sockfd, msg, strlen(msg), 0) < 0) { perror("client: send"); exit(1); }
        printf("%s has requested to transfer %s txcoins to %s\n",
               argv[1], argv[3], argv[2]);
        fflush(stdout);

        ssize_t n = recv(sockfd, reply, sizeof(reply) - 1, 0);
        if (n <= 0) { close(sockfd); exit(1); }
        reply[n] = '\0';

        if (strncmp(reply, "SUCCESS", 7) == 0) {
            long bal = strtol(reply + 7, NULL, 10);
            printf("%s successfully transferred %s txcoins to %s. "
                   "The current balance of %s is : %ld txcoins.\n",
                   argv[1], argv[3], argv[2], argv[1], bal);
        } else if (strncmp(reply, "INSUFFICIENT", 12) == 0) {
            long bal = strtol(reply + 12, NULL, 10);
            printf("%s was unable to transfer %s txcoins to %s because of "
                   "insufficient balance. The current balance of %s is : %ld txcoins.\n",
                   argv[1], argv[3], argv[2], argv[1], bal);
        } else if (strcmp(reply, "MISSINGSENDER") == 0) {
            printf("Unable to proceed with the transaction as %s is not part "
                   "of the network.\n", argv[1]);
        } else if (strcmp(reply, "MISSINGRECEIVER") == 0) {
            printf("Unable to proceed with the transaction as %s is not part "
                   "of the network.\n", argv[2]);
        } else if (strcmp(reply, "MISSINGBOTH") == 0) {
            printf("Unable to proceed with the transaction as %s and %s are not "
                   "part of the network.\n", argv[1], argv[2]);
        }
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}
