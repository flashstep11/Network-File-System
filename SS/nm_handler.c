// nm_handler.c

#include "defs.h"
#include "log.h"
#include "nm_handler.h"
/**
 * @brief Handles the logic for a "CREATE" command from the NS.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "CREATE...".
 */
// nm_handler.c

/**
 * @brief Handles the logic for a "CREATE" command from the NS.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "CREATE...".
 */
void handle_create_command(int nm_socket, char* buffer) {
    char filename[256];
    
    // 1. Parse the filename
    if (sscanf(buffer, "CREATE %255s", filename) != 1) {
        char *err = "ERR:400:BAD_CREATE_COMMAND\n";
        logger("[NM-Thread] Failed to parse CREATE command.\n");
        write(nm_socket, err, strlen(err));
        return;
    }

    // 2. --- CHECK FOR EXISTENCE ---
    // Try to open the file for reading. If this succeeds, the file already exists.
    FILE *existing_file = fopen(filename, "r");
    if (existing_file) {
        // --- Failure: File Exists ---
        fclose(existing_file);
        char *err = "ERR:409:FILE_ALREADY_EXISTS\n"; // 409 is HTTP "Conflict"
        logger("[NM-Thread] CREATE failed: File '%s' already exists.\n", filename);
        write(nm_socket, err, strlen(err));
        return; // Stop.
    }

    // 3. --- CREATE THE FILE ---
    // If we are here, fopen(filename, "r") failed, meaning the file does NOT exist.
    // Now it is safe to create it with "w".
    FILE *new_file = fopen(filename, "w"); 
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
/**
 * @brief Handles the logic for a "DELETE" command from the NS.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "DELETE...".
 */
void handle_delete_command(int nm_socket, char* buffer) {
    char filename[256];

    if (sscanf(buffer, "DELETE %255s", filename) != 1) {
        char *err = "ERR:400:BAD_DELETE_COMMAND\n";
        logger("[NM-Thread] Failed to parse DELETE command.\n");
        write(nm_socket, err, strlen(err));
        return; // Return here
    }

    // --- ADD THIS LOCKING SECTION ---
    pthread_mutex_t* file_lock = get_file_mutex(filename);
    if (!file_lock) {
        char *err = "ERR:500:INTERNAL_SERVER_ERROR (LockManager failed)\n";
        write(nm_socket, err, strlen(err));
        return;
    }

    logger("[NM-Thread] DELETE: Locking file %s for deletion\n", filename);
    pthread_mutex_lock(file_lock);
    // --- END ADD ---

    if (remove(filename) == 0) {
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
/**
 * @brief Handles the logic for a "GET_INFO" command from the NS.
 * This is used by the NS to get file size and timestamps.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "GET_INFO...".
 */
// nm_handler.c

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
        if (stat(filename, &file_stat) == 0) {
            
            // --- Success ---
            long file_size = file_stat.st_size;     // Get file size
            long mod_time = file_stat.st_mtime;   // Get modification time
            long acc_time = file_stat.st_atime;   // <-- ADD THIS: Get last access time

            // 3. --- Send all available info back to the NS ---
            // The NS will be responsible for formatting this.
            sprintf(reply_buffer, "ACK:INFO %ld %ld %ld\n", file_size, mod_time, acc_time);
            logger("[NM-Thread] Sending info for %s: %ld bytes, %ld mtime, %ld atime\n", 
                   filename, file_size, mod_time, acc_time);
            write(nm_socket, reply_buffer, strlen(reply_buffer));

        } else {
            // --- Failure ---
            char *err = "ERR:404:INFO_FAILED (File not found)\n";
            logger("[NM-Thread] Failed to get info for %s: %s\n", filename, strerror(errno));
            write(nm_socket, err, strlen(err));
        }
    }
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
        // ... (you can add more else if blocks for other NS commands) ...
        else if (strncmp(buffer, "GET_INFO", 8) == 0) {
            handle_get_info_command(nm_socket, buffer);
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