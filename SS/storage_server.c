
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

// Dynamic storage root path (set based on port)
char STORAGE_ROOT[64] = "storage_root/";

// SS identity for replication notifications
int g_ss_id = -1;
char g_nm_ip[32] = NM_IP;
int g_nm_port = NM_PORT;

// Checkpoint global variables
FileCheckpoints* g_checkpoints_head = NULL;
pthread_mutex_t g_checkpoints_lock = PTHREAD_MUTEX_INITIALIZER;

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
    if (!content || !start || !end) return 0;
    
    int current_sentence = 0;  // Changed to 0-indexed
    const char *ptr = content;
    *start = 0;
    *end = 0;

    // Skip leading whitespace for first sentence
    while (*ptr != '\0' && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
        ptr++;
    }
    *start = (int)(ptr - content);

    // If looking for sentence 0 and file is not empty
    if (sentence_num == 0 && *ptr != '\0') {
        // Find end of first sentence
        while (*ptr != '\0') {
            if (*ptr == '.' || *ptr == '!' || *ptr == '?') {
                *end = (int)(ptr - content);
                return 1;
            }
            ptr++;
        }
        // No delimiter found, treat whole file as one sentence
        *end = (int)(ptr - content) - 1;
        return 1;
    }

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
            ptr++;
            while (*ptr != '\0' && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
                ptr++;
            }
            
            if (*ptr == '\0') {
                return 0; // Reached end of string, no next sentence
            }
            
            *start = (int)(ptr - content);
            continue;
        }
        ptr++;
    }

    // Handle case where the last part of the file is the sentence we want
    // but it doesn't end with a delimiter (e.g., file ends mid-sentence)
    if (current_sentence == sentence_num && *start < (int)(ptr - content)) {
        *end = (int)(ptr - content) - 1; // End of string
        return 1;
    }
    
    return 0; // Sentence not found
}

int find_word(const char *content, int sent_start, int sent_end, int word_index,  int *word_start, int *word_end) {
    if (!content || !word_start || !word_end) return 0;
    
    const char *delims = " \t\n\r";
    const char *ptr = content + sent_start; // Start searching from the beginning of the sentence
    const char *end_of_sentence = content + sent_end;
    int current_word = 1;  // Changed to 1-indexed (words start from 1)

    // Skip any leading whitespace in the sentence
    while (ptr <= end_of_sentence && *ptr != '\0' && strchr(delims, *ptr)) {
        ptr++;
    }

    while (ptr <= end_of_sentence && *ptr != '\0') {
        // We are at the start of a word.
        *word_start = (int)(ptr - content);
        
        // Find the length of this word
        int word_len = 0;
        while (ptr + word_len <= end_of_sentence && *(ptr + word_len) != '\0' && !strchr(delims, *(ptr + word_len))) {
            word_len++;
        }
        
        if (word_len == 0) break; // No more words
        
        *word_end = *word_start + word_len - 1;

        if (current_word == word_index) {
            return 1; // Found it!
        }

        // Move to the next word
        ptr += word_len; // Move past the word
        while (ptr <= end_of_sentence && *ptr != '\0' && strchr(delims, *ptr)) {
            ptr++;
        }
        current_word++;
    }

    return 0; // Word not found
}

void init_lock_systems() {
    // 1. Init Sentence Lock Manager
    g_sentence_lock_manager = malloc(sizeof(SentenceLockManager));
    if (g_sentence_lock_manager == NULL) {
        perror("Failed to alloc SentenceLockManager");
        exit(EXIT_FAILURE);
    }
    g_sentence_lock_manager->head = NULL;
    pthread_mutex_init(&g_sentence_lock_manager->manager_lock, NULL);

    // 2. Init File Mutex List (THE MISSING PIECE)
    g_file_mutexes = NULL; // Initialize the list as empty
    // g_file_mutex_list_lock is already initialized with PTHREAD_MUTEX_INITIALIZER
    
    // 3. Init Checkpoint System
    init_checkpoints();
    
    logger("Lock systems and checkpoints initialized.\n");
}

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

