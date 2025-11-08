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
void handle_create_command(int nm_socket, char* buffer) {
    char filename[256];
    
    // 1. Parse the filename
    // Command from NS: "CREATE <filename>\n"
    if (sscanf(buffer, "CREATE %255s", filename) != 1) {
        // If parsing fails, it's a badly formed command
        char *err = "ERR:400:BAD_CREATE_COMMAND\n";
        logger("[NM-Thread] Failed to parse CREATE command.\n");
        write(nm_socket, err, strlen(err));
    } else {
        // 2. Try to create the file
        FILE *file = fopen(filename, "w"); // "w" = create/overwrite
        
        if (file) {
            // --- Success ---
            fclose(file); // Immediately close it, we just need it to exist
            char *ack = "ACK:CREATE_OK\n";
            logger("[NM-Thread] Successfully created file: %s\n", filename);
            write(nm_socket, ack, strlen(ack));
        } else {
            // --- Failure ---
            // This could be due to permissions, bad path, etc.
            char *err = "ERR:500:CREATE_FILE_FAILED (SS)\n";
            logger("[NM-Thread] Failed to create file %s: %s\n", filename, strerror(errno));
            write(nm_socket, err, strlen(err));
        }
    }
}
/**
 * @brief Handles the logic for an "UNDO" command from the NS.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "UNDO...".
 */
void handle_undo_command(int nm_socket, char* buffer) {
    char filename[256];
    char bak_filename[512];

    // 1. --- Parse the command ---
    // Command from NS: "UNDO <filename>\n"
    if (sscanf(buffer, "UNDO %255s", filename) != 1) {
        char *err = "ERR:400:BAD_UNDO_COMMAND\n";
        logger("[NM-Thread] Failed to parse UNDO command.\n");
        write(nm_socket, err, strlen(err));
    } else {
        
        // 2. --- Form the backup file name ---
        sprintf(bak_filename, "%s.bak", filename);

        // 3. --- Perform the Atomic Swap ---
        // This command renames the backup file, overwriting the
        // current (and newer) file. This is the UNDO.
        if (rename(bak_filename, filename) == 0) {
            // --- Success ---
            char *ack = "ACK:UNDO_OK\n";
            logger("[NM-Thread] Successfully restored backup for: %s\n", filename);
            write(nm_socket, ack, strlen(ack));
        } else {
            // --- Failure ---
            // This most likely means the .bak file doesn't exist
            char *err = "ERR:404:UNDO_FAILED (No backup found)\n";
            logger("[NM-Thread] Failed to find backup for %s: %s\n", filename, strerror(errno));
            write(nm_socket, err, strlen(err));
        }
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

    // 1. Parse the filename
    // Command from NS: "DELETE <filename>\n"
    if (sscanf(buffer, "DELETE %255s", filename) != 1) {
        char *err = "ERR:400:BAD_DELETE_COMMAND\n";
        logger("[NM-Thread] Failed to parse DELETE command.\n");
        write(nm_socket, err, strlen(err));
    } else {
        // 2. Try to delete the file
        // remove() is the standard C function to delete a file
        if (remove(filename) == 0) {
            // --- Success ---
            // remove() returns 0 on success
            char *ack = "ACK:DELETE_OK\n";
            logger("[NM-Thread] Successfully deleted file: %s\n", filename);
            write(nm_socket, ack, strlen(ack));
        } else {
            // --- Failure ---
            // remove() returns non-zero on failure (e.g., file not found)
            char *err = "ERR:404:DELETE_FILE_FAILED (SS)\n";
            logger("[NM-Thread] Failed to delete file %s: %s\n", filename, strerror(errno));
            write(nm_socket, err, strlen(err));
        }
    }
}
/**
 * @brief Handles the logic for a "GET_INFO" command from the NS.
 * This is used by the NS to get file size and timestamps.
 *
 * @param nm_socket The socket of the (private) Name Server connection.
 * @param buffer The command buffer, starting with "GET_INFO...".
 */
void handle_get_info_command(int nm_socket, char* buffer) {
    char filename[256];
    char reply_buffer[512]; // Buffer for sending the ACK
    struct stat file_stat; // The struct to hold file info

    // 1. --- Parse the command ---
    // Command from NS: "GET_INFO <filename>\n"
    if (sscanf(buffer, "GET_INFO %255s", filename) != 1) {
        char *err = "ERR:400:BAD_INFO_COMMAND\n";
        logger("[NM-Thread] Failed to parse GET_INFO command.\n");
        write(nm_socket, err, strlen(err));
    } else {
        
        // 2. --- Use stat() to get file info ---
        // stat() fills the file_stat struct with metadata
        // It returns 0 on success, -1 on failure
        if (stat(filename, &file_stat) == 0) {
            
            // --- Success ---
            long file_size = file_stat.st_size;     // Get file size
            long mod_time = file_stat.st_mtime;   // Get modification time
            
            // 3. --- Send the info back to the NS ---
            sprintf(reply_buffer, "ACK:INFO %ld %ld\n", file_size, mod_time);
            logger("[NM-Thread] Sending info for %s: %ld bytes, %ld mtime\n", filename, file_size, mod_time);
            write(nm_socket, reply_buffer, strlen(reply_buffer));

        } else {
            // --- Failure ---
            // This will fail if the file doesn't exist
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
        else if (strncmp(buffer, "UNDO", 4) == 0) {
            handle_undo_command(nm_socket, buffer);
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