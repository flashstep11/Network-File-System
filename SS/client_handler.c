#include "defs.h" // This has all your system headers
#include "log.h"  // For the logger
#include "client_handler.h" // For its own declaration

int check_permissions(int client_socket, const char *filename, const char *mode) {
    logger("[AccessControl] Checking %s permission for file %s\n", mode, filename);
    
    // 1. Get Client IP Address
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        logger("[AccessControl] getpeername failed\n");
        perror("getpeername failed");
        return 0; // Fail safe
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    logger("[AccessControl] Client IP: %s\n", client_ip);

    // 2. Connect to NM
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        logger("[AccessControl] Failed to create socket to NM\n");
        return 0;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        logger("[AccessControl] Invalid NM IP\n");
        close(nm_sock);
        return 0;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        logger("[AccessControl] Could not reach NM to verify permissions.\n");
        close(nm_sock);
        return 0; // If NM is down, deny access for security
    }
    
    logger("[AccessControl] Connected to NM\n");

    // 3. Send Query: CHECK_ACCESS <IP> <FILENAME> <MODE>
    char query[512];
    sprintf(query, "CHECK_ACCESS %s %s %s\n", client_ip, filename, mode);
    write(nm_sock, query, strlen(query));
    logger("[AccessControl] Sent query: %s", query);

    // 4. Read Response
    char response[64] = {0};
    ssize_t n = read(nm_sock, response, 63);
    logger("[AccessControl] Received response (%zd bytes): %s\n", n, response);
    close(nm_sock);

    if (strncmp(response, "ACK:YES", 7) == 0) {
        logger("[AccessControl] Access GRANTED\n");
        return 1; // Allowed
    }

    logger("[AccessControl] Denied access to %s for %s on file %s\n", client_ip, mode, filename);
    return 0; // Denied
}
void handle_read_command(int client_socket, char* buffer) {
    char filename[256]; // Use 256 for a standard filename buffer

    // 1. --- Parse the command ---
    // Command format: READ <filename>\n
    if (sscanf(buffer, "READ %255s", filename) != 1) {
        char *err = "ERR:400:BAD_READ_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return; // Return from this function
    }

    logger("[SS-Thread] Read request for: %s\n", filename);

    if (!check_permissions(client_socket, filename, "READ")) {
        char *err = "ERR:403:ACCESS_DENIED\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 3. --- Read the file ---
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long file_size = 0;
    char *file_content = read_file_to_string(full_path, &file_size);

    if (file_content != NULL) {
        // --- Success ---
        // Found the file, send all of its content.
        write(client_socket, file_content, file_size);
        
        // Send EOF marker so client knows to stop reading
        write(client_socket, "\nEOF\n", 5); 

        // Clean up
        free(file_content);
    } else {
        // --- Failure ---
        // read_file_to_string returned NULL (e.g., file not found).
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
    }
}
void handle_write_command(int client_socket, char* buffer) {
    char filename[256];
    int sentence_num;
    
    // 1. Parse - Interactive mode: WRITE <filename> <sentence_num>
    // Client will then send multiple "<word_index> <content>" lines
    if (sscanf(buffer, "WRITE %255s %d", filename, &sentence_num) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: WRITE <filename> <sentence_num>)\n", 61);
        return;
    }
    
    logger("[SS-Thread] WRITE request: %s sentence %d\n", filename, sentence_num);

    // 2. ACCESS CONTROL
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        logger("[SS-Thread] WRITE denied - no permission\n");
        return;
    }

    // 3. SENTENCE LOCK (The Critical Requirement)
    if (!acquire_sentence_lock(filename, sentence_num)) {
        char *err = "ERR:423:SENTENCE_LOCKED_BY_ANOTHER_USER\n";
        write(client_socket, err, strlen(err));
        logger("[SS-Thread] WRITE denied - sentence locked\n");
        return;
    }
    
    logger("[SS-Thread] Sentence lock acquired for %s sentence %d\n", filename, sentence_num);
    
    // Send ACK to client to start interactive editing
    write(client_socket, "ACK:SENTENCE_LOCKED Enter word updates (word_index content) or ETIRW to finish\n", 81);

    // 4. Load file and create backup
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    if (!f_mutex) {
        write(client_socket, "ERR:500:INTERNAL_ERROR\n", 23);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    pthread_mutex_lock(f_mutex);

    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);

    long fsize = 0;
    char* file_data = read_file_to_string(full_path, &fsize);
    
    logger("[SS-Thread] Loaded file %s, size=%ld, data=%p\n", filename, fsize, (void*)file_data);
    
    if (!file_data) {
        file_data = strdup(""); // Empty file
        if (!file_data) {
            write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            return;
        }
        logger("[SS-Thread] File is empty, created empty buffer\n");
    }
    
    // Create backup for UNDO
    char bak_name[512];
    snprintf(bak_name, sizeof(bak_name), "%s%s.bak", STORAGE_ROOT, filename);
    FILE *bak = fopen(bak_name, "w");
    if (bak) { 
        fprintf(bak, "%s", file_data); 
        fclose(bak); 
        logger("[SS-Thread] Backup created: %s\n", bak_name);
    }
    
    // Make a working copy we'll modify
    char* working_data = strdup(file_data);
    if (!working_data) {
        write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
        free(file_data);
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // 5. Interactive editing loop
    char edit_buffer[1024];
    while (1) {
        memset(edit_buffer, 0, sizeof(edit_buffer));
        ssize_t n = read(client_socket, edit_buffer, sizeof(edit_buffer) - 1);
        
        if (n <= 0) {
            logger("[SS-Thread] Client disconnected during WRITE\n");
            break;
        }
        
        edit_buffer[n] = '\0';
        // Remove trailing newline
        char* nl = strchr(edit_buffer, '\n');
        if (nl) *nl = '\0';
        
        // Check for ETIRW (end editing)
        if (strncmp(edit_buffer, "ETIRW", 5) == 0) {
            logger("[SS-Thread] ETIRW received, finalizing write\n");
            
            // Write working data to file
            FILE *fp = fopen(full_path, "w");
            if (fp) {
                fprintf(fp, "%s", working_data);
                fclose(fp);
                write(client_socket, "ACK:WRITE_COMPLETE All changes saved\n", 38);
                logger("[SS-Thread] File %s updated successfully\n", filename);
            } else {
                write(client_socket, "ERR:500:WRITE_FAILED Could not save file\n", 42);
                logger("[SS-Thread] Failed to write file %s\n", filename);
            }
            break;
        }
        
        // Parse word update: <word_index> <content>
        int word_index;
        char content[900];
        if (sscanf(edit_buffer, "%d %899[^\n]", &word_index, content) == 2) {
            logger("[SS-Thread] Parse OK: word_index=%d, content='%s', working_data='%s'\n", 
                   word_index, content, working_data);
            
            // Handle empty file or append operation
            size_t current_len = strlen(working_data);
            
            // Try to find the sentence first
            int s_start = 0, s_end = 0;
            int sentence_found = find_sentence(working_data, sentence_num, &s_start, &s_end);
            
            logger("[SS-Thread] find_sentence returned %d, s_start=%d, s_end=%d\n", 
                   sentence_found, s_start, s_end);
            
            if (!sentence_found && sentence_num == 0) {
                // Sentence 0 doesn't exist yet - we're building it from scratch
                // Just append words with spaces
                char* new_data;
                if (current_len == 0) {
                    // First word in empty file
                    new_data = (char*)malloc(strlen(content) + 1);
                    if (new_data) {
                        strcpy(new_data, content);
                    }
                } else {
                    // Append with space
                    new_data = (char*)malloc(current_len + strlen(content) + 2);
                    if (new_data) {
                        strcpy(new_data, working_data);
                        strcat(new_data, " ");
                        strcat(new_data, content);
                    }
                }
                
                if (new_data) {
                    free(working_data);
                    working_data = new_data;
                    write(client_socket, "ACK:WORD_UPDATED\n", 17);
                    logger("[SS-Thread] Added word %d to sentence %d\n", word_index, sentence_num);
                } else {
                    write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                }
                continue;
            }
            
            if (!sentence_found) {
                write(client_socket, "ERR:404:SENTENCE_NOT_FOUND\n", 28);
                continue;
            }
            
            // Sentence exists, now find the word
            int w_start, w_end;
            if (find_word(working_data, s_start, s_end, word_index, &w_start, &w_end)) {
                // Word exists - replace it
                int len_before = w_start;
                int len_new = strlen(content);
                size_t working_len = strlen(working_data);
                
                // Safety check
                if (w_end >= (int)working_len) {
                    write(client_socket, "ERR:500:INTERNAL_ERROR Invalid word bounds\n", 44);
                    logger("[SS-Thread] ERROR: w_end=%d >= working_len=%zu\n", w_end, working_len);
                    continue;
                }
                
                int len_after = working_len - w_end - 1;
                
                char* new_data = (char*)malloc(len_before + len_new + len_after + 1);
                if (new_data) {
                    memcpy(new_data, working_data, len_before);
                    memcpy(new_data + len_before, content, len_new);
                    if (len_after > 0) {
                        memcpy(new_data + len_before + len_new, working_data + w_end + 1, len_after);
                    }
                    new_data[len_before + len_new + len_after] = '\0';
                    
                    free(working_data);
                    working_data = new_data;
                    
                    write(client_socket, "ACK:WORD_UPDATED\n", 17);
                    logger("[SS-Thread] Replaced word %d in sentence %d\n", word_index, sentence_num);
                } else {
                    write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                }
            } else {
                // Word doesn't exist in sentence - append it
                int insert_pos = s_end + 1;
                int len_new = strlen(content);
                int new_total = current_len + len_new + 2; // +1 for space, +1 for null
                
                char* new_data = (char*)malloc(new_total);
                if (new_data) {
                    // Copy up to end of sentence
                    memcpy(new_data, working_data, insert_pos);
                    // Add space and new word
                    new_data[insert_pos] = ' ';
                    strcpy(new_data + insert_pos + 1, content);
                    // Copy rest of file
                    if (insert_pos < (int)current_len) {
                        strcat(new_data, working_data + insert_pos);
                    }
                    
                    free(working_data);
                    working_data = new_data;
                    write(client_socket, "ACK:WORD_UPDATED\n", 17);
                    logger("[SS-Thread] Appended word %d to sentence %d\n", word_index, sentence_num);
                } else {
                    write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                }
            }
        } else {
            write(client_socket, "ERR:400:BAD_FORMAT Use: word_index content\n", 44);
        }
    }
    
    // Cleanup
    if (file_data) free(file_data);
    if (working_data) free(working_data);
    
    // Release locks
    pthread_mutex_unlock(f_mutex);
    release_sentence_lock(filename, sentence_num);
    logger("[SS-Thread] Sentence lock released for %s sentence %d\n", filename, sentence_num);
}
void handle_write_command_old_single_shot(int client_socket, char* buffer) {
    // OLD IMPLEMENTATION - KEPT FOR REFERENCE
    char filename[256];
    int sentence_num, word_index;
    // 1. Parse
    // Command: WRITE <filename> <sentence_num> <word_index> <content...>
    if (sscanf(buffer, "WRITE %255s %d %d", filename, &sentence_num, &word_index) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST\n", 20);
        return;
    }

    // Find start of content
    char *content = buffer;
    int spaces = 0;
    while(*content && spaces < 4){ if(*content++ == ' ') spaces++; }
    // Trim newline
    char *nl = strrchr(content, '\n');
    if(nl) *nl = '\0';

    // 2. ACCESS CONTROL
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }

    // 3. SENTENCE LOCK (The Critical Requirement)
    // This check ensures no one else is editing THIS sentence right now.
    if (!acquire_sentence_lock(filename, sentence_num)) {
        // Fulfills "Attempting to write a locked sentence" error requirement
        char *err = "ERR:503:SENTENCE_LOCKED_BY_ANOTHER_USER\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 4. Perform the Read-Modify-Write
    // We grab the physical file mutex now.
    // Even though this part is serialized, the "Sentence Lock" above allowed
    // us to conceptually separate the locking domains.
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    pthread_mutex_lock(f_mutex); 

    long fsize;
    char* file_data = read_file_to_string(filename, &fsize);
    
    if (!file_data) {
        if (sentence_num == 0) { // Creating/Appending to empty file allowed?
             file_data = strdup("");
        } else {
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            write(client_socket, "ERR:404:FILE_NOT_FOUND\n", 23);
            return;
        }
    }

    // --- LOGIC TO REPLACE WORD/SENTENCE ---
    int s_start, s_end;
    char *new_file_data = NULL;

    if (find_sentence(file_data, sentence_num, &s_start, &s_end)) {
        // Sentence found. Now find word? 
        // Note: Requirement 30 says "update content at a word level".
        // If word_index is valid, replace word. 
        // Implementation of string manipulation is standard C (malloc, strncpy, strcat)...
        // [Omitted for brevity, but assume new_file_data is created here]
        
        // Placeholder logic: Append content for testing
        // Ideally: new_file_data = replace_word_in_str(file_data, s_start, ... content);
        int w_start, w_end;
    
    // Attempt to find the specific word within the found sentence
    if (find_word(file_data, s_start, s_end, word_index, &w_start, &w_end)) {
        
        // --- 1. Calculate new size ---
        int len_before = w_start;                      // Data before the word
        int len_new_content = strlen(content);         // The new text
        int len_after = strlen(file_data + w_end + 1); // Data after the word (including null term)
        
        // --- 2. Allocate Memory ---
        new_file_data = (char *)malloc(len_before + len_new_content + len_after + 1);
        if (new_file_data == NULL) {
            write(client_socket, "ERR:500:MEMORY_ALLOC_FAILED\n", 28);
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            free(file_data);
            return;
        }

        // --- 3. Construct New String ---
        // Copy the part before the word
        memcpy(new_file_data, file_data, len_before);
        
        // Copy the new word/content
        memcpy(new_file_data + len_before, content, len_new_content);
        
        // Copy the part after the word (strcpy handles the null terminator)
        strcpy(new_file_data + len_before + len_new_content, file_data + w_end + 1);

    } else {
        // --- Word Index Not Found ---
        // As per specifications, this is an error (unless you implement specific append logic)
        write(client_socket, "ERR:404:WORD_INDEX_OUT_OF_BOUNDS\n", 33);
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        free(file_data);
        return;
    }
    } 
    else {
        // Error handling for invalid sentence index
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        free(file_data);
        write(client_socket, "ERR:404:SENTENCE_NOT_FOUND\n", 27);
        return;
    }

    // 5. Create Backup (For UNDO)
    char bak_name[300];
    sprintf(bak_name, "%s.bak", filename);
    FILE *bak = fopen(bak_name, "w");
    if (bak) { fprintf(bak, "%s", file_data); fclose(bak); }

    // 6. Write New Data
    // 6. Write New Data to Disk
    FILE *fp = fopen(filename, "w");
    if (fp) {
        // Use the new constructed string
        if (new_file_data) {
            fprintf(fp, "%s", new_file_data);
        } else {
            // Fallback for empty file creation cases if necessary
            fprintf(fp, "%s", file_data); 
        }
        fclose(fp);
        write(client_socket, "ACK:WRITE_SUCCESS\n", 18);
    } else {
        write(client_socket, "ERR:500:WRITE_FAILED\n", 21);
    }

    // Clean up
    if (file_data) free(file_data);
    if (new_file_data) free(new_file_data);
    // if(new_file_data) free(new_file_data);

    // 7. RELEASE LOCKS
    pthread_mutex_unlock(f_mutex);
    release_sentence_lock(filename, sentence_num);
}
void handle_undo_command(int client_socket, char* buffer) {
    char filename[256];
    char bak_filename[512];

    // 1. --- Parse the command ---
    // Command from Client: "UNDO <filename>\n"
    if (sscanf(buffer, "UNDO %255s", filename) != 1) {
        char *err = "ERR:400:BAD_UNDO_COMMAND\n";
        logger("[SS-Thread] Failed to parse UNDO command.\n");
        write(client_socket, err, strlen(err));
        return;
    }

    // 2. --- ACCESS CONTROL CHECK ---
    // A user must have WRITE permission to perform an UNDO
    if (!check_permissions(client_socket, filename, "WRITE")) {
        char *err = "ERR:403:ACCESS_DENIED\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    // 3. --- Form the backup file name and real file paths ---
    char full_path[512], bak_full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    snprintf(bak_full_path, sizeof(bak_full_path), "%s%s.bak", STORAGE_ROOT, filename);

    // 4. --- Perform the Atomic Swap ---
    // We lock the file mutex to prevent a race condition with a WRITE
    pthread_mutex_t* file_lock = get_file_mutex(filename);
    if (!file_lock) { char *err = "ERR:500:INTERNAL_SERVER_ERROR (LockManager failed)\n";
        logger("[SS-Thread] UNDO: CRITICAL: Failed to get/create lock for %s. (Out of memory?)\n", filename);
        write(client_socket, err, strlen(err));
        return; }

    logger("[SS-Thread] UNDO: Attempting to lock file %s...\n", filename);
    pthread_mutex_lock(file_lock);

    if (rename(bak_full_path, full_path) == 0) {
        // --- Success ---
        char *ack = "ACK:UNDO_OK\n";
        logger("[SS-Thread] Successfully restored backup for: %s\n", filename);
        write(client_socket, ack, strlen(ack));
    } else {
        // --- Failure ---
        char *err = "ERR:404:UNDO_FAILED (No backup found)\n";
        logger("[SS-Thread] Failed to find backup for %s: %s\n", filename, strerror(errno));
        write(client_socket, err, strlen(err));
    }
    
    pthread_mutex_unlock(file_lock);
    logger("[SS-Thread] UNDO: Unlocked file %s.\n", filename);
}
void handle_stream_command(int client_socket, char* buffer) {
    char filename[256];
    
    // 1. --- Parse the command ---
    // Command format: STREAM <filename>\n
    if (sscanf(buffer, "STREAM %255s", filename) != 1) {
        char *err = "ERR:400:BAD_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return; // Return from this function
    }
    if (!check_permissions(client_socket, filename, "READ")) {
        char *err = "ERR:403:ACCESS_DENIED\n";
        write(client_socket, err, strlen(err));
        return;
    }
    logger("[SS-Thread] Stream request for: %s\n", filename);

    // 2. --- Read the entire file into memory ---
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long file_size = 0;
    char *file_content = read_file_to_string(full_path, &file_size);

    if (file_content == NULL) {
        // --- Error Handling: File Not Found ---
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 3. --- Stream the file, preserving formatting ---
    const char *delims = " \t\n\r";
    char *ptr = file_content; // Our "cursor" walking through the file

    while (*ptr != '\0') {
        int bytes_sent;

        // --- a. Find a "word" chunk (non-delimiters) ---
        int word_len = strcspn(ptr, delims);

        if (word_len > 0) {
            bytes_sent = write(client_socket, ptr, word_len);
            if (bytes_sent < 0) {
                perror("[SS-Thread] Client disconnected mid-stream");
                break; // Exit loop
            }
            usleep(100000); // 0.1 second delay
            ptr += word_len; // Move cursor past the word
        }

        // --- b. Find a "delimiter" chunk (whitespace/newlines) ---
        int delim_len = strspn(ptr, delims);

        if (delim_len > 0) {
            bytes_sent = write(client_socket, ptr, delim_len);
            if (bytes_sent < 0) {
                perror("[SS-Thread] Client disconnected mid-stream");
                break; // Exit loop
            }
            ptr += delim_len; // Move cursor past the whitespace
        }
    }
    
    // 4. --- Send the "STOP" packet ---
    char *stop_packet = "\nSTREAM_END\n";
    write(client_socket, stop_packet, strlen(stop_packet));
    
    // 5. --- Clean up ---
    free(file_content);
}
void *handle_client_request(void *arg) {
    // 1. Get the client socket from the argument
    int client_socket = *(int *)arg;
    free(arg); // Free the heap memory for the socket pointer

    char buffer[BUFFER_SIZE];
    int read_size;

    logger("[SS-Thread] New client connected. Waiting for commands...\n");

    // 2. Loop to read commands from this client
    // read() blocks until data is received or connection is closed
    while ((read_size = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        // Null-terminate the received string
        buffer[read_size] = '\0';
        logger("[SS-Thread] Received command: %s", buffer);
        if (strncmp(buffer, "CREATE", 6) == 0 || strncmp(buffer, "DELETE", 6) == 0 )
        {
            char *err = "ERR:401:UNAUTHORIZED (Clients cannot use this command)\n";
            write(client_socket, err, strlen(err));
            continue; // Ignore and wait for next command
        }
        else if (strncmp(buffer, "UNDO", 4) == 0) {
            handle_undo_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "READ", 4) == 0) {
            handle_read_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "WRITE", 5) == 0) {
          handle_write_command(client_socket, buffer);
        } 
        else if (strncmp(buffer, "STREAM", 6) == 0) {
            // Just call your new function
            handle_stream_command(client_socket, buffer);
        }

        else {
            // Unknown command
            char *err = "ERR:400:UNKNOWN_COMMAND\n";
            write(client_socket, err, strlen(err));
        }
         // 3. Send a generic ACK back to the client
        // write(client_socket, "ACK: Command received and processed.\n", 37);
        // Clear the buffer for the next read
        memset(buffer, 0, BUFFER_SIZE);
    }

    // 4. Handle client disconnect
    if (read_size == 0) {
        logger("[SS-Thread] Client disconnected.\n");
    } else if (read_size == -1) {
        perror("[SS-Thread] read failed");
    }

    // 5. Clean up: close the socket and exit the thread
    close(client_socket);
    pthread_exit(NULL);
}

void start_client_storage_server_loop(int my_port) {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;

    // 1. Create the SS's own server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("SS Server: socket failed");
        exit(EXIT_FAILURE);
    }

    // Optional: Allow port reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }

    // 2. Bind the server socket to *your* port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(my_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("SS Server: bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("SS Server: listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    logger("\n--- Storage Server is now ONLINE. Listening on port %d ---\n\n", my_port);

    // 4. Main accept() loop
    // This loop runs forever, waiting for new connections
    while (1) {
        client_addr_len = sizeof(client_addr);
        
        // accept() blocks and waits for a new connection
        client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("SS Server: accept failed");
            continue; // Skip this failed connection and wait for the next
        }

        logger("[SS-Main] New connection accepted! Spawning thread...\n");

        // 5. Create a new thread for this client
        pthread_t thread_id;
        
        // We must pass the client_socket on the heap
        // so it's unique for each thread.
        int *p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_create(&thread_id, NULL, handle_client_request, (void *)p_client_socket) < 0) {
            perror("pthread_create failed");
            free(p_client_socket); // Clean up if thread creation failed
            close(client_socket);
        }

        // Detach the thread - we don't need to join() it.
        // The OS will reclaim its resources when it exits.
        pthread_detach(thread_id);
    }

    // This code is never reached
    close(server_fd);
}
