// serverC.cpp — Backend Server C
// TxChain, EE 450 final project.
// Owns block3.txt. Communicates with the Main Server over UDP only.
//
// Phase 1: create + bind the UDP socket on the static port, print the boot
// message, then wait for requests (request handling arrives in Phase 2).
//
// UDP socket setup adapted from Beej's Guide to Network Programming
// (section on datagram sockets): http://www.beej.us/guide/bgnet/

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
#define LOCALHOST   "127.0.0.1"
// Static UDP port for Server C = 23000 + last 3 digits of USC ID (875).
#define UDP_PORT_C  23875
#define BUF_SIZE    4096

int main() {
    // Create the UDP (datagram) socket.
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("serverC: socket");
        exit(1);
    }

    // Allow quick reuse of the port if the server is restarted.
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind to localhost on the static UDP port.
    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(UDP_PORT_C);
    my_addr.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("serverC: bind");
        exit(1);
    }

    // Boot message (format follows the example output in the project doc).
    printf("The ServerC is up and running using UDP on port %d.\n", UDP_PORT_C);
    fflush(stdout);

    // Phase 1: no request processing yet. Stay alive and ready so that the
    // server keeps running until killed (Phase 2 fills in the handling here).
    char buf[BUF_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);
    while (1) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src_addr, &src_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        // Phase 2: parse and handle the request from the Main Server here.
    }

    close(sockfd);
    return 0;
}
