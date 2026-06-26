// serverM.cpp — Main Server (Server-M)
// TxChain, EE 450 final project.
// Talks to the client and monitor over TCP, and to backend servers A/B/C over UDP.
//
// Phase 2: on a client request, Server-M queries the three backends over UDP,
// performs the computation (CHECK WALLET balance, or a TXCOINS transfer), and
// replies to the client. Only Server-M performs encryption/decryption: it
// encrypts usernames/amounts before sending to the backends and decrypts the
// rows the backends return. (Phase 3 adds the monitor's TXLIST handling.)
//
// Encryption scheme (Caesar +3): letters shift within their case, digits shift
// mod 10, everything else (spaces, '.', etc.) is unchanged. Serial numbers are
// never encrypted.
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
#include <time.h>

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

using std::string;
using std::vector;

// Host is always the loopback address (required by the spec).
#define LOCALHOST         "127.0.0.1"
// Static ports = base + last 3 digits of USC ID (875).
#define UDP_PORT_M        24875   // UDP socket toward backend servers A/B/C
#define TCP_PORT_CLIENT   25875   // TCP listener for the client
#define TCP_PORT_MONITOR  26875   // TCP listener for the monitor
#define UDP_PORT_A        21875
#define UDP_PORT_B        22875
#define UDP_PORT_C        23875
#define INITIAL_BALANCE   1000
#define BUF_SIZE          65536

// ---- Encryption / decryption (Caesar +3) --------------------------------

static char enc_char(char c) {
    if (c >= 'a' && c <= 'z') return 'a' + (c - 'a' + 3) % 26;
    if (c >= 'A' && c <= 'Z') return 'A' + (c - 'A' + 3) % 26;
    if (c >= '0' && c <= '9') return '0' + (c - '0' + 3) % 10;
    return c;
}
static char dec_char(char c) {
    if (c >= 'a' && c <= 'z') return 'a' + (c - 'a' + 23) % 26; // -3 mod 26
    if (c >= 'A' && c <= 'Z') return 'A' + (c - 'A' + 23) % 26;
    if (c >= '0' && c <= '9') return '0' + (c - '0' + 7) % 10;  // -3 mod 10
    return c;
}
static string enc_str(const string &s) {
    string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) o += enc_char(s[i]);
    return o;
}
static string dec_str(const string &s) {
    string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) o += dec_char(s[i]);
    return o;
}

// ---- A single decrypted transaction -------------------------------------

struct Txn {
    long serial;
    string sender;     // plaintext
    string receiver;   // plaintext
    long amount;       // plaintext integer
};

// ---- Socket helpers -----------------------------------------------------

static int make_tcp_listener(int port, const char *label) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror(label); exit(1); }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror(label); exit(1); }
    if (listen(fd, 10) < 0) { perror(label); exit(1); }
    return fd;
}

static int g_udp_fd;                         // UDP socket toward backends
static struct sockaddr_in g_backend[3];      // A, B, C addresses
static const char *g_backend_name[3] = {"A", "B", "C"};
static const int   g_backend_port[3] = {UDP_PORT_A, UDP_PORT_B, UDP_PORT_C};

static void init_udp() {
    g_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_fd < 0) { perror("serverM: udp socket"); exit(1); }
    int yes = 1;
    setsockopt(g_udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT_M);
    addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    if (bind(g_udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("serverM: udp bind"); exit(1);
    }
    int ports[3] = {UDP_PORT_A, UDP_PORT_B, UDP_PORT_C};
    for (int i = 0; i < 3; i++) {
        memset(&g_backend[i], 0, sizeof(g_backend[i]));
        g_backend[i].sin_family = AF_INET;
        g_backend[i].sin_port = htons(ports[i]);
        g_backend[i].sin_addr.s_addr = inet_addr(LOCALHOST);
    }
}

// Send one UDP request to backend i and return its reply as a string.
static string udp_request(int i, const string &req) {
    sendto(g_udp_fd, req.c_str(), req.size(), 0,
           (struct sockaddr *)&g_backend[i], sizeof(g_backend[i]));
    char buf[BUF_SIZE];
    ssize_t n = recvfrom(g_udp_fd, buf, sizeof(buf) - 1, 0, NULL, NULL);
    if (n < 0) return "";
    buf[n] = '\0';
    return string(buf);
}

