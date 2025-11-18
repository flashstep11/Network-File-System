// nm_handler.c

#include "defs.h"
#include "log.h"
#include "nm_handler.h"
void handle_undo_command(int client_socket, char* buffer) {
    char filename[256];

    // 1. --- Parse the command ---
    // Command from NM: "UNDO <filename>\n"
    if (sscanf(buffer, "UNDO %255s", filename) != 1) {
        char *err = "ERR:400:BAD_UNDO_COMMAND\n";
        logger("[SS-Thread] Failed to parse UNDO command.\n");
        write(client_socket, err, strlen(err));
        return;
    }

    // NOTE: NM already checked permissions, no need to check again here
    
    // 2. --- Form the backup file name and real file paths ---
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
void handle_create_command(int nm_socket, char* buffer) {
    char filename[256];
    
    // 1. Parse the filename
    if (sscanf(buffer, "CREATE %255s", filename) != 1) {
        char *err = "ERR:400:BAD_CREATE_COMMAND\n";
        logger("[NM-Thread] Failed to parse CREATE command.\n");
        write(nm_socket, err, strlen(err));
        return;
    }

    // Build full path in storage_root
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);

    // 2. --- CHECK FOR EXISTENCE ---
    FILE *existing_file = fopen(full_path, "r");
    if (existing_file) {
        // --- Failure: File Exists ---
        fclose(existing_file);
        char *err = "ERR:409:FILE_ALREADY_EXISTS\n";
        logger("[NM-Thread] CREATE failed: File '%s' already exists.\n", filename);
        write(nm_socket, err, strlen(err));
        return;
    }

    // 3. --- CREATE THE FILE ---
    FILE *new_file = fopen(full_path, "w"); 
    if (new_file) {
        // --- Success ---
        fclose(new_file); 
        char *ack = "ACK:CREATE_OK\n";
        logger("[NM-Thread] Successfully created file: %s\n", filename);
        write(nm_socket, ack, strlen(ack));
    } else {
        // --- Failure (Permissions, etc.) ---
        char *err = "ERR:500:CREATE_FILE_FAILED (SS)\n";
        logger("[NM-Thread] Failed to create file %s: %s\n", filename, strerror(errno));
        write(nm_socket, err, strlen(err));
    }
}

void handle_delete_command(int nm_socket, char* buffer) {
    char filename[256];

    if (sscanf(buffer, "DELETE %255s", filename) != 1) {
        char *err = "ERR:400:BAD_DELETE_COMMAND\n";
        logger("[NM-Thread] Failed to parse DELETE command.\n");
        write(nm_socket, err, strlen(err));
        return;
    }

    // Build full path
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);

    // Lock the file
    pthread_mutex_t* file_lock = get_file_mutex(filename);
    if (!file_lock) {
        char *err = "ERR:500:INTERNAL_SERVER_ERROR (LockManager failed)\n";
        write(nm_socket, err, strlen(err));
        return;
    }

    logger("[NM-Thread] DELETE: Locking file %s for deletion\n", filename);
    pthread_mutex_lock(file_lock);

    if (remove(full_path) == 0) {
        char *ack = "ACK:DELETE_OK\n";
        logger("[NM-Thread] Successfully deleted file: %s\n", filename);
        write(nm_socket, ack, strlen(ack));
    } else {
        char *err = "ERR:404:DELETE_FILE_FAILED (SS)\n";
        logger("[NM-Thread] Failed to delete file %s: %s\n", filename, strerror(errno));
        write(nm_socket, err, strlen(err));
    }
    
    // --- ADD THIS UNLOCK ---
    pthread_mutex_unlock(file_lock);
}

void handle_move_command(int nm_socket, char* buffer) {
    char source[256], dest[256];

    // 1. Parse: MOVE <source> <dest>
    if (sscanf(buffer, "MOVE %255s %255s", source, dest) != 2) {
        char *err = "ERR:400:BAD_MOVE_COMMAND\n";
        logger("[NM-Thread] Failed to parse MOVE command.\n");
        write(nm_socket, err, strlen(err));
        return;
    }

    // 2. Build full paths
    char source_full_path[512], dest_full_path[512];
    get_full_path(source_full_path, sizeof(source_full_path), source);
    get_full_path(dest_full_path, sizeof(dest_full_path), dest);

    // 3. Create destination directory if needed
    char dest_dir[512];
    strncpy(dest_dir, dest_full_path, sizeof(dest_dir));
    char* last_slash = strrchr(dest_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Create directory recursively
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dest_dir);
        system(cmd);
    }

    // 4. Check if source exists
    if (access(source_full_path, F_OK) != 0) {
        logger("[NM-Thread] Source file %s not found for move.\n", source);
        char *err = "ERR:404:SOURCE_FILE_NOT_FOUND\n";
        write(nm_socket, err, strlen(err));
        return;
    }

    // 5. Lock both files
    pthread_mutex_t* source_lock = get_file_mutex(source);
    pthread_mutex_t* dest_lock = get_file_mutex(dest);
    pthread_mutex_lock(source_lock);
    pthread_mutex_lock(dest_lock);

    // 6. Move/rename the file
    if (rename(source_full_path, dest_full_path) == 0) {
        // Success - also move backup file if it exists
        char source_bak[512], dest_bak[512];
        snprintf(source_bak, sizeof(source_bak), "%s.bak", source_full_path);
        snprintf(dest_bak, sizeof(dest_bak), "%s.bak", dest_full_path);
        rename(source_bak, dest_bak);  // Ignore error if backup doesn't exist
        
        char *ack = "ACK:MOVE_OK\n";
        logger("[NM-Thread] Moved file: %s -> %s\n", source, dest);
        write(nm_socket, ack, strlen(ack));
    } else {
        char *err = "ERR:500:MOVE_FAILED\n";
        logger("[NM-Thread] Failed to move file: %s -> %s (%s)\n", source, dest, strerror(errno));
        write(nm_socket, err, strlen(err));
    }

    pthread_mutex_unlock(dest_lock);
    pthread_mutex_unlock(source_lock);
}

