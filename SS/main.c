#include <stdio.h>      // For standard I/O (printf, perror)
#include <stdlib.h>     // For standard library (exit, malloc, free)
#include <string.h>     // For string operations (memset, sprintf, strlen)
#include <unistd.h>     // For POSIX operations (read, write, close)
#include <arpa/inet.h>  // For IP address functions (inet_pton, htons)
#include <sys/socket.h> // For socket programming
#include <pthread.h>    // For multi-threading (pthreads)


// --- Configuration ---
// !! Coordinate these with your friend !!
#define NM_IP "127.0.0.1"    // Name Server's IP (localhost)
#define NM_PORT 8080         // Name Server's Port

// !! This is your server's info !!
#define MY_STORAGE_PORT 9001 // The port *this* server will listen on
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10       // Max pending connections for listen()


// -----------------------------------------------------------------
//  JOB 2: SERVER - WORKER THREAD
// -----------------------------------------------------------------

/**
 * @brief Handles all communication for a single client connection.
 * This function is run in its own thread.
 * @param arg A pointer to the client's socket file descriptor.
 */
// This is your global lock manager
char* read_file_to_string(const char *filename, long *out_size) {
    FILE *fp = fopen(filename, "rb"); // 1. Open in "read binary" mode
    if (fp == NULL) {
        perror("Error opening file");
        return NULL;
    }

    // 2. Go to the end of the file to get its size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) {
        perror("Error getting file size (ftell)");
        fclose(fp);
        return NULL;
    }
    fseek(fp, 0, SEEK_SET); // Rewind to the beginning

    // 3. Allocate memory for the file content + 1 for the null terminator
    char *buffer = (char *)malloc(size + 1);
    if (buffer == NULL) {
        perror("Error allocating memory");
        fclose(fp);
        return NULL;
    }

    // 4. Read the entire file into the buffer
    size_t bytes_read = fread(buffer, 1, size, fp);
    if (bytes_read != size) {
        perror("Error reading file");
        fclose(fp);
        free(buffer);
        return NULL;
    }

    // 5. Add the null terminator
    buffer[size] = '\0';

    // 6. Clean up and return
    fclose(fp);
    if (out_size != NULL) {
        *out_size = size;
    }

    return buffer;
}
int find_sentence(const char *content, int sentence_num, int *start, int *end) {
    int current_sentence = 1;
    const char *ptr = content;
    *start = 0;
    *end = 0;

    // The first sentence always starts at index 0
    *start = 0;

    while (*ptr != '\0') {
        if (*ptr == '.' || *ptr == '!' || *ptr == '?') {
            // Found the end of a sentence
            *end = (int)(ptr - content); 

            if (current_sentence == sentence_num) {
                return 1; // Found our sentence
            }

            // Move to the start of the next sentence
            current_sentence++;
            
            // Skip over the delimiter and any following whitespace
            const char *next_char = ptr + 1;
            while (*next_char != '\0' && (*next_char == ' ' || *next_char == '\n' || *next_char == '\r' || *next_char == '\t')) {
                next_char++;
            }
            
            if (*next_char == '\0') {
                return 0; // Reached end of string, no next sentence
            }
            
            *start = (int)(next_char - content);
            ptr = next_char; // Continue search from new start
            continue; // Skip the ptr++ at the end
        }
        ptr++;
    }

    // Handle case where the last part of the file is the sentence we want
    // but it doesn't end with a delimiter (e.g., file ends mid-sentence)
    if (current_sentence == sentence_num && *start < (ptr - content)) {
        *end = (int)(ptr - content) - 1; // End of string
        return 1;
    }
    
    return 0; // Sentence not found
}

// -----------------------------------------------------------------
//  LOCK MANAGER (The "Key Cabinet")
// -----------------------------------------------------------------

// This is the node for our linked list of locks
typedef struct FileLockNode {
    char* filename;                 // The "key" (e.g., "file.txt")
    pthread_mutex_t file_lock;      // The "value" (the room key)
    struct FileLockNode* next;      // Pointer to the next node
} FileLockNode;

// This is your new global lock manager
typedef struct {
    FileLockNode* head;         // The start of the list
    pthread_mutex_t list_lock;  // The "Level 1" Cabinet Lock
} LockManager;