// Check if sentence structure changed for a file
// Returns 1 if any other locked sentences are now invalid
int check_sentence_structure_changed(const char* filename, const char* file_content) {
    pthread_mutex_lock(&g_sentence_lock_manager->manager_lock);
    
    int structure_changed = 0;
    SentenceLockNode* curr = g_sentence_lock_manager->head;
    
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            // Check if this sentence still exists at the same index
            int s_start = 0, s_end = 0;
            if (!find_sentence(file_content, curr->sentence_id, &s_start, &s_end)) {
                // Sentence no longer exists at this index!
                logger("[SS-Lock] WARNING: Sentence %d of %s no longer exists after concurrent edit!\n", 
                       curr->sentence_id, filename);
                structure_changed = 1;
            }
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
    return structure_changed;
}

// Release ALL sentence locks for a file (called when structure changes)
void release_all_sentence_locks_for_file(const char* filename) {
    pthread_mutex_lock(&g_sentence_lock_manager->manager_lock);
    
    SentenceLockNode* curr = g_sentence_lock_manager->head;
    SentenceLockNode* prev = NULL;
    
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            logger("[SS-Lock] Invalidating sentence lock %d on %s due to structure change\n", 
                   curr->sentence_id, filename);
            
            SentenceLockNode* to_free = curr;
            
            if (prev) {
                prev->next = curr->next;
                curr = curr->next;
            } else {
                g_sentence_lock_manager->head = curr->next;
                curr = g_sentence_lock_manager->head;
            }
            
            free(to_free);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    
    pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
}

int is_file_being_edited(const char* filename) {
    pthread_mutex_lock(&g_sentence_lock_manager->manager_lock);
    
    SentenceLockNode* curr = g_sentence_lock_manager->head;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            // Found at least one locked sentence for this file
            pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
            return 1; // File is being edited
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&g_sentence_lock_manager->manager_lock);
    return 0; // No sentences locked for this file
}

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

// ============= CHECKPOINT IMPLEMENTATION =============

void init_checkpoints() {
    pthread_mutex_lock(&g_checkpoints_lock);
    g_checkpoints_head = NULL;
    pthread_mutex_unlock(&g_checkpoints_lock);
    logger("Checkpoint system initialized.\n");
}

// Helper to find or create FileCheckpoints for a file
static FileCheckpoints* get_file_checkpoints(const char* filename) {
    FileCheckpoints* curr = g_checkpoints_head;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    
    // Create new FileCheckpoints
    FileCheckpoints* fc = malloc(sizeof(FileCheckpoints));
    strncpy(fc->filename, filename, 255);
    fc->filename[255] = '\0';
    fc->checkpoints = NULL;
    pthread_mutex_init(&fc->lock, NULL);
    fc->next = g_checkpoints_head;
    g_checkpoints_head = fc;
    return fc;
}

int create_checkpoint(const char* filename, const char* tag) {
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    // Read current file content
    long file_size = 0;
    char* content = read_file_to_string(full_path, &file_size);
    if (!content) {
        logger("[Checkpoint] Failed to read file %s\n", filename);
        return 0;
    }
    
    pthread_mutex_lock(&g_checkpoints_lock);
    FileCheckpoints* fc = get_file_checkpoints(filename);
    pthread_mutex_lock(&fc->lock);
    pthread_mutex_unlock(&g_checkpoints_lock);
    
    // Check if tag already exists
    CheckpointNode* curr = fc->checkpoints;
    while (curr) {
        if (strcmp(curr->tag, tag) == 0) {
            // Update existing checkpoint
            free(curr->content);
            curr->content = content;
            curr->timestamp = time(NULL);
            pthread_mutex_unlock(&fc->lock);
            logger("[Checkpoint] Updated checkpoint '%s' for %s\n", tag, filename);
            return 1;
        }
        curr = curr->next;
    }
    
    // Create new checkpoint
    CheckpointNode* cp = malloc(sizeof(CheckpointNode));
    strncpy(cp->tag, tag, 63);
    cp->tag[63] = '\0';
    cp->content = content;
    cp->timestamp = time(NULL);
    cp->next = fc->checkpoints;
    fc->checkpoints = cp;
    
    pthread_mutex_unlock(&fc->lock);
    logger("[Checkpoint] Created checkpoint '%s' for %s\n", tag, filename);
    return 1;
}

