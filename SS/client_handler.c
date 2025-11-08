#include "defs.h" // This has all your system headers
#include "log.h"  // For the logger
#include "client_handler.h" // For its own declaration
/**
 * @brief Handles the logic for a "READ" command.
 * This function is called by handle_client_request.
 * It sends the full contents of a requested file.
 *
 * @param client_socket The socket of the connected client.
 * @param buffer The command buffer, starting with "READ...".
 */
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

    // 2. --- TODO: Check Permissions ---
    // This is the spot. You will need to ask the Name Server
    // (on the private command line) if this client
    // has permission to read this file. For now, we'll assume yes.
    //
    // if (!check_permissions(client_username, filename, "READ")) {
    //     char *err = "ERR:401:UNAUTHORIZED_ACCESS\n";
    //     write(client_socket, err, strlen(err));
    //     return;
    // }

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
/**
 * @brief Handles the logic for a "WRITE" command (with word_index).
 * This function is called by handle_client_request.
 */

void handle_write_command(int client_socket, char* buffer) {
    
    // 1. --- Parse the command ---
    // Command format: WRITE <filename> <sentence_num> <word_index> <content...>\n
    char filename[256];
    int sentence_num, word_index;
    char *new_content = NULL;

    // Use sscanf to parse the first *three* parts
    if (sscanf(buffer, "WRITE %255s %d %d", filename, &sentence_num, &word_index) != 3) {
        char *err = "ERR:400:BAD_WRITE_COMMAND\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    // Find the start of the actual content (after the 3rd space)
    char *first_space = strchr(buffer, ' ');
    if (!first_space) { /*... error ...*/ return; }
    
    char *second_space = strchr(first_space + 1, ' ');
    if (!second_space) { /*... error ...*/ return; }

    char *third_space = strchr(second_space + 1, ' ');
    if (!third_space) {
        char *err = "ERR:400:BAD_WRITE_COMMAND (missing content)\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    new_content = third_space + 1; // Content starts after the 3rd space
    
    // Trim trailing newline from new_content if it exists
    char *newline = strrchr(new_content, '\n');
    if (newline) *newline = '\0';
    
    // 2. --- Get the file-level lock ---
    pthread_mutex_t* file_lock = get_lock_for_file(filename);
    if (file_lock == NULL) {
        char *err = "ERR:500:LOCK_MANAGER_FAILURE\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 3. --- LOCK (This is the Level 2 "Room Key" lock) ---
    logger("[SS-Thread] WRITE: Attempting to lock file %s...\n", filename);
    pthread_mutex_lock(file_lock);
    logger("[SS-Thread] WRITE: Locked file %s.\n", filename);

    // --- CRITICAL SECTION STARTS ---

    // 4. --- Start Atomic Swap ---
    long file_size = 0;
    char *original_content = read_file_to_string(filename, &file_size);
    
    if (original_content == NULL) {
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        pthread_mutex_unlock(file_lock);
        return;
    }

    int sent_start_index = 0, sent_end_index = 0;
    char *modified_content = NULL;
    if (file_size == 0 && sentence_num == 1 && word_index == 1) {

        logger("[SS-Thread] WRITE: Handling empty file case.\n");
        // The file is empty, just make the new content the modified content
        modified_content = strdup(new_content);

    }
    // First, find the sentence
    else if (find_sentence(original_content, sentence_num, &sent_start_index, &sent_end_index)) {
        
        int word_start_index = 0, word_end_index = 0;
        
        // Second, find the word *within* that sentence
        if (find_word(original_content, sent_start_index, sent_end_index, 
                      word_index, &word_start_index, &word_end_index))
        {
            // --- Success: Found the word ---
            int len_before = word_start_index;
            int len_new = strlen(new_content);
            int len_after = strlen(original_content + word_end_index + 1);

            modified_content = (char *)malloc(len_before + len_new + len_after + 1);
            if (modified_content == NULL) { /*... handle alloc error ...*/ }

            // Copy the part *before* the word
            strncpy(modified_content, original_content, len_before);
            modified_content[len_before] = '\0';

            // Concatenate the *new* word
            strcat(modified_content, new_content);

            // Concatenate the part *after* the word
            strcat(modified_content, original_content + word_end_index + 1);

        } else {
            // The specified word number wasn't found
            char *err = "ERR:404:WORD_NOT_FOUND\n";
            write(client_socket, err, strlen(err));
            free(original_content);
            pthread_mutex_unlock(file_lock);
            return;
        }

    } else {
        // The specified sentence number wasn't found
        char *err = "ERR:404:SENTENCE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        free(original_content);
        pthread_mutex_unlock(file_lock);
        return;
    }
    
    // We are done with the original string, free it
    free(original_content);

    // 5. --- Write to a temporary file ---
    char temp_filename[512];
    sprintf(temp_filename, "%s.%ld.temp", filename, (long)pthread_self());

    FILE *temp_file = fopen(temp_filename, "wb");
    if (temp_file == NULL) {
        char *err = "ERR:500:COULD_NOT_CREATE_TEMP_FILE\n";
        write(client_socket, err, strlen(err));
        free(modified_content);
        pthread_mutex_unlock(file_lock);
        return;
    }
    
    fwrite(modified_content, 1, strlen(modified_content), temp_file);
    fclose(temp_file);
    free(modified_content);

    // 6. --- Backup and Atomic Swap ---
    // Create the .bak file for the UNDO command
    char bak_filename[512];
    sprintf(bak_filename, "%s.bak", filename);
    rename(filename, bak_filename); // This creates the backup

    // Perform the atomic swap
    if (rename(temp_filename, filename) != 0) {
        char *err = "ERR:500:ATOMIC_RENAME_FAILED\n";
        write(client_socket, err, strlen(err));
        remove(temp_filename); // Try to clean up
        rename(bak_filename, filename); // !! Try to restore the backup !!
        pthread_mutex_unlock(file_lock);
        return;
    }

    // --- CRITICAL SECTION ENDS ---

    // 7. --- UNLOCK ---
    pthread_mutex_unlock(file_lock);
    logger("[SS-Thread] WRITE: Unlocked file %s.\n", filename);

    // 8. --- Send Success ---
    char *success_msg = "ACK:WRITE_OK\n";
    write(client_socket, success_msg, strlen(success_msg));
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
        write(client_socket, "ACK: Command received and processed.\n", 37);
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

// -----------------------------------------------------------------
//  JOB 2: SERVER - LISTENER LOOP
// -----------------------------------------------------------------

/**
 * @brief Starts the main server loop for the Storage Server.
 * This function binds, listens, and accepts new client connections,
 * spinning off a new thread for each one.
 * This function does not return.
 */
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