// This must be a global variable, accessible to all threads
LockManager* g_lock_manager;

/**
 * @brief Initializes the global lock manager.
 * !! Call this *once* in main() before starting the server loop. !!
 */
void lock_manager_init() {
    g_lock_manager = (LockManager*)malloc(sizeof(LockManager));
    if (g_lock_manager == NULL) {
        perror("Failed to allocate memory for Lock Manager");
        exit(EXIT_FAILURE);
    }
    g_lock_manager->head = NULL; // List starts empty
    pthread_mutex_init(&g_lock_manager->list_lock, NULL);
    printf("Lock Manager Initialized (using Linked List).\n");
}

/**
 * @brief Helper function to find-or-create a mutex for a given file.
 * This is the implementation of our "Key Cabinet" logic.
 *
 * @param filename The name of the file to get a lock for.
 * @return A pointer to the file's specific mutex.
 */
pthread_mutex_t* get_lock_for_file(const char *filename) {
    FileLockNode* current = NULL;

    // 1. Lock the global list (Lock the cabinet)
    pthread_mutex_lock(&g_lock_manager->list_lock);

    // 2. Search the list for the file
    current = g_lock_manager->head;
    while (current != NULL) {
        if (strcmp(current->filename, filename) == 0) {
            // --- Found it! ---
            // We found the key. Unlock the cabinet and return
            // the room key.
            pthread_mutex_unlock(&g_lock_manager->list_lock);
            return &current->file_lock;
        }
        current = current->next;
    }

    // --- Not Found. We must create it. ---
    // (We are still inside the locked "cabinet")
    
    printf("[LockManager] Creating new lock for file: %s\n", filename);

    // 3. Create a new node (a new "key" and "hook")
    FileLockNode* new_node = (FileLockNode*)malloc(sizeof(FileLockNode));
    if (new_node == NULL) {
        perror("Failed to allocate memory for new lock node");
        pthread_mutex_unlock(&g_lock_manager->list_lock); // Unlock before failing
        return NULL; // This will cause a crash later, but it's a critical error
    }
    
    new_node->filename = strdup(filename); // Copy the filename
    if (new_node->filename == NULL) {
        perror("Failed to strdup filename for lock");
        free(new_node);
        pthread_mutex_unlock(&g_lock_manager->list_lock);
        return NULL;
    }
    
    pthread_mutex_init(&new_node->file_lock, NULL);
    
    // 4. Add it to the front of the list
    new_node->next = g_lock_manager->head;
    g_lock_manager->head = new_node;

    // 5. Unlock the global list (Unlock the cabinet)
    pthread_mutex_unlock(&g_lock_manager->list_lock);

    // 6. Return the new mutex
    return &new_node->file_lock;
}

// -----------------------------------------------------------------
//  MAIN `WRITE` LOGIC
// -----------------------------------------------------------------

/**
 * This is the code block that goes inside your
 * `handle_client_request` function's `while` loop.
 *
 * It assumes `client_socket` and `buffer` are available.
 */
