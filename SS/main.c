#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define NM_IP "127.0.0.1" // Connect to localhost for testing
#define NM_PORT 8080

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    printf("Storage Server (SS) starting up...\n");

    // 1. Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(NM_PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, NM_IP, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 2. Connect to the Name Server
    printf("Attempting to connect to Name Server at %s:%d...\n", NM_IP, NM_PORT);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed. Is the NM running?\n");
        return -1;
    }

    printf("Successfully connected to NM!\n");

    // 3. Wait for a greeting (just to prove data can flow)
    int valread = read(sock, buffer, 1024);
    printf("Received from NM: %s\n", buffer);

    // 4. Hang up
    printf("Closing connection and shutting down SS.\n");
    close(sock);

    return 0;
}