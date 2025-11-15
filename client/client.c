// client.c - Network File System Client

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 8192
#define NM_IP "127.0.0.1"
#define NM_PORT 8080

int nm_socket = -1;
char username[64];
char client_ip[16];
int client_nm_port;
int client_ss_port;

void print_prompt() {
    printf("nfs:%s> ", username);
    fflush(stdout);
}

int connect_to_nm() {
    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket < 0) {
        perror("Failed to create socket");
        return -1;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    
    if (inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM IP");
        close(nm_socket);
        return -1;
    }

    if (connect(nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        close(nm_socket);
        return -1;
    }

    return 0;
}

int register_with_nm() {
    char reg_msg[256];
    snprintf(reg_msg, sizeof(reg_msg), "REGISTER_CLIENT %s %s %d %d\n",
             username, client_ip, client_nm_port, client_ss_port);
    
    if (write(nm_socket, reg_msg, strlen(reg_msg)) < 0) {
        perror("Failed to send registration");
        return -1;
    }

    char response[BUFFER_SIZE];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("No response from NM\n");
        return -1;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        printf("✓ Registered as '%s'\n", username);
        return 0;
    } else {
        printf("Registration failed: %s", response);
        return -1;
    }
}

int connect_to_ss(const char* ss_ip, int ss_port) {
    int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Failed to create SS socket");
        return -1;
    }

    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("Invalid SS IP");
        close(ss_socket);
        return -1;
    }

    if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("Connection to SS failed");
        close(ss_socket);
        return -1;
    }

    return ss_socket;
}

void handle_read(const char* filename) {
    char command[256];
    snprintf(command, sizeof(command), "READ %s\n", filename);
    write(nm_socket, command, strlen(command));

    char response[BUFFER_SIZE];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("No response from NM\n");
        return;
    }
    response[n] = '\0';

    // Check if NM returned SS_INFO
    if (strncmp(response, "SS_INFO:", 8) == 0) {
        char ss_ip[16];
        int ss_port;
        if (sscanf(response, "SS_INFO:%15[^:]:%d", ss_ip, &ss_port) == 2) {
            printf("Connecting to SS at %s:%d...\n", ss_ip, ss_port);
            
            int ss_socket = connect_to_ss(ss_ip, ss_port);
            if (ss_socket < 0) {
                printf("Failed to connect to SS\n");
                return;
            }

            // Send READ command to SS
            snprintf(command, sizeof(command), "READ %s\n", filename);
            write(ss_socket, command, strlen(command));

            // Read content from SS
            while (1) {
                n = read(ss_socket, response, sizeof(response) - 1);
                if (n <= 0) break;
                response[n] = '\0';
                printf("%s", response);
                if (strstr(response, "\nEOF\n") || strstr(response, "STOP")) break;
            }
            printf("\n");
            close(ss_socket);
        }
    } else {
        printf("%s", response);
    }
}

void handle_stream(const char* filename) {
    char command[256];
    snprintf(command, sizeof(command), "STREAM %s\n", filename);
    write(nm_socket, command, strlen(command));

    char response[BUFFER_SIZE];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("No response from NM\n");
        return;
    }
    response[n] = '\0';

    if (strncmp(response, "SS_INFO:", 8) == 0) {
        char ss_ip[16];
        int ss_port;
        if (sscanf(response, "SS_INFO:%15[^:]:%d", ss_ip, &ss_port) == 2) {
            int ss_socket = connect_to_ss(ss_ip, ss_port);
            if (ss_socket < 0) {
                printf("Failed to connect to SS\n");
                return;
            }

            snprintf(command, sizeof(command), "STREAM %s\n", filename);
            write(ss_socket, command, strlen(command));

            // Read word by word
            while (1) {
                n = read(ss_socket, response, sizeof(response) - 1);
                if (n <= 0) {
                    printf("\n✗ Connection lost during streaming\n");
                    break;
                }
                response[n] = '\0';
                
                if (strstr(response, "STREAM_END") || strstr(response, "EOF") || strstr(response, "STOP")) {
                    printf("\n");
                    break;
                }
                
                printf("%s", response);
                fflush(stdout);
            }
            close(ss_socket);
        }
    } else {
        printf("%s", response);
    }
}