void handle_write_command(int client_socket, char* buffer) {

    // 1. --- Parse the command ---
    // Command format: WRITE <filename> <sentence_num> <content...>\n
    char filename[256];
    int sentence_num;
    char *new_content = NULL;

    // Use sscanf to parse the first two parts
    if (sscanf(buffer, "WRITE %255s %d", filename, &sentence_num) < 2) {
        char *err = "ERR:400:BAD_WRITE_COMMAND\n";
        write(client_socket, err, strlen(err));
        return; // Return from this function, not the whole thread
    }
    
    // Find the start of the actual content
    char *first_space = strchr(buffer, ' ');
    if (!first_space) { /*... error ...*/ return; }
    
    char *second_space = strchr(first_space + 1, ' ');
    if (!second_space) {
        char *err = "ERR:400:BAD_WRITE_COMMAND (missing content)\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    new_content = second_space + 1; // Content starts after the 2nd space
    
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
    printf("[SS-Thread] WRITE: Attempting to lock file %s...\n", filename);
    pthread_mutex_lock(file_lock);
    printf("[SS-Thread] WRITE: Locked file %s.\n", filename);

    // --- CRITICAL SECTION STARTS ---

    // 4. --- Start Atomic Swap ---
    long file_size = 0;
    char *original_content = read_file_to_string(filename, &file_size);
    
    // Check if file exists.
    // If it doesn't, we can't edit a sentence.
    if (original_content == NULL) {
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        pthread_mutex_unlock(file_lock); // !! IMPORTANT: Unlock on error !!
        return;
    }

    int start_index = 0, end_index = 0;
    char *modified_content = NULL;
    
    if (find_sentence(original_content, sentence_num, &start_index, &end_index)) {
        
        // Found the sentence, now replace it in memory
        int len_before = start_index;
        int len_new = strlen(new_content);
        // Calculate length after, from the end delimiter onwards
        int len_after = strlen(original_content + end_index + 1);

        // Allocate memory for the new string
        modified_content = (char *)malloc(len_before + len_new + len_after + 1);
        if (modified_content == NULL) {
            perror("Failed to alloc memory for modified content");
            free(original_content);
            pthread_mutex_unlock(file_lock);
            return;
        }

        // Copy the part *before* the sentence
        strncpy(modified_content, original_content, len_before);
        modified_content[len_before] = '\0'; // Manually null-terminate

        // Concatenate the *new* sentence
        strcat(modified_content, new_content);

        // Concatenate the part *after* the sentence
        strcat(modified_content, original_content + end_index + 1);

    } else {
        // The specified sentence number wasn't found
        char *err = "ERR:404:SENTENCE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        free(original_content);
        pthread_mutex_unlock(file_lock); // !! IMPORTANT: Unlock on error !!
        return;
    }
    
    // We are done with the original string, free it
    free(original_content);

    // 5. --- Write to a temporary file ---
    char temp_filename[512];
    sprintf(temp_filename, "%s.%ld.temp", filename, (long)pthread_self());

    FILE *temp_file = fopen(temp_filename, "wb"); // "wb" = write binary
    if (temp_file == NULL) {
        char *err = "ERR:500:COULD_NOT_CREATE_TEMP_FILE\n";
        write(client_socket, err, strlen(err));
        free(modified_content);
        pthread_mutex_unlock(file_lock);
        return;
    }
    
    fwrite(modified_content, 1, strlen(modified_content), temp_file);
    fclose(temp_file);
    free(modified_content); // We are done with the modified string

    // 6. --- Atomic Swap (The "Magic") ---
    if (rename(temp_filename, filename) != 0) {
        char *err = "ERR:500:ATOMIC_RENAME_FAILED\n";
        write(client_socket, err, strlen(err));
        remove(temp_filename); // Try to clean up the temp file
        pthread_mutex_unlock(file_lock);
        return;
    }

    // --- CRITICAL SECTION ENDS ---

    // 7. --- UNLOCK ---
    pthread_mutex_unlock(file_lock);
    printf("[SS-Thread] WRITE: Unlocked file %s.\n", filename);

    // 8. --- Send Success ---
    char *success_msg = "ACK:WRITE_OK\n";
    write(client_socket, success_msg, strlen(success_msg));
}
void *handle_client_request(void *arg) {
    // 1. Get the client socket from the argument
    int client_socket = *(int *)arg;
    free(arg); // Free the heap memory for the socket pointer

    char buffer[BUFFER_SIZE];
    int read_size;

    printf("[SS-Thread] New client connected. Waiting for commands...\n");

    // 2. Loop to read commands from this client
    // read() blocks until data is received or connection is closed
    while ((read_size = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        // Null-terminate the received string
        buffer[read_size] = '\0';
        printf("[SS-Thread] Received command: %s", buffer);
        if (strncmp(buffer, "CREATE", 6) == 0) {
            // - Get filename
            char filename[BUFFER_SIZE];
            if (sscanf(buffer, "CREATE %255s", filename) != 1) 
            {
                // sscanf returns 1 if it successfully matched the filename.
                char *err = "ERR:400:BAD_CREATE_COMMAND_FORMAT\n";
                write(client_socket, err, strlen(err));
                return; // Or continue loop
            }
            sscanf(buffer + 7, "%s", filename);
            FILE *file = fopen(filename, "w");
            if (file) {
                fclose(file);
            }

        }
        else if (strncmp(buffer, "READ", 4) == 0) 
        {
            //MAY BE WE SHOULD CHECK WHETHER THE CLIENT HAS ACCESS OR NOT
            // - Get filename 
            char filename[BUFFER_SIZE];
            if (sscanf(buffer, "READ %255s", filename) != 1) 
            {
                // sscanf returns 1 if it successfully matched the filename.
                char *err = "ERR:400:BAD_READ_COMMAND_FORMAT\n";
                write(client_socket, err, strlen(err));
                return; // Or continue loop
            }
            sscanf(buffer + 5, "%s", filename);
            int data_len;
            char* file_data = read_file_to_string(filename,&data_len);
            if (file_data)
            {
                write(client_socket, file_data, data_len);
                free(file_data);
            }
            else
            {
               char *err = "ERR:404:FILE_NOT_FOUND\n";
                write(client_socket, err, strlen(err));
            }
        }
        else if (strncmp(buffer, "WRITE", 5) == 0) {
            // - Get filename, sentence index, and content
            // - **Implement write logic with locking**
            // - (e.g., pthread_mutex_lock(&file_lock);)
          handle_write_command(client_socket, buffer);
        } else {
            // Unknown command
            char *err = "ERR:400:UNKNOWN_COMMAND\n";
            write(client_socket, err, strlen(err));
        }
        //
        // -------------------------------------------------

        // 3. Send a generic ACK back to the client
        // (You should make this reply specific to the command's success/failure)
        write(client_socket, "ACK: Command received and processed.\n", 37);

        // Clear the buffer for the next read
        memset(buffer, 0, BUFFER_SIZE);
    }

    // 4. Handle client disconnect
    if (read_size == 0) {
        printf("[SS-Thread] Client disconnected.\n");
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
void start_storage_server_loop() {
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
    server_addr.sin_port = htons(MY_STORAGE_PORT);

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

    printf("\n--- Storage Server is now ONLINE. Listening on port %d ---\n\n", MY_STORAGE_PORT);

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

        printf("[SS-Main] New connection accepted! Spawning thread...\n");

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


// -----------------------------------------------------------------
//  JOB 1: CLIENT - REGISTRATION
// -----------------------------------------------------------------

/**
 * @brief Main function: runs Job 1, then starts Job 2.
 */
int main() {
    int nm_socket = 0;
    struct sockaddr_in nm_addr;
    char buffer[BUFFER_SIZE] = {0};

    printf("Storage Server (SS) starting up...\n");

    // --- Create client socket to connect to NM ---
    if ((nm_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NM client socket creation error");
        return -1;
    }

    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);

    // Convert IP address from text to binary
    if(inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM IP address");
        close(nm_socket);
        return -1;
    }

    // --- 1. Connect to the Name Server ---
    printf("Attempting to connect to Name Server at %s:%d...\n", NM_IP, NM_PORT);
    if (connect(nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM Failed. Is the NM running?");
        close(nm_socket);
        return -1;
    }
    printf("Successfully connected to NM!\n");

    // --- 2. Send registration message ---
    // !! You and your friend MUST agree on this exact message format !!
    char reg_msg[128];
    sprintf(reg_msg, "REGISTER_SS %d\n", MY_STORAGE_PORT); 
    
    printf("Sending registration: %s", reg_msg);
    if (write(nm_socket, reg_msg, strlen(reg_msg)) < 0) 
    {
        perror("write to NM failed");
        close(nm_socket);
        return -1;
    }

    // --- 3. Wait for ACK (acknowledgment) ---
    int valread = read(nm_socket, buffer, BUFFER_SIZE - 1);
    if (valread > 0) {
        buffer[valread] = '\0';
        printf("Received from NM: %s\n", buffer);
        
        // TODO: Add a real check for the ACK
        // e.g., if(strstr(buffer, "ACK:OK") == NULL) { exit(-1); }

    } else {
        printf("NM did not reply or disconnected.\n");
        close(nm_socket);
        return -1;
    }
    
    // --- 4. Registration complete ---
    close(nm_socket); // We are done with the NM (for now)
    printf("Registration complete.\n");

    // --- 5. Start JOB 2 ---
    // Now that we are registered, start our *own* server loop.
    // This function will run forever.
    start_storage_server_loop();

    // This line will never be reached
    return 0;
}