// Parse a backend QUERY reply ("<maxSerial>\n<row>\n<row>...") into decrypted
// transactions, updating the running global max serial.
static void parse_query_reply(const string &reply, vector<Txn> &out, long &max_serial) {
    std::istringstream iss(reply);
    string line;
    if (!std::getline(iss, line)) return;
    long ms = strtol(line.c_str(), NULL, 10);
    if (ms > max_serial) max_serial = ms;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::istringstream rs(line);
        long serial; string es, er, ea;
        if (!(rs >> serial >> es >> er >> ea)) continue;
        Txn t;
        t.serial = serial;
        t.sender = dec_str(es);
        t.receiver = dec_str(er);
        t.amount = strtol(dec_str(ea).c_str(), NULL, 10);
        out.push_back(t);
    }
}

// Query all three backends for the given (encrypted) names. name2 may be "*".
// Prints the per-backend "sent/received" messages; verb distinguishes the two
// operations ("transactions" for CHECK WALLET, "the feedback" for TXCOINS).
static void query_all(const string &enc1, const string &enc2, bool txcoins,
                      vector<Txn> &txns, long &max_serial) {
    string req = "QUERY " + enc1 + " " + enc2;
    for (int i = 0; i < 3; i++) {
        printf("The main server sent a request to server %s.\n", g_backend_name[i]);
        fflush(stdout);
        string reply = udp_request(i, req);
        if (txcoins) {
            printf("The main server received the feedback from server %s using UDP over port %d.\n",
                   g_backend_name[i], g_backend_port[i]);
        } else {
            printf("The main server received transactions from Server %s using UDP over port %d.\n",
                   g_backend_name[i], g_backend_port[i]);
        }
        fflush(stdout);
        parse_query_reply(reply, txns, max_serial);
    }
}

// ---- Request handlers ---------------------------------------------------

static void do_check_wallet(int conn, const string &user) {
    string enc_user = enc_str(user);
    vector<Txn> txns;
    long max_serial = 0;
    query_all(enc_user, "*", false, txns, max_serial);

    string reply;
    if (txns.empty()) {
        // User does not appear in any transaction -> not part of the network.
        reply = "NOTFOUND";
    } else {
        long balance = INITIAL_BALANCE;
        for (size_t i = 0; i < txns.size(); i++) {
            if (txns[i].receiver == user) balance += txns[i].amount;
            if (txns[i].sender == user)   balance -= txns[i].amount;
        }
        std::ostringstream os; os << "FOUND " << balance;
        reply = os.str();
    }
    send(conn, reply.c_str(), reply.size(), 0);
    printf("The main server sent the current balance to the client.\n");
    fflush(stdout);
}

static void do_txcoins(int conn, const string &sender, const string &receiver, long amount) {
    string enc_s = enc_str(sender);
    string enc_r = enc_str(receiver);
    vector<Txn> txns;
    long max_serial = 0;
    query_all(enc_s, enc_r, true, txns, max_serial);

    // Determine which parties appear anywhere in the records.
    bool sender_found = false, receiver_found = false;
    long sender_balance = INITIAL_BALANCE;
    for (size_t i = 0; i < txns.size(); i++) {
        if (txns[i].sender == sender || txns[i].receiver == sender) sender_found = true;
        if (txns[i].sender == receiver || txns[i].receiver == receiver) receiver_found = true;
        if (txns[i].receiver == sender) sender_balance += txns[i].amount;
        if (txns[i].sender == sender)   sender_balance -= txns[i].amount;
    }

    string reply;
    if (!sender_found && !receiver_found) {
        reply = "MISSINGBOTH";
    } else if (!sender_found) {
        reply = "MISSINGSENDER";
    } else if (!receiver_found) {
        reply = "MISSINGRECEIVER";
    } else if (sender_balance < amount) {
        std::ostringstream os; os << "INSUFFICIENT " << sender_balance;
        reply = os.str();
    } else {
        // Feasible transfer: assign the next serial, encrypt the entry, and
        // hand it to a randomly chosen backend to record.
        long serial = max_serial + 1;
        std::ostringstream amt; amt << amount;
        std::ostringstream entry;
        entry << "NEW " << serial << " " << enc_s << " " << enc_r << " "
              << enc_str(amt.str());
        int pick = rand() % 3;
        udp_request(pick, entry.str());   // backend appends and confirms ("OK")

        long new_balance = sender_balance - amount;
        std::ostringstream os; os << "SUCCESS " << new_balance;
        reply = os.str();
    }
    send(conn, reply.c_str(), reply.size(), 0);
    printf("The main server sent the result of the transaction to the client.\n");
    fflush(stdout);
}