void handle_write(const char* args) {
    // WRITE <filename> <sentence_number>
    char filename[256];
    int sentence_num;
    if (sscanf(args, "%255s %d", filename, &sentence_num) != 2) {
        printf("Usage: WRITE <filename> <sentence_number>\n");
        return;
    }

    char command[256];
    snprintf(command, sizeof(command), "WRITE %s\n", filename);
    write(nm_socket, command, strlen(command));

    char response[BUFFER_SIZE];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("No response from NM\n");
        return;
    }
    response[n] = '\0';

    if (strncmp(response, "SS_INFO:", 8) == 0) {
        char ss_ip[16];
        int ss_port;
        if (sscanf(response, "SS_INFO:%15[^:]:%d", ss_ip, &ss_port) == 2) {
            int ss_socket = connect_to_ss(ss_ip, ss_port);
            if (ss_socket < 0) {
                printf("Failed to connect to SS\n");
                return;
            }

            // Lock sentence
            snprintf(command, sizeof(command), "WRITE %s %d\n", filename, sentence_num);
            write(ss_socket, command, strlen(command));
            
            n = read(ss_socket, response, sizeof(response) - 1);
            if (n > 0) {
                response[n] = '\0';
                printf("%s", response);
                
                if (strncmp(response, "ACK:", 4) == 0) {
                    // Interactive editing mode
                    printf("\n=== INTERACTIVE WRITE MODE ===\n");
                    printf("Format: <word_index> <content>\n");
                    printf("Example: 0 Hello\n");
                    printf("         1 World\n");
                    printf("Type 'ETIRW' to save and exit\n");
                    printf("==============================\n\n");
                    
                    char line[512];
                    while (1) {
                        printf("edit> ");
                        fflush(stdout);
                        if (!fgets(line, sizeof(line), stdin)) break;
                        
                        // Remove trailing newline for comparison
                        line[strcspn(line, "\n")] = 0;
                        
                        if (strcmp(line, "ETIRW") == 0 || strcmp(line, "etirw") == 0) {
                            write(ss_socket, "ETIRW\n", 6);
                            n = read(ss_socket, response, sizeof(response) - 1);
                            if (n > 0) {
                                response[n] = '\0';
                                printf("\n%s\n", response);
                            }
                            break;
                        }
                        
                        // Send the line with newline added back
                        char send_buf[520];
                        snprintf(send_buf, sizeof(send_buf), "%s\n", line);
                        write(ss_socket, send_buf, strlen(send_buf));
                        
                        n = read(ss_socket, response, sizeof(response) - 1);
                        if (n > 0) {
                            response[n] = '\0';
                            if (strncmp(response, "ACK:", 4) == 0) {
                                printf("✓ %s", response);
                            } else if (strncmp(response, "ERR:", 4) == 0) {
                                printf("✗ %s", response);
                            } else {
                                printf("%s", response);
                            }
                        }
                    }
                }
            }
            close(ss_socket);
        }
    } else {
        printf("%s", response);
    }
}

void send_simple_command(const char* cmd) {
    char command[512];
    snprintf(command, sizeof(command), "%s\n", cmd);
    ssize_t sent = write(nm_socket, command, strlen(command));
    if (sent <= 0) {
        printf("Failed to send command to NM\n");
        return;
    }

    // Set a timeout for reading
    struct timeval tv;
    tv.tv_sec = 5;  // 5 second timeout
    tv.tv_usec = 0;
    setsockopt(nm_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    char response[BUFFER_SIZE];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        if (n == 0) {
            printf("NM disconnected\n");
        } else {
            printf("No response from NM (timeout or error)\n");
        }
        return;
    }
    response[n] = '\0';
    printf("%s", response);
    
    // Reset to blocking mode
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(nm_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}

void print_help() {
    printf("\nAvailable Commands:\n");
    printf("  VIEW [-a] [-l]              - List files\n");
    printf("  READ <filename>             - Read file contents\n");
    printf("  CREATE <filename>           - Create new file\n");
    printf("  WRITE <file> <sentence#>    - Edit file (interactive)\n");
    printf("  DELETE <filename>           - Delete file\n");
    printf("  INFO <filename>             - Show file details\n");
    printf("  STREAM <filename>           - Stream file word-by-word\n");
    printf("  UNDO <filename>             - Undo last change\n");
    printf("  LIST                        - List all users\n");
    printf("  ADDACCESS -R/-W <file> <user> - Grant access\n");
    printf("  REMACCESS <file> <user>     - Remove access\n");
    printf("  EXEC <filename>             - Execute file as commands\n");
    printf("  HELP                        - Show this help\n");
    printf("  QUIT / EXIT                 - Disconnect\n\n");
}

int main() {
    printf("=========================================\n");
    printf("  Network File System - Client          \n");
    printf("=========================================\n\n");

    // Get username
    printf("Enter username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) {
        printf("Failed to read username\n");
        return 1;
    }
    username[strcspn(username, "\n")] = 0;

    // Set client info (in real system, get actual IP/ports)
    strcpy(client_ip, "127.0.0.1");
    client_nm_port = 5000 + (getpid() % 1000);
    client_ss_port = 6000 + (getpid() % 1000);

    printf("Connecting to Name Server at %s:%d...\n", NM_IP, NM_PORT);
    if (connect_to_nm() < 0) {
        return 1;
    }

    if (register_with_nm() < 0) {
        close(nm_socket);
        return 1;
    }

    printf("\nType 'HELP' for available commands\n\n");

    // Main command loop
    char line[512];
    while (1) {
        print_prompt();
        if (!fgets(line, sizeof(line), stdin)) break;
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Parse command
        char cmd[64];
        char args[448];
        memset(args, 0, sizeof(args));
        
        if (sscanf(line, "%63s", cmd) != 1) continue;
        
        char* space = strchr(line, ' ');
        if (space) {
            strncpy(args, space + 1, sizeof(args) - 1);
        }

        // Handle commands
        if (strcasecmp(cmd, "QUIT") == 0 || strcasecmp(cmd, "EXIT") == 0) {
            send_simple_command("QUIT");
            break;
        } else if (strcasecmp(cmd, "HELP") == 0) {
            print_help();
        } else if (strcasecmp(cmd, "READ") == 0) {
            handle_read(args);
        } else if (strcasecmp(cmd, "STREAM") == 0) {
            handle_stream(args);
        } else if (strcasecmp(cmd, "WRITE") == 0) {
            handle_write(args);
        } else {
            // All other commands go directly to NM
            send_simple_command(line);
        }
    }

    printf("\nDisconnecting...\n");
    close(nm_socket);
    return 0;
}