char* get_checkpoint_content(const char* filename, const char* tag) {
    pthread_mutex_lock(&g_checkpoints_lock);
    
    FileCheckpoints* curr = g_checkpoints_head;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            pthread_mutex_lock(&curr->lock);
            pthread_mutex_unlock(&g_checkpoints_lock);
            
            CheckpointNode* cp = curr->checkpoints;
            while (cp) {
                if (strcmp(cp->tag, tag) == 0) {
                    char* content_copy = strdup(cp->content);
                    pthread_mutex_unlock(&curr->lock);
                    return content_copy;
                }
                cp = cp->next;
            }
            pthread_mutex_unlock(&curr->lock);
            return NULL;
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&g_checkpoints_lock);
    return NULL;
}

int revert_to_checkpoint(const char* filename, const char* tag) {
    char* content = get_checkpoint_content(filename, tag);
    if (!content) {
        logger("[Checkpoint] Checkpoint '%s' not found for %s\n", tag, filename);
        return 0;
    }
    
    // Write checkpoint content to file
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    FILE* fp = fopen(full_path, "w");
    if (!fp) {
        logger("[Checkpoint] Failed to open %s for writing\n", filename);
        free(content);
        return 0;
    }
    
    fprintf(fp, "%s", content);
    fclose(fp);
    free(content);
    
    logger("[Checkpoint] Reverted %s to checkpoint '%s'\n", filename, tag);
    return 1;
}

char* list_checkpoints(const char* filename) {
    pthread_mutex_lock(&g_checkpoints_lock);
    
    FileCheckpoints* curr = g_checkpoints_head;
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            pthread_mutex_lock(&curr->lock);
            pthread_mutex_unlock(&g_checkpoints_lock);
            
            if (!curr->checkpoints) {
                pthread_mutex_unlock(&curr->lock);
                return strdup("No checkpoints found.\n");
            }
            
            // Build list string
            char* result = malloc(4096);
            result[0] = '\0';
            strcat(result, "Checkpoints:\n");
            
            CheckpointNode* cp = curr->checkpoints;
            while (cp) {
                char time_str[64];
                struct tm* tm_info = localtime(&cp->timestamp);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                
                char line[256];
                snprintf(line, sizeof(line), "  - %s (created: %s)\n", cp->tag, time_str);
                strcat(result, line);
                
                cp = cp->next;
            }
            
            pthread_mutex_unlock(&curr->lock);
            return result;
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&g_checkpoints_lock);
    return strdup("No checkpoints found.\n");
}

