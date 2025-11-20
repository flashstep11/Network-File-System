
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <readline/readline.h>
#include <readline/history.h>

#define BUFFER_SIZE 8192
#define DEFAULT_NM_IP "127.0.0.1"
#define NM_PORT 8080

int nm_socket = -1;
char username[64];
char client_ip[16];
int client_nm_port;
char nm_ip[16] = DEFAULT_NM_IP;  // Can be overridden by command line
int client_ss_port;

char* get_prompt() {
    static char prompt[80];
    snprintf(prompt, sizeof(prompt), "nfs:%s> ", username);
    return prompt;
}

// Connects to Name Server with 5 second timeout to avoid hanging
int connect_to_nm() {
    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket < 0) {
        perror("Failed to create socket");
        return -1;
    }

    // 5 second timeout prevents infinite wait if NM is down
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(nm_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Warning: Failed to set receive timeout");
    }
    if (setsockopt(nm_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Warning: Failed to set send timeout");
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM IP");
        close(nm_socket);
        return -1;
    }

    printf("Attempting to connect to %s:%d...\n", nm_ip, NM_PORT);
    if (connect(nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        printf("\nTroubleshooting:\n");
        printf("1. Is the Name Server running? Start it with: cd NM && ./name_server\n");
        printf("2. Check firewall settings (port %d must be open)\n", NM_PORT);
        printf("3. If NM is on different machine, verify the IP address\n");
        close(nm_socket);
        return -1;
    }

    printf("✓ Connected to Name Server!\n");
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
            int ss_socket = connect_to_ss(ss_ip, ss_port);
            if (ss_socket < 0) {
                printf("Failed to connect to SS\n");
                return;
            }

            // Send READ command to SS with username for permission check
            snprintf(command, sizeof(command), "READ %s %s\n", username, filename);
            write(ss_socket, command, strlen(command));

            // Read content from SS
            int content_printed = 0;
            while (1) {
                n = read(ss_socket, response, sizeof(response) - 1);
                if (n <= 0) break;
                response[n] = '\0';
                
                // Check for EOF marker and stop before printing it
                char* eof_marker = strstr(response, "\nEOF\n");
                if (eof_marker) {
                    *eof_marker = '\0';  // Truncate before EOF
                    if (strlen(response) > 0) {
                        printf("%s", response);
                        content_printed = 1;
                    }
                    break;
                }
                
                if (strlen(response) > 0) {
                    printf("%s", response);
                    content_printed = 1;
                }
                if (strstr(response, "STOP")) break;
            }
            
            if (!content_printed) {
                printf("(empty file)\n");
            } else {
                printf("\n");
            }
            close(ss_socket);
        }
    } else {
        printf("%s", response);
    }
}

