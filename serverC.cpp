// serverC.cpp — Backend Server C
// TxChain, EE 450 final project.
// Owns block3.txt. Communicates with the Main Server over UDP only.
//
// The backend stores transactions entirely in ENCRYPTED (ciphertext) form and
// never decrypts: the Main Server encrypts queries before sending, so the
// backend just matches/stores ciphertext. It supports two requests:
//   QUERY <encName1> <encName2>  -> reply: "<maxSerial>" then matching rows
//                                   (name2 may be "*" meaning "no second name")
//   NEW <serial> <encS> <encR> <encAmt> -> append the row, reply "OK"
//
// UDP socket setup adapted from Beej's Guide to Network Programming
// (datagram sockets): http://www.beej.us/guide/bgnet/

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

#include <string>
#include <vector>
#include <sstream>
#include <fstream>

using std::string;
using std::vector;

// Host is always the loopback address (required by the spec).
#define LOCALHOST   "127.0.0.1"
// Static UDP port for Server C = 23000 + last 3 digits of USC ID (875).
#define UDP_PORT_C  23875
#define BLOCK_FILE  "block3.txt"
#define SRV         "C"
#define BUF_SIZE    65536

// In-memory copy of this backend's block file, one raw ciphertext line per
// entry: "<serial> <encSender> <encReceiver> <encAmount>".
static vector<string> g_txns;

// Load the block file into memory at boot (skipping blank lines).
static void load_block() {
    std::ifstream in(BLOCK_FILE);
    string line;
    while (std::getline(in, line)) {
        if (!line.empty()) g_txns.push_back(line);
    }
}

// Parse a stored line into its 4 fields. Returns false on a malformed line.
static bool parse_row(const string &line, long &serial, string &s, string &r, string &amt) {
    std::istringstream iss(line);
    return (bool)(iss >> serial >> s >> r >> amt);
}

// Build the QUERY reply: first line = max serial in this block, then every row
// in which name1 (or name2, if not "*") appears as sender or receiver.
static string handle_query(const string &name1, const string &name2) {
    long max_serial = 0;
    string rows;
    bool use2 = (name2 != "*");
    for (size_t i = 0; i < g_txns.size(); i++) {
        long serial; string s, r, amt;
        if (!parse_row(g_txns[i], serial, s, r, amt)) continue;
        if (serial > max_serial) max_serial = serial;
        bool match = (s == name1 || r == name1) ||
                     (use2 && (s == name2 || r == name2));
        if (match) {
            rows += "\n";
            rows += g_txns[i];
        }
    }
    std::ostringstream out;
    out << max_serial << rows;
    return out.str();
}

// Append a new (already-encrypted) transaction to memory and to the block file.
static string handle_new(const string &rest) {
    // rest = "<serial> <encS> <encR> <encAmt>"
    long serial; string s, r, amt;
    if (!parse_row(rest, serial, s, r, amt)) return "ERR";

    std::ostringstream line;
    line << serial << " " << s << " " << r << " " << amt;
    g_txns.push_back(line.str());

    // Persist to the block file, terminated with a newline as required.
    std::ofstream out(BLOCK_FILE, std::ios::app);
    out << line.str() << "\n";
    out.close();
    return "OK";
}

int main() {
    load_block();

    // Create + bind the UDP socket on the static port.
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("server" SRV ": socket"); exit(1); }
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(UDP_PORT_C);
    my_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("server" SRV ": bind");
        exit(1);
    }

    printf("The Server" SRV " is up and running using UDP on port %d.\n", UDP_PORT_C);
    fflush(stdout);

    char buf[BUF_SIZE];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    while (1) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src, &src_len);
        if (n < 0) { if (errno == EINTR) continue; continue; }
        buf[n] = '\0';

        printf("The Server" SRV " received a request from the Main Server.\n");
        fflush(stdout);

        // Dispatch on the first token.
        string msg(buf);
        std::istringstream iss(msg);
        string op; iss >> op;
        string reply;
        if (op == "QUERY") {
            string n1, n2; iss >> n1 >> n2;
            reply = handle_query(n1, n2);
        } else if (op == "NEW") {
            string rest;
            std::getline(iss, rest);                 // " <serial> <encS> <encR> <encAmt>"
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            reply = handle_new(rest);
        } else {
            reply = "ERR";
        }

        sendto(sockfd, reply.c_str(), reply.size(), 0,
               (struct sockaddr *)&src, src_len);

        printf("The Server" SRV " finished sending the response to the Main Server.\n");
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}
