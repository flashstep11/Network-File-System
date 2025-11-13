
// storage_server.c

// 1. Include headers
#include "defs.h"
#include "log.h"
#include "client_handler.h" // <-- Include your new handler
#include "nm_handler.h"
// 2. --- Global Variable DEFINITION ---
// LockManager* g_lock_manager;
// Global Instances
SentenceLockManager* g_sentence_lock_manager;

// A simple list to hold physical file mutexes
typedef struct FileMutexNode {
    char filename[256];
    pthread_mutex_t mutex;
    struct FileMutexNode* next;
} FileMutexNode;

FileMutexNode* g_file_mutexes = NULL;
pthread_mutex_t g_file_mutex_list_lock = PTHREAD_MUTEX_INITIALIZER;

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
/**
 * @brief Finds the start and end index of the Nth word *within* a specific sentence.
 *
 * @param content The full, original file content.
 * @param sent_start The starting index of the sentence.
 * @param sent_end The ending index of the sentence.
 * @param word_index The 1-based index of the word to find.
 * @param word_start A pointer to store the word's absolute start index.
 * @param word_end A pointer to store the word's absolute end index.
 * @return 1 on success, 0 on failure (word not found).
 */
int find_word(const char *content, int sent_start, int sent_end, int word_index,  int *word_start, int *word_end) {
    
    const char *delims = " \t\n\r";
    const char *ptr = content + sent_start; // Start searching from the beginning of the sentence
    const char *end_of_sentence = content + sent_end;
    int current_word = 1;

    // Skip any leading whitespace in the sentence
    ptr += strspn(ptr, delims);

    while (ptr <= end_of_sentence && *ptr != '\0') {
        // We are at the start of a word.
        *word_start = (int)(ptr - content);
        
        // Find the length of this word
        int word_len = strcspn(ptr, delims);
        
        // Check if this word goes past the end of the sentence
        if (ptr + word_len > end_of_sentence) {
            word_len = (int)(end_of_sentence - ptr) + 1;
        }
        
        *word_end = *word_start + word_len - 1;

        if (current_word == word_index) {
            return 1; // Found it!
        }

        // Move to the next word
        ptr += word_len; // Move past the word
        ptr += strspn(ptr, delims); // Move past the delimiters after it
        current_word++;
    }

    return 0; // Word not found
}
//     return &new_node->file_lock;
// }
void init_sentence_locks() {
    g_sentence_lock_manager = malloc(sizeof(SentenceLockManager));
    g_sentence_lock_manager->head = NULL;
    pthread_mutex_init(&g_sentence_lock_manager->manager_lock, NULL);
}

/**
 * @brief Tries to lock a specific sentence.
 * @return 1 if successful, 0 if ALREADY LOCKED (Access Denied).
 */
int acquire_sentence_lock(const char* filename, int sentence_id) {
    pthread_mutex_lock(&g_sentence_lock_manager->manager_lock);
    
    SentenceLockNode* curr = g_sentence_lock_manager->head;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_id == sentence_id) {
            // Already locked!
            pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
            return 0; 
        }
        curr = curr->next;
    }

    // Not locked, create new lock node
    SentenceLockNode* new_node = malloc(sizeof(SentenceLockNode));
    strcpy(new_node->filename, filename);
    new_node->sentence_id = sentence_id;
    new_node->next = g_sentence_lock_manager->head;
    g_sentence_lock_manager->head = new_node;

    pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
    return 1;
}

void release_sentence_lock(const char* filename, int sentence_id) {
    pthread_mutex_lock(&g_sentence_lock_manager->manager_lock);

    SentenceLockNode* curr = g_sentence_lock_manager->head;
    SentenceLockNode* prev = NULL;

    while (curr) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_id == sentence_id) {
            if (prev) prev->next = curr->next;
            else g_sentence_lock_manager->head = curr->next;
            
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
}