char* diff_checkpoints(const char* filename, const char* tag1, const char* tag2) {
    // Get both checkpoint contents
    char* content1 = get_checkpoint_content(filename, tag1);
    char* content2 = get_checkpoint_content(filename, tag2);
    
    if (!content1 || !content2) {
        char* error = malloc(256);
        snprintf(error, 256, "ERROR: Could not find one or both checkpoints ('%s' and '%s').\n", tag1, tag2);
        free(content1);
        free(content2);
        return error;
    }
    
    // Split content into lines
    char** lines1 = malloc(sizeof(char*) * 10000);
    char** lines2 = malloc(sizeof(char*) * 10000);
    int count1 = 0, count2 = 0;
    
    // Parse content1 into lines
    char* tmp1 = strdup(content1);
    char* line = strtok(tmp1, "\n");
    while (line && count1 < 10000) {
        lines1[count1++] = strdup(line);
        line = strtok(NULL, "\n");
    }
    free(tmp1);
    
    // Parse content2 into lines
    char* tmp2 = strdup(content2);
    line = strtok(tmp2, "\n");
    while (line && count2 < 10000) {
        lines2[count2++] = strdup(line);
        line = strtok(NULL, "\n");
    }
    free(tmp2);
    
    // Build diff output (simple line-by-line comparison)
    char* result = malloc(100000);
    result[0] = '\0';
    
    char header[256];
    snprintf(header, sizeof(header), "Diff: %s (%s) vs (%s)\n", filename, tag1, tag2);
    strcat(result, header);
    strcat(result, "=====================================\n");
    
    int max_lines = (count1 > count2) ? count1 : count2;
    
    for (int i = 0; i < max_lines; i++) {
        if (i < count1 && i < count2) {
            // Both checkpoints have this line
            if (strcmp(lines1[i], lines2[i]) == 0) {
                // Same line - show context
                char line_buf[2048];
                snprintf(line_buf, sizeof(line_buf), "  %s\n", lines1[i]);
                strcat(result, line_buf);
            } else {
                // Different lines
                char line_buf[2048];
                snprintf(line_buf, sizeof(line_buf), "- %s\n", lines1[i]);
                strcat(result, line_buf);
                snprintf(line_buf, sizeof(line_buf), "+ %s\n", lines2[i]);
                strcat(result, line_buf);
            }
        } else if (i < count1) {
            // Line only in tag1 (removed in tag2)
            char line_buf[2048];
            snprintf(line_buf, sizeof(line_buf), "- %s\n", lines1[i]);
            strcat(result, line_buf);
        } else if (i < count2) {
            // Line only in tag2 (added in tag2)
            char line_buf[2048];
            snprintf(line_buf, sizeof(line_buf), "+ %s\n", lines2[i]);
            strcat(result, line_buf);
        }
    }
    
    // Cleanup
    for (int i = 0; i < count1; i++) free(lines1[i]);
    for (int i = 0; i < count2; i++) free(lines2[i]);
    free(lines1);
    free(lines2);
    free(content1);
    free(content2);
    
    return result;
}

// ============= END CHECKPOINT IMPLEMENTATION =============

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./storage_server <port> [nm_ip]\n");
        printf("  <port>   : Port for this storage server\n");
        printf("  [nm_ip]  : IP address of Name Server (default: 127.0.0.1)\n");
        return -1;
    }
    int my_port = atoi(argv[1]);
    
    // CRITICAL: Initialize logger FIRST before any logger() calls
    logger_init("storage_server.log");
    
    // Accept NM IP as optional argument
    if (argc >= 3) {
        strncpy(g_nm_ip, argv[2], sizeof(g_nm_ip) - 1);
        g_nm_ip[sizeof(g_nm_ip) - 1] = '\0';
        logger("Using Name Server IP: %s\n", g_nm_ip);
    }
    
    // Set storage root based on port to maintain separate directories
    snprintf(STORAGE_ROOT, sizeof(STORAGE_ROOT), "storage_root_%d/", my_port);
    logger("Storage root set to: %s\n", STORAGE_ROOT);
    
    // Create the storage directory if it doesn't exist
    mkdir(STORAGE_ROOT, 0755);
    
    // CRITICAL: Initialize lock systems FIRST
    init_lock_systems();
    
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
    if(inet_pton(AF_INET, g_nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM IP address");
        close(nm_socket);
        return -1;
    }
    logger("Connecting to Name Server at %s:%d...\n", g_nm_ip, NM_PORT);
    if (connect(nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM Failed. Is the NM running?");
        close(nm_socket);
        return -1;
    }
    logger("Successfully connected to NM!\n");
    // We'll build the message in a large buffer.
    char reg_msg[4096] = {0}; // 4KB buffer for file list
    
    // Start with BOTH ports (nm_port and client_port)
    // For simplicity, we'll use the same port for both NM and client connections
    int current_len = sprintf(reg_msg, "REGISTER_SS %d %d", my_port, my_port);

    // Now, scan the storage_root directory for files
    DIR *d;
    struct dirent *dir;
    d = opendir(STORAGE_ROOT); // Look in our dedicated storage folder
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
    
    // Parse SS_ID from response: "ACK:SS_REGISTRATION_OK <ss_id>\n"
    int my_ss_id = -1;
    if (sscanf(buffer, "ACK:SS_REGISTRATION_OK %d", &my_ss_id) == 1) {
        g_ss_id = my_ss_id;
        logger("Assigned SS_ID: %d\n", g_ss_id);
    } else {
        logger("Warning: Could not parse SS_ID from registration response\n");
    }
    
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