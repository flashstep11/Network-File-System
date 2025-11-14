#include "defs.h" // This has all your system headers
#include "log.h"  // For the logger
#include "client_handler.h" // For its own declaration

int check_permissions(int client_socket, const char *filename, const char *mode) {
    // 1. Get Client IP Address
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        perror("getpeername failed");
        return 0; // Fail safe
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    // 2. Connect to NM
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) return 0;

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        close(nm_sock);
        return 0;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        logger("[AccessControl] Could not reach NM to verify permissions.\n");
        close(nm_sock);
        return 0; // If NM is down, deny access for security
    }

    // 3. Send Query: CHECK_ACCESS <IP> <FILENAME> <MODE>
    char query[512];
    sprintf(query, "CHECK_ACCESS %s %s %s\n", client_ip, filename, mode);
    write(nm_sock, query, strlen(query));

    // 4. Read Response
    char response[64] = {0};
    read(nm_sock, response, 63);
    close(nm_sock);

    if (strncmp(response, "ACK:YES", 7) == 0) {
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
    long file_size = 0;
    // This assumes you have the read_file_to_string() helper
    char *file_content = read_file_to_string(filename, &file_size);

    if (file_content != NULL) {
        // --- Success ---
        // Found the file, send all of its content.
        write(client_socket, file_content, file_size);
        
        // Send a newline just in case the file doesn't end with one,
        // to keep the client's prompt clean.
        write(client_socket, "\n", 1); 

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
    
    // 3. --- Form the backup file name ---
    sprintf(bak_filename, "%s.bak", filename);

    // 4. --- Perform the Atomic Swap ---
    // We lock the file mutex to prevent a race condition with a WRITE
    pthread_mutex_t* file_lock = get_lock_for_file(filename);
    if (!file_lock) { char *err = "ERR:500:INTERNAL_SERVER_ERROR (LockManager failed)\n";
        logger("[SS-Thread] UNDO: CRITICAL: Failed to get/create lock for %s. (Out of memory?)\n", filename);
        write(client_socket, err, strlen(err));
        return; }

    logger("[SS-Thread] UNDO: Attempting to lock file %s...\n", filename);
    pthread_mutex_lock(file_lock);

    if (rename(bak_filename, filename) == 0) {
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
    long file_size = 0;
    // This assumes you have the read_file_to_string() helper
    char *file_content = read_file_to_string(filename, &file_size);

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