// Standard File Mutex (to prevent corrupt writes)
pthread_mutex_t* get_file_mutex(const char* filename) {
    pthread_mutex_lock(&g_file_mutex_list_lock);
    FileMutexNode* curr = g_file_mutexes;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            pthread_mutex_unlock(&g_file_mutex_list_lock);
            return &curr->mutex;
        }
        curr = curr->next;
    }
    // Create new
    FileMutexNode* node = malloc(sizeof(FileMutexNode));
    strcpy(node->filename, filename);
    pthread_mutex_init(&node->mutex, NULL);
    node->next = g_file_mutexes;
    g_file_mutexes = node;
    pthread_mutex_unlock(&g_file_mutex_list_lock);
    return &node->mutex;
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./storage_server <port>\n");
        return -1;
    }
    int my_port = atoi(argv[1]);
    logger_init("storage_server.log");
    int nm_socket = 0;
    struct sockaddr_in nm_addr;
    char buffer[BUFFER_SIZE] = {0};

    logger("Storage Server (SS) starting up...\n");

    // --- (Same as your code) ---
    if ((nm_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NM client socket creation error");
        return -1;
    }
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if(inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM IP address");
        close(nm_socket);
        return -1;
    }
    if (connect(nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM Failed. Is the NM running?");
        close(nm_socket);
        return -1;
    }
    logger("Successfully connected to NM!\n");
    // We'll build the message in a large buffer.
    char reg_msg[4096] = {0}; // 4KB buffer for file list
    
    // Start with the port
    int current_len = sprintf(reg_msg, "REGISTER_SS %d", my_port);

    // Now, scan the current directory for files
    DIR *d;
    struct dirent *dir;
    d = opendir("."); // "." means the current directory
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            // Check if it's a regular file (not a directory)
            struct stat st;
            if (stat(dir->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                
                // IMPORTANT: Filter out .c files, .o files, and your executable!
                if (strstr(dir->d_name, ".c") == NULL && 
                    strstr(dir->d_name, ".o") == NULL && 
                    strcmp(dir->d_name, "storage_server") != 0 &&
                    strcmp(dir->d_name, "name_server") != 0) 
                {
                    // Add a space, then the filename, to our message
                    if (current_len + strlen(dir->d_name) + 2 < 4096) {
                        current_len += sprintf(reg_msg + current_len, " %s", dir->d_name);
                    }
                }
            }
        }
        closedir(d);
    }
    
    // Add the final newline
    strcat(reg_msg, "\n");

    logger("Sending registration: %s", reg_msg);
    
    // Send the single, long message
    if (write(nm_socket, reg_msg, strlen(reg_msg)) < 0) {
        perror("write to NM failed");
        close(nm_socket);
        return -1;
    }
    int valread = read(nm_socket, buffer, BUFFER_SIZE - 1);
    if (valread <= 0) { // Check for 0 (disconnect) or -1 (error)
        logger("NM did not reply or disconnected.\n");
        close(nm_socket);
        return -1;
    }
    buffer[valread] = '\0';
    logger("Received from NM: %s\n", buffer);
    logger("Registration complete. Private command line is active.\n");
    // Instead of closing the socket, we pass it to a new thread.
    pthread_t nm_thread_id;
    // We must pass the nm_socket on the heap
    int *p_nm_socket = malloc(sizeof(int));
    *p_nm_socket = nm_socket;

    if (pthread_create(&nm_thread_id, NULL, handle_nm_commands, (void *)p_nm_socket) < 0) {
        perror("Failed to create NM command thread");
        close(nm_socket);
        return -1;
    }
    pthread_detach(nm_thread_id); // Let this thread run in the background
    
    // --- 5. Start JOB 2 ---
    // The main thread is now free. It can proceed to
    // start the "Public Lobby" server for clients.
    logger("[Main Thread] Starting public client server on port %d...\n", my_port);
    start_client_storage_server_loop(my_port);

    // This line is never reached
    return 0;
}