// STREAM: Displays file word-by-word from Storage Server
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

    // Early error check prevents infinite loop (bug fix for deleted files)
    if (strncmp(response, "ERR:", 4) == 0) {
        printf("%s", response);
        return;
    }

    if (strncmp(response, "SS_INFO:", 8) == 0) {
        char ss_ip[16];
        int ss_port;
        if (sscanf(response, "SS_INFO:%15[^:]:%d", ss_ip, &ss_port) == 2) {
            int ss_socket = connect_to_ss(ss_ip, ss_port);
            if (ss_socket < 0) {
                printf("Failed to connect to SS\n");
                return;
            }

            snprintf(command, sizeof(command), "STREAM %s %s\n", username, filename);
            write(ss_socket, command, strlen(command));

            // 30 second timeout to detect SS issues (prevents hanging)
            struct timeval tv;
            tv.tv_sec = 30;
            tv.tv_usec = 0;
            setsockopt(ss_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            while (1) {
                n = read(ss_socket, response, sizeof(response) - 1);
                if (n <= 0) {
                    if (n == 0) {
                        printf("\n"); // Clean end
                    } else {
                        printf("\n✗ Connection lost or timeout during streaming\n");
                    }
                    break;
                }
                response[n] = '\0';
                
                // Check for end markers
                if (strstr(response, "STREAM_END") || strstr(response, "EOF") || 
                    strstr(response, "STOP") || strncmp(response, "ERR:", 4) == 0) {
                    printf("\n");
                    if (strncmp(response, "ERR:", 4) == 0) {
                        printf("%s", response);
                    }
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
            snprintf(command, sizeof(command), "WRITE %s %s %d\n", username, filename, sentence_num);
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
    printf("  REPLACE <file> <sentence#>  - Replace words in sentence\n");
    printf("  DELETE <filename>           - Delete file\n");
    printf("  INFO <filename>             - Show file details\n");
    printf("  STREAM <filename>           - Stream file word-by-word\n");
    printf("  UNDO <filename>             - Undo last change\n");
    printf("  LIST                        - List all users\n");
    printf("  ADDACCESS -R/-W <file> <user> - Grant access\n");
    printf("  REMACCESS <file> <user>     - Remove access\n");
    printf("  EXEC <filename>             - Execute file as commands\n");
    printf("  CREATEFOLDER <name>         - Create virtual folder\n");
    printf("  MOVE <file> <folder>        - Move file to folder\n");
    printf("  VIEWFOLDER <folder>         - List folder contents\n");
    printf("  LISTFOLDERS                 - List all folders\n");
    printf("\n[BONUS] Access Request Commands:\n");
    printf("  REQUESTACCESS <file> <-R|-W> - Request access to file\n");
    printf("  VIEWREQUESTS                 - View pending requests (owner)\n");
    printf("  APPROVEREQUEST <user> <file> - Approve access request\n");
    printf("  DENYREQUEST <user> <file>    - Deny access request\n");
    printf("\n  ↑/↓ Arrow Keys              - Navigate command history\n");
    printf("  Ctrl+R                      - Reverse history search\n");
    printf("\n[BONUS] Checkpoint Commands:\n");
    printf("  CHECKPOINT <file> <tag>     - Create checkpoint with tag\n");
    printf("  VIEWCHECKPOINT <file> <tag> - View checkpoint content\n");
    printf("  REVERT <file> <tag>         - Revert to checkpoint\n");
    printf("  LISTCHECKPOINTS <file>      - List all checkpoints\n");
    printf("  DIFF <file> <tag1> <tag2>   - Compare two checkpoints\n");
    printf("\n");
    printf("  HELP                        - Show this help\n");
    printf("  QUIT / EXIT                 - Disconnect\n\n");
}

// ============= REPLACE HANDLER (BONUS) =============

void handle_replace(const char* args) {
    // REPLACE <filename> <sentence_number>
    char filename[256];
    int sentence_num;
    if (sscanf(args, "%255s %d", filename, &sentence_num) != 2) {
        printf("Usage: REPLACE <filename> <sentence_number>\n");
        return;
    }

    char command[256];
    snprintf(command, sizeof(command), "REPLACE %s\n", filename);
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

            // Send REPLACE command to SS
            snprintf(command, sizeof(command), "REPLACE %s %s %d\n", username, filename, sentence_num);
            write(ss_socket, command, strlen(command));
            
            // Read acknowledgment
            n = read(ss_socket, response, sizeof(response) - 1);
            if (n <= 0) {
                printf("No response from SS\n");
                close(ss_socket);
                return;
            }
            response[n] = '\0';
            
            // Check if sentence locked
            if (strncmp(response, "ACK:SENTENCE_LOCKED", 19) == 0) {
                printf("%s", response);
                
                // Interactive loop
                char edit_line[1024];
                while (1) {
                    printf("edit> ");
                    fflush(stdout);
                    
                    if (!fgets(edit_line, sizeof(edit_line), stdin)) {
                        break;
                    }
                    
                    // Send to server
                    write(ss_socket, edit_line, strlen(edit_line));
                    
                    // Check if it's ECALPER
                    if (strncmp(edit_line, "ECALPER", 7) == 0) {
                        // Read final response
                        n = read(ss_socket, response, sizeof(response) - 1);
                        if (n > 0) {
                            response[n] = '\0';
                            printf("%s", response);
                            if (n > 0 && response[n-1] != '\n') {
                                printf("\n");
                            }
                        }
                        break;
                    }
                    
                    // Read response
                    n = read(ss_socket, response, sizeof(response) - 1);
                    if (n <= 0) {
                        printf("Connection lost\n");
                        break;
                    }
                    response[n] = '\0';
                    
                    // Print response and ensure newline
                    printf("%s", response);
                    if (n > 0 && response[n-1] != '\n') {
                        printf("\n");
                    }
                }
            } else {
                printf("%s", response);
            }
            
            close(ss_socket);
        }
    } else {
        printf("%s", response);
    }
}

// ============= CHECKPOINT HANDLERS =============

void handle_checkpoint(const char* args) {
    // CHECKPOINT <filename> <tag>
    char filename[256], tag[64];
    if (sscanf(args, "%255s %63s", filename, tag) != 2) {
        printf("Usage: CHECKPOINT <filename> <tag>\n");
        return;
    }
    
    // Ask NM for SS location
    char command[512];
    snprintf(command, sizeof(command), "CHECKPOINT %s\n", filename);
    write(nm_socket, command, strlen(command));
    
    char response[4096];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("Error communicating with Name Server\n");
        return;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        // Parse SS info: ACK:SS_INFO <ip> <port>
        char ss_ip[32];
        int ss_port;
        if (sscanf(response, "ACK:SS_INFO %31s %d", ss_ip, &ss_port) == 2) {
            // Connect to SS
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);
            
            if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                // Send CHECKPOINT command to SS
                snprintf(command, sizeof(command), "CHECKPOINT %s %s %s\n", username, filename, tag);
                write(ss_socket, command, strlen(command));
                
                // Read response from SS
                n = read(ss_socket, response, sizeof(response) - 1);
                response[n] = '\0';
                printf("%s", response);
                
                close(ss_socket);
            } else {
                printf("Error: Could not connect to Storage Server\n");
            }
        }
    } else {
        printf("%s", response);
    }
}

