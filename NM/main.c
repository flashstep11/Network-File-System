#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> // For networking definitions

#define NM_PORT 8080 // The well-known port everyone connects to

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    // 1. Create the socket (The Phone)
    // AF_INET = IPv4, SOCK_STREAM = TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Optional: overrides "Address already in use" error when restarting quickly
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // 2. Define the address to bind to (The Phone Number)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on ALL available network interfaces
    address.sin_port = htons(NM_PORT);    // htons converts port to network byte order

    // 3. Bind the socket to the address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 4. Start Listening (Turn ringer on)
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Name Server (NM) started. Listening on port %d...\n", NM_PORT);

    // 5. Main Loop: Accept incoming calls indefinitely
    while(1) {
        printf("Waiting for connections...\n");
        
        // accept() BLOCKS until someone connects
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue; // Don't crash the whole server if one connection fails
        }

        // Convert IP to human-readable string for logging
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("[LOG] Connection established from %s:%d\n", client_ip, ntohs(address.sin_port));

        // For this skeleton, we just say hello and hang up
        char *hello = "Hello from Name Server!";
        send(new_socket, hello, strlen(hello), 0);
        printf("[LOG] Sent greeting, closing connection.\n\n");

        close(new_socket);
    }

    return 0;
}