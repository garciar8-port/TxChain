// monitor.cpp — Monitor
// TxChain, EE 450 final project.
// Connects to the Main Server over TCP.
//   ./monitor TXLIST  -> request a sorted, decrypted transaction list (txchain.txt)
//
// Phase 0: no-op skeleton. Logic added in later phases.

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
#define LOCALHOST "127.0.0.1"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}
