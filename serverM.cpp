// serverM.cpp — Main Server (Server-M)
// TxChain, EE 450 final project.
// Talks to the client and monitor over TCP, and to backend servers A/B/C over UDP.
//
// Phase 1: boot all sockets (1 UDP to backends, 2 TCP listeners for client and
// monitor), accept incoming client/monitor connections, read the request, and
// print the corresponding "received" message. No computation or backend
// communication yet (that arrives in Phase 2/3).
//
// Socket setup adapted from Beej's Guide to Network Programming:
// http://www.beej.us/guide/bgnet/

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
// Static ports = base + last 3 digits of USC ID (875).
#define UDP_PORT_M        24875   // UDP socket toward backend servers A/B/C
#define TCP_PORT_CLIENT   25875   // TCP listener for the client
#define TCP_PORT_MONITOR  26875   // TCP listener for the monitor
#define BUF_SIZE          4096

// Create a TCP listening socket bound to localhost on the given port.
static int make_tcp_listener(int port, const char *label) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror(label);
        exit(1);
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror(label);
        exit(1);
    }
    if (listen(fd, 10) < 0) {
        perror(label);
        exit(1);
    }
    return fd;
}

// Create the UDP socket toward the backend servers, bound to the static port.
static int make_udp_socket() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("serverM: udp socket");
        exit(1);
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT_M);
    addr.sin_addr.s_addr = inet_addr(LOCALHOST);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("serverM: udp bind");
        exit(1);
    }
    return fd;
}

// Accept one client connection, read the request, print the "received" line.
// Phase 1 only reports receipt; the response is computed in later phases.
static void handle_client(int listen_fd) {
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int conn = accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
    if (conn < 0) {
        perror("serverM: accept client");
        return;
    }

    char buf[BUF_SIZE];
    ssize_t n = recv(conn, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(conn);
        return;
    }
    buf[n] = '\0';

    // Wire format from client:
    //   "CHECK_WALLET <username>"
    //   "TXCOINS <sender> <receiver> <amount>"
    char op[64], a[256], b[256], c[256];
    if (strncmp(buf, "CHECK_WALLET", 12) == 0) {
        sscanf(buf, "%63s %255s", op, a);
        printf("The main server received input=\"%s\" from the client using TCP "
               "over port %d.\n", a, TCP_PORT_CLIENT);
    } else if (strncmp(buf, "TXCOINS", 7) == 0) {
        sscanf(buf, "%63s %255s %255s %255s", op, a, b, c); // a=sender b=receiver c=amount
        printf("The main server received from %s to transfer %s coins to %s "
               "using TCP over port %d.\n", a, c, b, TCP_PORT_CLIENT);
    }
    fflush(stdout);

    // Phase 2: query backends, compute, and reply to the client here.
    close(conn);
}

// Accept one monitor connection, read the request, print the "received" line.
static void handle_monitor(int listen_fd) {
    struct sockaddr_in mon;
    socklen_t mon_len = sizeof(mon);
    int conn = accept(listen_fd, (struct sockaddr *)&mon, &mon_len);
    if (conn < 0) {
        perror("serverM: accept monitor");
        return;
    }

    char buf[BUF_SIZE];
    ssize_t n = recv(conn, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(conn);
        return;
    }
    buf[n] = '\0';

    // Wire format from monitor: "TXLIST"
    if (strncmp(buf, "TXLIST", 6) == 0) {
        printf("The main server received a sorted list request from the monitor "
               "using TCP over port %d.\n", TCP_PORT_MONITOR);
    }
    fflush(stdout);

    // Phase 3: gather all transactions, sort, write txchain.txt, reply here.
    close(conn);
}

int main() {
    // 1 UDP socket toward backends + 2 TCP listeners (client, monitor).
    int udp_fd = make_udp_socket();                                  // Phase 2 use
    (void)udp_fd;
    int client_listen = make_tcp_listener(TCP_PORT_CLIENT, "serverM: client listener");
    int monitor_listen = make_tcp_listener(TCP_PORT_MONITOR, "serverM: monitor listener");

    printf("The main server is up and running.\n");
    fflush(stdout);

    // Multiplex the two TCP listeners so we can serve client and monitor
    // requests as they arrive, indefinitely.
    fd_set master;
    FD_ZERO(&master);
    FD_SET(client_listen, &master);
    FD_SET(monitor_listen, &master);
    int max_fd = (client_listen > monitor_listen) ? client_listen : monitor_listen;

    while (1) {
        fd_set read_fds = master;
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("serverM: select");
            exit(1);
        }
        if (FD_ISSET(client_listen, &read_fds))  handle_client(client_listen);
        if (FD_ISSET(monitor_listen, &read_fds)) handle_monitor(monitor_listen);
    }

    return 0;
}