void handle_viewcheckpoint(const char* args) {
    // VIEWCHECKPOINT <filename> <tag>
    char filename[256], tag[64];
    if (sscanf(args, "%255s %63s", filename, tag) != 2) {
        printf("Usage: VIEWCHECKPOINT <filename> <tag>\n");
        return;
    }
    
    // Ask NM for SS location
    char command[512];
    snprintf(command, sizeof(command), "VIEWCHECKPOINT %s\n", filename);
    write(nm_socket, command, strlen(command));
    
    char response[4096];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("Error communicating with Name Server\n");
        return;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        char ss_ip[32];
        int ss_port;
        if (sscanf(response, "ACK:SS_INFO %31s %d", ss_ip, &ss_port) == 2) {
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);
            
            if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                snprintf(command, sizeof(command), "VIEWCHECKPOINT %s %s %s\n", username, filename, tag);
                write(ss_socket, command, strlen(command));
                
                // Read content from SS
                while ((n = read(ss_socket, response, sizeof(response) - 1)) > 0) {
                    response[n] = '\0';
                    printf("%s", response);
                    if (strstr(response, "EOF")) break;
                }
                
                close(ss_socket);
            } else {
                printf("Error: Could not connect to Storage Server\n");
            }
        }
    } else {
        printf("%s", response);
    }
}

void handle_revert(const char* args) {
    // REVERT <filename> <tag>
    char filename[256], tag[64];
    if (sscanf(args, "%255s %63s", filename, tag) != 2) {
        printf("Usage: REVERT <filename> <tag>\n");
        return;
    }
    
    // Ask NM for SS location
    char command[512];
    snprintf(command, sizeof(command), "REVERT %s\n", filename);
    write(nm_socket, command, strlen(command));
    
    char response[4096];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("Error communicating with Name Server\n");
        return;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        char ss_ip[32];
        int ss_port;
        if (sscanf(response, "ACK:SS_INFO %31s %d", ss_ip, &ss_port) == 2) {
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);
            
            if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                snprintf(command, sizeof(command), "REVERT %s %s %s\n", username, filename, tag);
                write(ss_socket, command, strlen(command));
                
                n = read(ss_socket, response, sizeof(response) - 1);
                response[n] = '\0';
                printf("%s", response);
                
                close(ss_socket);
            } else {
                printf("Error: Could not connect to Storage Server\n");
            }
        }
    } else {
        printf("%s", response);
    }
}

void handle_listcheckpoints(const char* args) {
    // LISTCHECKPOINTS <filename>
    char filename[256];
    if (sscanf(args, "%255s", filename) != 1) {
        printf("Usage: LISTCHECKPOINTS <filename>\n");
        return;
    }
    
    // Ask NM for SS location
    char command[512];
    snprintf(command, sizeof(command), "LISTCHECKPOINTS %s\n", filename);
    write(nm_socket, command, strlen(command));
    
    char response[4096];
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("Error communicating with Name Server\n");
        return;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        char ss_ip[32];
        int ss_port;
        if (sscanf(response, "ACK:SS_INFO %31s %d", ss_ip, &ss_port) == 2) {
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);
            
            if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                snprintf(command, sizeof(command), "LISTCHECKPOINTS %s %s\n", username, filename);
                write(ss_socket, command, strlen(command));
                
                n = read(ss_socket, response, sizeof(response) - 1);
                response[n] = '\0';
                printf("%s\n", response);
                
                close(ss_socket);
            } else {
                printf("Error: Could not connect to Storage Server\n");
            }
        }
    } else {
        printf("%s", response);
    }
}