void handle_get_info_command(int nm_socket, char* buffer) {
    char filename[256];
    char reply_buffer[512]; 
    struct stat file_stat; 

    // 1. --- Parse the command ---
    if (sscanf(buffer, "GET_INFO %255s", filename) != 1) {
        char *err = "ERR:400:BAD_INFO_COMMAND\n";
        logger("[NM-Thread] Failed to parse GET_INFO command.\n");
        write(nm_socket, err, strlen(err));
    } else {
        
        // 2. --- Use stat() to get file info ---
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_ROOT, filename);
        
        if (stat(full_path, &file_stat) == 0) {
            
            // --- Success ---
            long file_size = file_stat.st_size;
            long mod_time = file_stat.st_mtime;
            
            // Calculate word count
            int word_count = 0;
            FILE* fp = fopen(full_path, "r");
            if (fp) {
                char ch;
                int in_word = 0;
                while ((ch = fgetc(fp)) != EOF) {
                    if (isspace(ch) || ch == '\n' || ch == '\t') {
                        in_word = 0;
                    } else if (!in_word) {
                        in_word = 1;
                        word_count++;
                    }
                }
                fclose(fp);
            }

            // 3. --- Send all available info back to the NM ---
            sprintf(reply_buffer, "ACK:INFO %ld %ld %d\n", file_size, mod_time, word_count);
            logger("[NM-Thread] Sending info for %s: %ld bytes, %ld mtime, %d words\n", 
                   filename, file_size, mod_time, word_count);
            write(nm_socket, reply_buffer, strlen(reply_buffer));

        } else {
            // --- Failure ---
            char *err = "ERR:404:INFO_FAILED (File not found)\n";
            logger("[NM-Thread] Failed to get info for %s: %s\n", full_path, strerror(errno));
            write(nm_socket, err, strlen(err));
        }
    }
}

void handle_get_content_command(int nm_socket, char* buffer) {
    char filename[256];
    
    if (sscanf(buffer, "GET_CONTENT %255s", filename) != 1) {
        char *err = "ERR:400:BAD_GET_CONTENT_COMMAND\n";
        logger("[NM-Thread] Failed to parse GET_CONTENT command.\n");
        write(nm_socket, err, strlen(err));
        return;
    }
    
    // Build full path
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_ROOT, filename);
    
    // Read file content
    FILE* fp = fopen(full_path, "r");
    if (!fp) {
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        logger("[NM-Thread] Failed to open file %s for reading: %s\n", full_path, strerror(errno));
        write(nm_socket, err, strlen(err));
        return;
    }
    
    // Build full response in buffer
    char response[BUFFER_SIZE * 2];
    int offset = snprintf(response, sizeof(response), "ACK:CONTENT\n");
    
    // Read file content line by line
    char line[1024];
    while (fgets(line, sizeof(line), fp) && offset < sizeof(response) - 100) {
        offset += snprintf(response + offset, sizeof(response) - offset, "%s", line);
    }
    
    fclose(fp);
    
    // Add EOF marker
    offset += snprintf(response + offset, sizeof(response) - offset, "\nEOF\n");
    
    // Send entire response at once
    write(nm_socket, response, offset);
    
    logger("[NM-Thread] Sent content of %s to NM (%d bytes)\n", filename, offset);
}

void *handle_nm_commands(void *arg) {
    int nm_socket = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    int read_size;

    logger("[NM-Thread] Command listener is running, waiting for NS commands.\n");

    // This thread's whole life is just reading from this one socket
    while ((read_size = read(nm_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[read_size] = '\0';
        logger("[NM-Thread] Received trusted command: %s", buffer);

        // --- Handle Trusted Commands ---
        // Command: "CREATE <filename>\n"
        if (strncmp(buffer, "CREATE", 6) == 0) {
            handle_create_command(nm_socket, buffer);
        }
        // Command: "DELETE <filename>\n"
        else if (strncmp(buffer, "DELETE", 6) == 0) {
            handle_delete_command(nm_socket, buffer);
        }
        // Command: "MOVE <source> <dest>\n"
        else if (strncmp(buffer, "MOVE", 4) == 0) {
            handle_move_command(nm_socket, buffer);
        }
        // ... (you can add more else if blocks for other NS commands) ...
        else if (strncmp(buffer, "GET_INFO", 8) == 0) {
            handle_get_info_command(nm_socket, buffer);
        }
        else if (strncmp(buffer, "GET_CONTENT", 11) == 0) {
            handle_get_content_command(nm_socket, buffer);
        }
        else if (strncmp(buffer, "UNDO", 4) == 0) {
            handle_undo_command(nm_socket, buffer);
        }
        else {
            // Unknown command
            char *err = "ERR:400:UNKNOWN_COMMAND\n";
            write(nm_socket, err, strlen(err));
        }
         // Clear the buffer for the next read  

        memset(buffer, 0, BUFFER_SIZE);
    }

    // If we are here, the NS disconnected (read_size <= 0)
    logger("[NM-Thread] Name Server disconnected! Command thread shutting down.\n");
    close(nm_socket);
    // You might want to shut down the whole SS here, as per the spec
    // exit(1);
    
    return NULL;
}