// Accept one client connection, read the request, run the operation, reply.
static void handle_client(int listen_fd) {
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int conn = accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
    if (conn < 0) { perror("serverM: accept client"); return; }

    char buf[BUF_SIZE];
    ssize_t n = recv(conn, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(conn); return; }
    buf[n] = '\0';

    string request(buf);
    std::istringstream iss(request);
    string op; iss >> op;
    if (op == "CHECK_WALLET") {
        string user; iss >> user;
        printf("The main server received input=\"%s\" from the client using TCP "
               "over port %d.\n", user.c_str(), TCP_PORT_CLIENT);
        fflush(stdout);
        do_check_wallet(conn, user);
    } else if (op == "TXCOINS") {
        string sender, receiver, amt; iss >> sender >> receiver >> amt;
        printf("The main server received from %s to transfer %s coins to %s "
               "using TCP over port %d.\n", sender.c_str(), amt.c_str(),
               receiver.c_str(), TCP_PORT_CLIENT);
        fflush(stdout);
        do_txcoins(conn, sender, receiver, strtol(amt.c_str(), NULL, 10));
    }
    close(conn);
}

static bool by_serial(const Txn &a, const Txn &b) { return a.serial < b.serial; }

// Handle the monitor's TXLIST: gather every transaction from A/B/C, sort by
// serial, and write the fully-decrypted txchain.txt, then confirm to the monitor.
static void do_txlist(int conn) {
    vector<Txn> all;
    long max_serial = 0;
    for (int i = 0; i < 3; i++) {
        string reply = udp_request(i, "ALL");   // M-silent: no Table-7 entry exists
        parse_query_reply(reply, all, max_serial);
    }
    (void)max_serial;
    std::sort(all.begin(), all.end(), by_serial);

    // Write the decrypted statement; every row ends with a newline.
    std::ofstream out("txchain.txt");
    for (size_t i = 0; i < all.size(); i++) {
        out << all[i].serial << " " << all[i].sender << " "
            << all[i].receiver << " " << all[i].amount << "\n";
    }
    out.close();

    const char *ok = "OK";
    send(conn, ok, strlen(ok), 0);
}

// Accept one monitor connection (TXLIST), build txchain.txt, and confirm.
static void handle_monitor(int listen_fd) {
    struct sockaddr_in mon;
    socklen_t mon_len = sizeof(mon);
    int conn = accept(listen_fd, (struct sockaddr *)&mon, &mon_len);
    if (conn < 0) { perror("serverM: accept monitor"); return; }

    char buf[BUF_SIZE];
    ssize_t n = recv(conn, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(conn); return; }
    buf[n] = '\0';

    if (strncmp(buf, "TXLIST", 6) == 0) {
        printf("The main server received a sorted list request from the monitor "
               "using TCP over port %d.\n", TCP_PORT_MONITOR);
        fflush(stdout);
        do_txlist(conn);
    }
    close(conn);
}

int main() {
    srand((unsigned)time(NULL));   // for the random backend selection in TXCOINS

    init_udp();                                                      // UDP toward backends
    int client_listen  = make_tcp_listener(TCP_PORT_CLIENT,  "serverM: client listener");
    int monitor_listen = make_tcp_listener(TCP_PORT_MONITOR, "serverM: monitor listener");

    printf("The main server is up and running.\n");
    fflush(stdout);

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