void handle_diff(const char* args) {
    // DIFF <filename> <tag1> <tag2>
    char filename[256], tag1[64], tag2[64];
    if (sscanf(args, "%255s %63s %63s", filename, tag1, tag2) != 3) {
        printf("Usage: DIFF <filename> <tag1> <tag2>\n");
        return;
    }
    
    // Ask NM for SS location
    char command[512];
    snprintf(command, sizeof(command), "DIFF %s %s %s\n", filename, tag1, tag2);
    write(nm_socket, command, strlen(command));
    
    char response[102400]; // Large buffer for diff output
    ssize_t n = read(nm_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        printf("Error communicating with Name Server\n");
        return;
    }
    response[n] = '\0';
    
    if (strncmp(response, "ACK:", 4) == 0) {
        char ss_ip[32];
        int ss_port;
        if (sscanf(response, "ACK:SS_INFO %31s %d", ss_ip, &ss_port) == 2) {
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);
            
            if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                snprintf(command, sizeof(command), "DIFF %s %s %s\n", filename, tag1, tag2);
                write(ss_socket, command, strlen(command));
                
                // Read potentially large diff output
                char buffer[4096];
                while ((n = read(ss_socket, buffer, sizeof(buffer) - 1)) > 0) {
                    buffer[n] = '\0';
                    printf("%s", buffer);
                    if (n < sizeof(buffer) - 1) break; // Last chunk
                }
                printf("\n");
                
                close(ss_socket);
            } else {
                printf("Error: Could not connect to Storage Server\n");
            }
        }
    } else {
        printf("%s", response);
    }
}

// ============= END CHECKPOINT HANDLERS =============

int main(int argc, char* argv[]) {
    // Parse command line arguments for NM IP
    if (argc > 1) {
        strncpy(nm_ip, argv[1], sizeof(nm_ip) - 1);
        nm_ip[sizeof(nm_ip) - 1] = '\0';
        printf("Using NM IP: %s\n", nm_ip);
    }
    
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

    printf("Connecting to Name Server at %s:%d...\n", nm_ip, NM_PORT);
    if (connect_to_nm() < 0) {
        return 1;
    }

    if (register_with_nm() < 0) {
        close(nm_socket);
        return 1;
    }

    printf("\nType 'HELP' for available commands\n");
    printf("Use UP/DOWN arrow keys to navigate command history\n\n");

    // Main command loop with readline
    char* line;
    while (1) {
        line = readline(get_prompt());
        
        // Handle EOF (Ctrl+D)
        if (!line) {
            printf("\n");
            break;
        }
        
        // Skip empty lines
        if (strlen(line) == 0) {
            free(line);
            continue;
        }
        
        // Add to history
        add_history(line);

        // Parse command
        char cmd[64];
        char args[448];
        memset(args, 0, sizeof(args));
        
        if (sscanf(line, "%63s", cmd) != 1) {
            free(line);
            continue;
        }
        
        char* space = strchr(line, ' ');
        if (space) {
            strncpy(args, space + 1, sizeof(args) - 1);
        }

        // Handle commands
        if (strcasecmp(cmd, "QUIT") == 0 || strcasecmp(cmd, "EXIT") == 0) {
            free(line);
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
        } else if (strcasecmp(cmd, "REPLACE") == 0) {
            handle_replace(args);
        } else if (strcasecmp(cmd, "CHECKPOINT") == 0) {
            handle_checkpoint(args);
        } else if (strcasecmp(cmd, "VIEWCHECKPOINT") == 0) {
            handle_viewcheckpoint(args);
        } else if (strcasecmp(cmd, "REVERT") == 0) {
            handle_revert(args);
        } else if (strcasecmp(cmd, "LISTCHECKPOINTS") == 0) {
            handle_listcheckpoints(args);
        } else if (strcasecmp(cmd, "DIFF") == 0) {
            handle_diff(args);
        } else {
            // All other commands go directly to NM
            send_simple_command(line);
        }
        
        // Free readline buffer
        free(line);
    }

    printf("\nDisconnecting...\n");
    close(nm_socket);
    return 0;
}
