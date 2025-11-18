

#include "nm.h"
#include "persistence.h"

// ======================== GLOBALS ========================
NameServer g_nm;
FILE* g_log_file = NULL;
pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

// ======================== LOGGING MODULE ========================

void nm_log_init(const char* log_filename) {
    g_log_file = fopen(log_filename, "a");
    if (g_log_file == NULL) {
        perror("Failed to open NM log file");
        exit(EXIT_FAILURE);
    }
    time_t now = time(NULL);
    char* time_str = ctime(&now);
    if (time_str) time_str[strlen(time_str) - 1] = '\0';
    nm_log("\n========== NAME SERVER STARTED: %s ==========\n", time_str ? time_str : "unknown");
}

void nm_log(const char* format, ...) {
    char time_buffer[26];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_buffer, 26, "[%Y-%m-%d %H:%M:%S]", tm_info);
    
    pthread_mutex_lock(&g_log_lock);
    printf("%s ", time_buffer);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    
    if (g_log_file) {
        fprintf(g_log_file, "%s ", time_buffer);
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_lock);
}

void nm_log_operation(const char* client_ip, int client_port, const char* username, 
                      const char* operation, const char* details, int status) {
    const char* status_str = (status == ERR_NONE) ? "SUCCESS" : "FAILED";
    nm_log("[OP] User=%s IP=%s:%d Op=%s Details=%s Status=%s\n",
           username ? username : "UNKNOWN", client_ip ? client_ip : "0.0.0.0",
           client_port, operation ? operation : "UNKNOWN", details ? details : "", status_str);
}

const char* error_code_to_string(int error_code) {
    switch(error_code) {
        case ERR_NONE: return "SUCCESS";
        case ERR_FILE_NOT_FOUND: return "FILE_NOT_FOUND";
        case ERR_ACCESS_DENIED: return "UNAUTHORIZED";
        case ERR_SS_OFFLINE: return "NO_STORAGE_SERVER";
        case ERR_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN_ERROR";
    }
}

void send_error(int socket_fd, int error_code, const char* message) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "ERR:%d:%s:%s\n", error_code, error_code_to_string(error_code), message);
    write(socket_fd, buffer, strlen(buffer));
    nm_log("[SENT ERROR] Code=%d Msg=%s\n", error_code, message);
}

void send_success(int socket_fd, const char* message) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "ACK:%s\n", message);
    write(socket_fd, buffer, strlen(buffer));
}

void nm_log_cleanup() {
    if (g_log_file) {
        nm_log("\n========== NAME SERVER SHUTDOWN ==========\n");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

// ======================== TRIE MODULE ========================

TrieNode* trie_create_node(void) {
    TrieNode* node = (TrieNode*)calloc(1, sizeof(TrieNode));
    if (!node) return NULL;
    node->is_end_of_word = 0;
    node->file_info = NULL;
    return node;
}

void trie_insert(TrieNode* root, const char* filename, FileMetadata* file_info) {
    if (!root || !filename || !file_info) return;
    TrieNode* current = root;
    int len = strlen(filename);
    for (int i = 0; i < len; i++) {
        int index = (unsigned char)filename[i];
        if (index >= 128) index = 127;
        if (!current->children[index]) {
            current->children[index] = trie_create_node();
            if (!current->children[index]) return;
        }
        current = current->children[index];
    }
    current->is_end_of_word = 1;
    current->file_info = file_info;
}

FileMetadata* trie_search(TrieNode* root, const char* filename) {
    if (!root || !filename) return NULL;
    TrieNode* current = root;
    int len = strlen(filename);
    for (int i = 0; i < len; i++) {
        int index = (unsigned char)filename[i];
        if (index >= 128) index = 127;
        if (!current->children[index]) return NULL;
        current = current->children[index];
    }
    return current->is_end_of_word ? current->file_info : NULL;
}

static int trie_is_empty(TrieNode* node) {
    for (int i = 0; i < 128; i++) {
        if (node->children[i]) return 0;
    }
    return 1;
}

static TrieNode* trie_delete_helper(TrieNode* node, const char* filename, int depth) {
    if (!node) return NULL;
    if (depth == strlen(filename)) {
        if (node->is_end_of_word) {
            node->is_end_of_word = 0;
            node->file_info = NULL;
        }
        if (trie_is_empty(node)) {
            free(node);
            node = NULL;
        }
        return node;
    }
    int index = (unsigned char)filename[depth];
    if (index >= 128) index = 127;
    node->children[index] = trie_delete_helper(node->children[index], filename, depth + 1);
    if (trie_is_empty(node) && !node->is_end_of_word) {
        free(node);
        node = NULL;
    }
    return node;
}

void trie_delete(TrieNode* root, const char* filename) {
    if (!root || !filename) return;
    trie_delete_helper(root, filename, 0);
}

void trie_free(TrieNode* node) {
    if (!node) return;
    for (int i = 0; i < 128; i++) {
        if (node->children[i]) trie_free(node->children[i]);
    }
    free(node);
}

// ======================== LRU CACHE MODULE ========================

LRUCache* cache_create(int capacity) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    if (!cache) return NULL;
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->capacity = capacity;
    pthread_mutex_init(&cache->lock, NULL);
    return cache;
}

static void cache_remove_node(LRUCache* cache, CacheNode* node) {
    if (!node) return;
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache->tail = node->prev;
    }
}

static void cache_add_to_front(LRUCache* cache, CacheNode* node) {
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) cache->head->prev = node;
    cache->head = node;
    if (!cache->tail) cache->tail = node;
}

FileMetadata* cache_get(LRUCache* cache, const char* filename) {
    if (!cache || !filename) return NULL;
    pthread_mutex_lock(&cache->lock);
    CacheNode* current = cache->head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            cache_remove_node(cache, current);
            cache_add_to_front(cache, current);
            current->timestamp = time(NULL);
            FileMetadata* result = current->file_info;
            pthread_mutex_unlock(&cache->lock);
            return result;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&cache->lock);
    return NULL;
}

void cache_put(LRUCache* cache, const char* filename, FileMetadata* file_info) {
    if (!cache || !filename || !file_info) return;
    pthread_mutex_lock(&cache->lock);
    CacheNode* current = cache->head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            current->file_info = file_info;
            current->timestamp = time(NULL);
            cache_remove_node(cache, current);
            cache_add_to_front(cache, current);
            pthread_mutex_unlock(&cache->lock);
            return;
        }
        current = current->next;
    }
    CacheNode* new_node = (CacheNode*)malloc(sizeof(CacheNode));
    if (!new_node) {
        pthread_mutex_unlock(&cache->lock);
        return;
    }
    strncpy(new_node->filename, filename, MAX_FILENAME_LEN - 1);
    new_node->filename[MAX_FILENAME_LEN - 1] = '\0';
    new_node->file_info = file_info;
    new_node->timestamp = time(NULL);
    new_node->prev = NULL;
    new_node->next = NULL;
    cache_add_to_front(cache, new_node);
    cache->size++;
    if (cache->size > cache->capacity) {
        CacheNode* lru = cache->tail;
        if (lru) {
            cache_remove_node(cache, lru);
            free(lru);
            cache->size--;
        }
    }
    pthread_mutex_unlock(&cache->lock);
}

void cache_invalidate(LRUCache* cache, const char* filename) {
    if (!cache || !filename) return;
    pthread_mutex_lock(&cache->lock);
    CacheNode* current = cache->head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            cache_remove_node(cache, current);
            free(current);
            cache->size--;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&cache->lock);
}

void cache_free(LRUCache* cache) {
    if (!cache) return;
    pthread_mutex_lock(&cache->lock);
    CacheNode* current = cache->head;
    while (current) {
        CacheNode* next = current->next;
        free(current);
        current = next;
    }
    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

// ======================== FILE & ACL MODULE ========================

FileMetadata* file_create_metadata(const char* filename, const char* owner, int ss_id) {
    FileMetadata* file_info = (FileMetadata*)malloc(sizeof(FileMetadata));
    if (!file_info) return NULL;
    strncpy(file_info->filename, filename, MAX_FILENAME_LEN - 1);
    file_info->filename[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(file_info->owner, owner, MAX_USERNAME_LEN - 1);
    file_info->owner[MAX_USERNAME_LEN - 1] = '\0';
    file_info->ss_id = ss_id;
    file_info->created_time = time(NULL);
    file_info->last_modified = file_info->created_time;
    file_info->last_accessed = file_info->created_time;
    file_info->file_size = 0;
    file_info->word_count = 0;
    file_info->char_count = 0;
    file_info->is_folder = 0;  // Default: not a folder
    file_info->acl = NULL;
    pthread_rwlock_init(&file_info->lock, NULL);
    acl_add_access(file_info, owner, 1, 1);
    return file_info;
}

int file_add_to_registry(NameServer* nm, FileMetadata* file_info) {
    if (!nm || !file_info) return -1;
    pthread_rwlock_wrlock(&nm->trie_lock);
    trie_insert(nm->file_trie_root, file_info->filename, file_info);
    pthread_rwlock_unlock(&nm->trie_lock);
    cache_put(nm->search_cache, file_info->filename, file_info);
    return 0;
}

FileMetadata* file_lookup(NameServer* nm, const char* filename) {
    if (!nm || !filename) return NULL;
    FileMetadata* cached = cache_get(nm->search_cache, filename);
    if (cached) return cached;
    pthread_rwlock_rdlock(&nm->trie_lock);
    FileMetadata* file_info = trie_search(nm->file_trie_root, filename);
    pthread_rwlock_unlock(&nm->trie_lock);
    if (file_info) cache_put(nm->search_cache, filename, file_info);
    return file_info;
}

int file_delete_from_registry(NameServer* nm, const char* filename) {
    if (!nm || !filename) return -1;
    cache_invalidate(nm->search_cache, filename);
    FileMetadata* file_info = file_lookup(nm, filename);
    if (file_info) {
        AccessEntry* current = file_info->acl;
        while (current) {
            AccessEntry* next = current->next;
            free(current);
            current = next;
        }
        pthread_rwlock_destroy(&file_info->lock);
        free(file_info);
    }
    pthread_rwlock_wrlock(&nm->trie_lock);
    trie_delete(nm->file_trie_root, filename);
    pthread_rwlock_unlock(&nm->trie_lock);
    return 0;
}

void file_update_stats(FileMetadata* file_info, long size, int words, int chars) {
    if (!file_info) return;
    pthread_rwlock_wrlock(&file_info->lock);
    file_info->file_size = size;
    file_info->word_count = words;
    file_info->char_count = chars;
    file_info->last_modified = time(NULL);
    pthread_rwlock_unlock(&file_info->lock);
}

int acl_check_read(FileMetadata* file_info, const char* username) {
    if (!file_info || !username) return 0;
    pthread_rwlock_rdlock(&file_info->lock);
    AccessEntry* current = file_info->acl;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            int result = current->can_read;
            pthread_rwlock_unlock(&file_info->lock);
            return result;
        }
        current = current->next;
    }
    pthread_rwlock_unlock(&file_info->lock);
    return 0;
}

int acl_check_write(FileMetadata* file_info, const char* username) {
    if (!file_info || !username) return 0;
    pthread_rwlock_rdlock(&file_info->lock);
    AccessEntry* current = file_info->acl;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            int result = current->can_write;
            pthread_rwlock_unlock(&file_info->lock);
            return result;
        }
        current = current->next;
    }
    pthread_rwlock_unlock(&file_info->lock);
    return 0;
}

int acl_add_access(FileMetadata* file_info, const char* username, int read, int write) {
    if (!file_info || !username) return -1;
    pthread_rwlock_wrlock(&file_info->lock);
    AccessEntry* current = file_info->acl;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            current->can_read = read;
            current->can_write = write;
            pthread_rwlock_unlock(&file_info->lock);
            return 0;
        }
        current = current->next;
    }
    AccessEntry* new_entry = (AccessEntry*)malloc(sizeof(AccessEntry));
    if (!new_entry) {
        pthread_rwlock_unlock(&file_info->lock);
        return -1;
    }
    strncpy(new_entry->username, username, MAX_USERNAME_LEN - 1);
    new_entry->username[MAX_USERNAME_LEN - 1] = '\0';
    new_entry->can_read = read;
    new_entry->can_write = write;
    new_entry->next = file_info->acl;
    file_info->acl = new_entry;
    pthread_rwlock_unlock(&file_info->lock);
    return 0;
}

int acl_remove_access(FileMetadata* file_info, const char* username) {
    if (!file_info || !username) return -1;
    if (strcmp(file_info->owner, username) == 0) return -1;
    pthread_rwlock_wrlock(&file_info->lock);
    AccessEntry* current = file_info->acl;
    AccessEntry* prev = NULL;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                file_info->acl = current->next;
            }
            free(current);
            pthread_rwlock_unlock(&file_info->lock);
            return 0;
        }
        prev = current;
        current = current->next;
    }
    pthread_rwlock_unlock(&file_info->lock);
    return -1;
}

// ======================== SS & CLIENT MANAGEMENT ========================

int ss_register(NameServer* nm, const char* ip, int nm_port, int client_port, 
                const char* file_list, int socket_fd) {
    if (!nm || !ip) return -1;
    pthread_rwlock_wrlock(&nm->ss_lock);
    if (nm->ss_count >= MAX_STORAGE_SERVERS) {
        pthread_rwlock_unlock(&nm->ss_lock);
        return -1;
    }
    int ss_id = nm->ss_count;
    StorageServer* ss = &nm->storage_servers[ss_id];
    ss->id = ss_id;
    strncpy(ss->ip, ip, MAX_IP_LEN - 1);
    ss->ip[MAX_IP_LEN - 1] = '\0';
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->is_active = 1;
    ss->socket_fd = socket_fd;
    ss->last_heartbeat = time(NULL);
    ss->file_count = 0;  // Initialize file count
    pthread_mutex_init(&ss->lock, NULL);
    nm->ss_count++;
    pthread_rwlock_unlock(&nm->ss_lock);
    nm_log("[SS%d] Registered %s (NM=%d, Client=%d)\n", ss_id, ip, nm_port, client_port);
    if (file_list && strlen(file_list) > 0) {
        char* file_list_copy = strdup(file_list);
        char* token = strtok(file_list_copy, " \t\n");
        int file_count = 0;
        int new_files = 0;
        int existing_files = 0;
        while (token != NULL) {
            // Check if file already exists in metadata (from persistence)
            FileMetadata* existing = file_lookup(nm, token);
            if (existing) {
                // File exists in metadata - just update the ss_id mapping
                pthread_rwlock_wrlock(&existing->lock);
                existing->ss_id = ss_id;
                pthread_rwlock_unlock(&existing->lock);
                existing_files++;
                nm_log("[SS%d] Updated ss_id for existing file: %s (owner=%s)\n", 
                       ss_id, token, existing->owner);
            } else {
                // New file not in metadata - create with owner="system"
                FileMetadata* file_info = file_create_metadata(token, "system", ss_id);
                if (file_info) {
                    file_add_to_registry(nm, file_info);
                    new_files++;
                    nm_log("[SS%d] Registered new file: %s (owner=system)\n", ss_id, token);
                }
            }
            file_count++;
            token = strtok(NULL, " \t\n");
        }
        free(file_list_copy);
        
        // Update file count for this SS
        pthread_rwlock_wrlock(&nm->ss_lock);
        ss->file_count = file_count;
        pthread_rwlock_unlock(&nm->ss_lock);
        
        nm_log("[SS%d] Registered %d files (%d new, %d existing with preserved metadata)\n", 
               ss_id, file_count, new_files, existing_files);
    }
    return ss_id;
}

StorageServer* ss_get_by_id(NameServer* nm, int ss_id) {
    if (!nm || ss_id < 0 || ss_id >= nm->ss_count) return NULL;
    pthread_rwlock_rdlock(&nm->ss_lock);
    StorageServer* ss = &nm->storage_servers[ss_id];
    pthread_rwlock_unlock(&nm->ss_lock);
    return ss->is_active ? ss : NULL;
}

StorageServer* ss_get_for_file(NameServer* nm, const char* filename) {
    if (!nm || !filename) return NULL;
    FileMetadata* file_info = file_lookup(nm, filename);
    if (!file_info) return NULL;
    return ss_get_by_id(nm, file_info->ss_id);
}

void ss_mark_inactive(NameServer* nm, int ss_id) {
    if (!nm || ss_id < 0 || ss_id >= nm->ss_count) return;
    pthread_rwlock_wrlock(&nm->ss_lock);
    nm->storage_servers[ss_id].is_active = 0;
    pthread_rwlock_unlock(&nm->ss_lock);
    nm_log("[SS%d] Marked inactive\n", ss_id);
}

int ss_send_command(StorageServer* ss, const char* command, char* response, int response_size) {
    if (!ss || !command) return -1;
    
    pthread_mutex_lock(&ss->lock);
    if (!ss->is_active) {
        pthread_mutex_unlock(&ss->lock);
        return -1;
    }
    int sock_fd = ss->socket_fd;
    pthread_mutex_unlock(&ss->lock);
    
    // Set socket timeout (2 seconds) to prevent indefinite blocking
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    ssize_t sent = write(sock_fd, command, strlen(command));
    if (sent <= 0) {
        pthread_mutex_lock(&ss->lock);
        ss->is_active = 0;
        pthread_mutex_unlock(&ss->lock);
        return -1;
    }
    
    if (response && response_size > 0) {
        memset(response, 0, response_size);
        ssize_t received = read(sock_fd, response, response_size - 1);
        if (received <= 0) {
            pthread_mutex_lock(&ss->lock);
            ss->is_active = 0;
            pthread_mutex_unlock(&ss->lock);
            return -1;
        }
        response[received] = '\0';
    }
    
    pthread_mutex_lock(&ss->lock);
    ss->last_heartbeat = time(NULL);
    pthread_mutex_unlock(&ss->lock);
    return 0;
}

int client_register(NameServer* nm, const char* username, const char* ip, 
                    int nm_port, int ss_port, int socket_fd) {
    if (!nm || !username || !ip) return -1;
    pthread_rwlock_wrlock(&nm->client_lock);
    if (nm->client_count >= MAX_CLIENTS) {
        pthread_rwlock_unlock(&nm->client_lock);
        return -1;
    }
    Client* client = &nm->clients[nm->client_count];
    strncpy(client->username, username, MAX_USERNAME_LEN - 1);
    client->username[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(client->ip, ip, MAX_IP_LEN - 1);
    client->ip[MAX_IP_LEN - 1] = '\0';
    client->nm_port = nm_port;
    client->ss_port = ss_port;
    client->socket_fd = socket_fd;
    client->connected_time = time(NULL);
    client->is_active = 1;
    pthread_mutex_init(&client->lock, NULL);
    nm->client_count++;
    pthread_rwlock_unlock(&nm->client_lock);
    nm_log("[CLIENT] %s @ %s:%d registered\n", username, ip, nm_port);
    return 0;
}

Client* client_get_by_socket(NameServer* nm, int socket_fd) {
    if (!nm) return NULL;
    pthread_rwlock_rdlock(&nm->client_lock);
    for (int i = 0; i < nm->client_count; i++) {
        if (nm->clients[i].socket_fd == socket_fd && nm->clients[i].is_active) {
            pthread_rwlock_unlock(&nm->client_lock);
            return &nm->clients[i];
        }
    }
    pthread_rwlock_unlock(&nm->client_lock);
    return NULL;
}

Client* client_get_by_ip(NameServer* nm, const char* ip) {
    if (!nm || !ip) return NULL;
    pthread_rwlock_rdlock(&nm->client_lock);
    for (int i = 0; i < nm->client_count; i++) {
        if (nm->clients[i].is_active && strcmp(nm->clients[i].ip, ip) == 0) {
            pthread_rwlock_unlock(&nm->client_lock);
            return &nm->clients[i];
        }
    }
    pthread_rwlock_unlock(&nm->client_lock);
    return NULL;
}

void client_disconnect(NameServer* nm, int socket_fd) {
    if (!nm) return;
    pthread_rwlock_wrlock(&nm->client_lock);
    for (int i = 0; i < nm->client_count; i++) {
        if (nm->clients[i].socket_fd == socket_fd) {
            nm->clients[i].is_active = 0;
            close(socket_fd);
            nm_log("[CLIENT] %s disconnected\n", nm->clients[i].username);
            break;
        }
    }
    pthread_rwlock_unlock(&nm->client_lock);
}

// ======================== FORWARD DECLARATIONS ========================
static void trie_collect_files(TrieNode* node, FileMetadata** files, int* count, int max_count);

// ======================== COMMAND HANDLERS (Part 1 of 2) ========================
// Due to size, splitting into logical sections

static void handle_create_command(Client* client, char* filename) {
    FileMetadata* existing = file_lookup(&g_nm, filename);
    if (existing) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND+5, "File exists");
        return;
    }
    pthread_rwlock_rdlock(&g_nm.ss_lock);
    StorageServer* ss = NULL;
    int min_files = INT_MAX;
    
    // Find the active SS with the least number of files (least-loaded)
    for (int i = 0; i < g_nm.ss_count; i++) {
        if (g_nm.storage_servers[i].is_active && g_nm.storage_servers[i].file_count < min_files) {
            ss = &g_nm.storage_servers[i];
            min_files = g_nm.storage_servers[i].file_count;
        }
    }
    pthread_rwlock_unlock(&g_nm.ss_lock);
    if (!ss) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "No SS available");
        return;
    }
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "CREATE %s\n", filename);
    char ss_response[BUFFER_SIZE];
    if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) < 0) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS communication failed");
        return;
    }
    if (strncmp(ss_response, "ACK:", 4) == 0) {
        FileMetadata* file_info = file_create_metadata(filename, client->username, ss->id);
        if (file_info) {
            file_add_to_registry(&g_nm, file_info);
            
            // Increment file count for this SS
            pthread_rwlock_wrlock(&g_nm.ss_lock);
            ss->file_count++;
            pthread_rwlock_unlock(&g_nm.ss_lock);
            
            send_success(client->socket_fd, "CREATE_OK");
            nm_log("[LoadBalance] Created file %s on SS%d (now has %d files)\n", 
                   filename, ss->id, ss->file_count);
            nm_log_operation(client->ip, client->nm_port, client->username, "CREATE", filename, ERR_NONE);
            // Save metadata after creating file
            persist_save_metadata(&g_nm);
        } else {
            send_error(client->socket_fd, ERR_UNKNOWN, "Failed to register file");
        }
    } else {
        write(client->socket_fd, ss_response, strlen(ss_response));
    }
}

static void handle_read_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    pthread_rwlock_wrlock(&file_info->lock);
    file_info->last_accessed = time(NULL);
    pthread_rwlock_unlock(&file_info->lock);
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "SS_INFO:%s:%d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "READ", filename, ERR_NONE);
}

static void handle_write_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_write(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No write permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "SS_INFO:%s:%d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "WRITE", filename, ERR_NONE);
}

// Checkpoint command handlers - route to SS like READ/WRITE
static void handle_checkpoint_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_write(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No write permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "ACK:SS_INFO %s %d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "CHECKPOINT", filename, ERR_NONE);
}

static void handle_viewcheckpoint_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "ACK:SS_INFO %s %d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "VIEWCHECKPOINT", filename, ERR_NONE);
}

static void handle_revert_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_write(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No write permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "ACK:SS_INFO %s %d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "REVERT", filename, ERR_NONE);
}

static void handle_listcheckpoints_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "ACK:SS_INFO %s %d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "LISTCHECKPOINTS", filename, ERR_NONE);
}

static void handle_diff_command(Client* client, char* args) {
    char filename[256], tag1[64], tag2[64];
    if (sscanf(args, "%255s %63s %63s", filename, tag1, tag2) != 3) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Usage: DIFF <filename> <tag1> <tag2>");
        return;
    }
    
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "ACK:SS_INFO %s %d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "DIFF", filename, ERR_NONE);
}

static void handle_delete_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (strcmp(file_info->owner, client->username) != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "Only owner can delete");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "DELETE %s\n", filename);
    char ss_response[BUFFER_SIZE];
    if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) < 0) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS communication failed");
        return;
    }
    if (strncmp(ss_response, "ACK:", 4) == 0) {
        file_delete_from_registry(&g_nm, filename);
        
        // Decrement file count for this SS
        pthread_rwlock_wrlock(&g_nm.ss_lock);
        if (ss->file_count > 0) ss->file_count--;
        pthread_rwlock_unlock(&g_nm.ss_lock);
        
        send_success(client->socket_fd, "DELETE_OK");
        nm_log("[LoadBalance] Deleted file %s from SS%d (now has %d files)\n", 
               filename, ss->id, ss->file_count);
        nm_log_operation(client->ip, client->nm_port, client->username, "DELETE", filename, ERR_NONE);
        // Save metadata after deleting file
        persist_save_metadata(&g_nm);
    } else {
        write(client->socket_fd, ss_response, strlen(ss_response));
    }
}

static void handle_info_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (ss && ss->is_active) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "GET_INFO %s\n", filename);
        char ss_response[BUFFER_SIZE];
        if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) == 0) {
            long size = 0, mtime = 0;
            if (sscanf(ss_response, "ACK:INFO %ld %ld", &size, &mtime) == 2) {
                pthread_rwlock_wrlock(&file_info->lock);
                file_info->file_size = size;
                file_info->last_modified = (time_t)mtime;
                pthread_rwlock_unlock(&file_info->lock);
            }
        }
    }
    pthread_rwlock_rdlock(&file_info->lock);
    char response[BUFFER_SIZE];
    char created_str[26], modified_str[26], accessed_str[26];
    struct tm *tm_info;
    tm_info = localtime(&file_info->created_time);
    strftime(created_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    tm_info = localtime(&file_info->last_modified);
    strftime(modified_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    tm_info = localtime(&file_info->last_accessed);
    strftime(accessed_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    int offset = snprintf(response, sizeof(response),
                         "FILE INFO:\nFilename: %s\nOwner: %s\nSize: %ld bytes\n"
                         "Created: %s\nModified: %s\nAccessed: %s\n"
                         "Storage Server: SS%d (%s:%d)\nAccess Control:\n",
                         file_info->filename, file_info->owner, file_info->file_size,
                         created_str, modified_str, accessed_str,
                         file_info->ss_id, ss ? ss->ip : "unknown", ss ? ss->client_port : 0);
    AccessEntry* ace = file_info->acl;
    while (ace && offset < sizeof(response) - 100) {
        offset += snprintf(response + offset, sizeof(response) - offset,
                          "  %s: %s%s\n", ace->username,
                          ace->can_read ? "R" : "", ace->can_write ? "W" : "");
        ace = ace->next;
    }
    pthread_rwlock_unlock(&file_info->lock);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "INFO", filename, ERR_NONE);
}

static void handle_stream_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "SS_INFO:%s:%d\n", ss->ip, ss->client_port);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "STREAM", filename, ERR_NONE);
}

static void handle_list_command(Client* client) {
    char response[BUFFER_SIZE];
    int offset = snprintf(response, sizeof(response), "USERS:\n");
    pthread_rwlock_rdlock(&g_nm.client_lock);
    for (int i = 0; i < g_nm.client_count && offset < sizeof(response) - 100; i++) {
        if (g_nm.clients[i].is_active) {
            offset += snprintf(response + offset, sizeof(response) - offset,
                              "%s (%s:%d)\n", g_nm.clients[i].username,
                              g_nm.clients[i].ip, g_nm.clients[i].nm_port);
        }
    }
    pthread_rwlock_unlock(&g_nm.client_lock);
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "LIST", "", ERR_NONE);
}

static void handle_addaccess_command(Client* client, char* args) {
    char flag[8], filename[MAX_FILENAME_LEN], username[MAX_USERNAME_LEN];
    if (sscanf(args, "%7s %255s %63s", flag, filename, username) != 3) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Invalid syntax");
        return;
    }
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (strcmp(file_info->owner, client->username) != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "Only owner can modify access");
        return;
    }
    int read_access = 0, write_access = 0;
    if (strcmp(flag, "-R") == 0) {
        read_access = 1;
    } else if (strcmp(flag, "-W") == 0) {
        read_access = 1;
        write_access = 1;
    } else {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Invalid flag (use -R or -W)");
        return;
    }
    if (acl_add_access(file_info, username, read_access, write_access) == 0) {
        send_success(client->socket_fd, "ACCESS_ADDED");
        nm_log_operation(client->ip, client->nm_port, client->username, "ADDACCESS", args, ERR_NONE);
        // Save metadata after modifying ACL
        persist_save_metadata(&g_nm);
    } else {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to add access");
    }
}

static void handle_remaccess_command(Client* client, char* args) {
    char filename[MAX_FILENAME_LEN], username[MAX_USERNAME_LEN];
    if (sscanf(args, "%255s %63s", filename, username) != 2) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Invalid syntax");
        return;
    }
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (strcmp(file_info->owner, client->username) != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "Only owner can modify access");
        return;
    }
    if (acl_remove_access(file_info, username) == 0) {
        send_success(client->socket_fd, "ACCESS_REMOVED");
        nm_log_operation(client->ip, client->nm_port, client->username, "REMACCESS", args, ERR_NONE);
        // Save metadata after modifying ACL
        persist_save_metadata(&g_nm);
    } else {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to remove access");
    }
}

static void handle_exec_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_read(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No read permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    
    // Request file content from SS
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "GET_CONTENT %s\n", filename);
    char ss_response[BUFFER_SIZE * 4];
    
    if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) < 0) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "Failed to fetch file");
        return;
    }
    
    // Check if we got an error
    if (strncmp(ss_response, "ERR:", 4) == 0) {
        write(client->socket_fd, ss_response, strlen(ss_response));
        return;
    }
    
    // Skip "ACK:CONTENT\n" header
    char* content = ss_response;
    if (strncmp(content, "ACK:CONTENT\n", 12) == 0) {
        content += 12; // Skip header
    }
    
    // Remove EOF marker if present
    char* eof_marker = strstr(content, "\nEOF\n");
    if (eof_marker) {
        *eof_marker = '\0';
    }
    
    nm_log("[EXEC] Executing commands from %s for user %s\n", filename, client->username);
    
    // Execute each line as a shell command
    char response[BUFFER_SIZE * 4];
    int offset = snprintf(response, sizeof(response), "EXEC OUTPUT:\n");
    
    char* line = strtok(content, "\n");
    while (line != NULL && offset < sizeof(response) - 1000) {
        // Trim leading/trailing whitespace
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        char* end = line + strlen(line) - 1;
        while (end > line && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
            *end = '\0';
            end--;
        }
        
        // Skip empty lines
        if (strlen(line) == 0) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Execute command with explicit shell
        offset += snprintf(response + offset, sizeof(response) - offset, "\n$ %s\n", line);
        
        // Use sh -c to ensure proper shell execution
        char shell_cmd[BUFFER_SIZE];
        snprintf(shell_cmd, sizeof(shell_cmd), "/bin/sh -c '%s'", line);
        
        FILE* pipe = popen(shell_cmd, "r");
        if (pipe) {
            char cmd_output[512];
            int got_output = 0;
            while (fgets(cmd_output, sizeof(cmd_output), pipe) != NULL && offset < sizeof(response) - 100) {
                offset += snprintf(response + offset, sizeof(response) - offset, "%s", cmd_output);
                got_output = 1;
            }
            int status = pclose(pipe);
            if (status != 0 && !got_output) {
                offset += snprintf(response + offset, sizeof(response) - offset, "(exit code: %d)\n", WEXITSTATUS(status));
            }
        } else {
            offset += snprintf(response + offset, sizeof(response) - offset, "Failed to execute command\n");
        }
        
        line = strtok(NULL, "\n");
    }
    
    offset += snprintf(response + offset, sizeof(response) - offset, "\nEND OF EXECUTION\n");
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "EXEC", filename, ERR_NONE);
}

static void handle_undo_command(Client* client, char* filename) {
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    if (!acl_check_write(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No write permission");
        return;
    }
    StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
    if (!ss || !ss->is_active) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
        return;
    }
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "UNDO %s\n", filename);
    char ss_response[BUFFER_SIZE];
    if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) < 0) {
        send_error(client->socket_fd, ERR_SS_OFFLINE, "SS communication failed");
        return;
    }
    write(client->socket_fd, ss_response, strlen(ss_response));
    int status = (strncmp(ss_response, "ACK:", 4) == 0) ? ERR_NONE : ERR_UNKNOWN;
    nm_log_operation(client->ip, client->nm_port, client->username, "UNDO", filename, status);
}

static void handle_createfolder_command(Client* client, char* foldername) {
    // Check if folder already exists
    FileMetadata* existing = file_lookup(&g_nm, foldername);
    if (existing) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND+5, "Folder/File already exists");
        return;
    }
    
    // Folders don't need to be on any SS - they're virtual in NM
    FileMetadata* folder_info = file_create_metadata(foldername, client->username, -1);
    if (!folder_info) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to create folder");
        return;
    }
    
    folder_info->is_folder = 1;  // Mark as folder
    file_add_to_registry(&g_nm, folder_info);
    send_success(client->socket_fd, "FOLDER_CREATED");
    nm_log("[Folder] Created folder: %s (owner=%s)\n", foldername, client->username);
    nm_log_operation(client->ip, client->nm_port, client->username, "CREATEFOLDER", foldername, ERR_NONE);
    persist_save_metadata(&g_nm);
}

static void handle_move_command(Client* client, char* args) {
    char source[MAX_FILENAME_LEN];
    char dest_folder[MAX_FILENAME_LEN];
    
    // Parse: MOVE <filename> <foldername>
    if (sscanf(args, "%255s %255s", source, dest_folder) != 2) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Usage: MOVE <filename> <foldername>");
        return;
    }
    
    // Check if source file exists
    FileMetadata* file_info = file_lookup(&g_nm, source);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "Source file not found");
        return;
    }
    
    // Check if user has write permission
    if (!acl_check_write(file_info, client->username)) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "No write permission on source");
        return;
    }
    
    // Check if destination folder exists
    FileMetadata* folder_info = file_lookup(&g_nm, dest_folder);
    if (!folder_info || !folder_info->is_folder) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "Destination folder not found");
        return;
    }
    
    // Extract filename from source path
    char* filename = strrchr(source, '/');
    if (filename) {
        filename++;  // Skip the '/'
    } else {
        filename = source;  // No path, just filename
    }
    
    // Build new path: dest_folder/filename
    char new_path[MAX_FILENAME_LEN];
    snprintf(new_path, sizeof(new_path), "%s/%s", dest_folder, filename);
    
    // Check if destination already exists
    if (file_lookup(&g_nm, new_path)) {
        send_error(client->socket_fd, ERR_UNKNOWN, "File already exists in destination");
        return;
    }
    
    // If it's an actual file (not folder), ask SS to move it
    if (!file_info->is_folder) {
        StorageServer* ss = ss_get_by_id(&g_nm, file_info->ss_id);
        if (!ss || !ss->is_active) {
            send_error(client->socket_fd, ERR_SS_OFFLINE, "SS unavailable");
            return;
        }
        
        // Send MOVE command to SS
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "MOVE %s %s\n", source, new_path);
        char ss_response[BUFFER_SIZE];
        if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) < 0) {
            send_error(client->socket_fd, ERR_SS_OFFLINE, "SS communication failed");
            return;
        }
        
        if (strncmp(ss_response, "ACK:", 4) != 0) {
            write(client->socket_fd, ss_response, strlen(ss_response));
            return;
        }
    }
    
    // Update metadata: remove old entry, add new one
    file_delete_from_registry(&g_nm, source);
    
    // Create new entry with updated path
    FileMetadata* new_file_info = file_create_metadata(new_path, file_info->owner, file_info->ss_id);
    if (new_file_info) {
        new_file_info->is_folder = file_info->is_folder;
        new_file_info->file_size = file_info->file_size;
        new_file_info->word_count = file_info->word_count;
        new_file_info->char_count = file_info->char_count;
        new_file_info->created_time = file_info->created_time;
        new_file_info->last_modified = time(NULL);
        new_file_info->last_accessed = file_info->last_accessed;
        
        // Copy ACL
        AccessEntry* ace = file_info->acl;
        while (ace) {
            acl_add_access(new_file_info, ace->username, ace->can_read, ace->can_write);
            ace = ace->next;
        }
        
        file_add_to_registry(&g_nm, new_file_info);
        send_success(client->socket_fd, "FILE_MOVED");
        nm_log("[Move] Moved %s to %s\n", source, new_path);
        nm_log_operation(client->ip, client->nm_port, client->username, "MOVE", args, ERR_NONE);
        persist_save_metadata(&g_nm);
    } else {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to update metadata");
    }
}

static void handle_viewfolder_command(Client* client, char* foldername) {
    // Check if folder exists
    FileMetadata* folder_info = file_lookup(&g_nm, foldername);
    if (!folder_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "Folder not found");
        return;
    }
    
    if (!folder_info->is_folder) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Not a folder");
        return;
    }
    
    // Collect all files in this folder
    FileMetadata* files[1000];
    int file_count = 0;
    
    pthread_rwlock_rdlock(&g_nm.trie_lock);
    trie_collect_files(g_nm.file_trie_root, files, &file_count, 1000);
    pthread_rwlock_unlock(&g_nm.trie_lock);
    
    // Build folder prefix
    char folder_prefix[MAX_FILENAME_LEN + 2];
    snprintf(folder_prefix, sizeof(folder_prefix), "%s/", foldername);
    int prefix_len = strlen(folder_prefix);
    
    // Filter files in this folder
    char response[BUFFER_SIZE * 4];
    int offset = snprintf(response, sizeof(response), "Contents of folder '%s':\n", foldername);
    
    int found_count = 0;
    for (int i = 0; i < file_count; i++) {
        // Check if file is directly in this folder (not in subfolder)
        if (strncmp(files[i]->filename, folder_prefix, prefix_len) == 0) {
            const char* relative_name = files[i]->filename + prefix_len;
            
            // Make sure it's directly in this folder (no more slashes)
            if (strchr(relative_name, '/') == NULL) {
                pthread_rwlock_rdlock(&files[i]->lock);
                const char* type = files[i]->is_folder ? "[DIR]" : "[FILE]";
                offset += snprintf(response + offset, sizeof(response) - offset,
                                 "  %s %s (owner: %s)\n", 
                                 type, relative_name, files[i]->owner);
                pthread_rwlock_unlock(&files[i]->lock);
                found_count++;
                
                if (offset >= sizeof(response) - 100) break;
            }
        }
    }
    
    if (found_count == 0) {
        offset += snprintf(response + offset, sizeof(response) - offset, "  (empty)\n");
    }
    
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "VIEWFOLDER", foldername, ERR_NONE);
}

// Helper to collect all files from trie (recursive)
static void trie_collect_files(TrieNode* node, FileMetadata** files, int* count, int max_count) {
    if (!node || *count >= max_count) return;
    
    if (node->is_end_of_word && node->file_info) {
        files[(*count)++] = node->file_info;
    }
    
    for (int i = 0; i < 128; i++) {
        if (node->children[i]) {
            trie_collect_files(node->children[i], files, count, max_count);
        }
    }
}

static void handle_view_command(Client* client, char* args) {
    // Trim any trailing whitespace from args
    if (args) {
        char* end = args + strlen(args) - 1;
        while (end >= args && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
    }
    
    int show_all = (args && strchr(args, 'a') != NULL);      // Check if 'a' flag present
    int show_details = (args && strchr(args, 'l') != NULL);  // Check if 'l' flag present
    
    char response[BUFFER_SIZE * 8];
    int offset = 0;
    
    // Collect all files
    FileMetadata* files[1000];
    int file_count = 0;
    
    pthread_rwlock_rdlock(&g_nm.trie_lock);
    trie_collect_files(g_nm.file_trie_root, files, &file_count, 1000);
    pthread_rwlock_unlock(&g_nm.trie_lock);
    
    // Determine header based on flags
    if (show_details) {
        // VIEW -l or VIEW -al: Always show details with owner
        offset = snprintf(response, sizeof(response), 
                         "| %-12s | %-6s | %-6s | %-21s | %-8s |\n",
                         "Filename", "Words", "Chars", "Last Access Time", "Owner");
        offset += snprintf(response + offset, sizeof(response) - offset,
                          "|--------------|--------|--------|----------------------|----------|\n");
    } else {
        // VIEW or VIEW -a: Just list filenames
        offset = snprintf(response, sizeof(response), "Files:\n");
    }
    
    int displayed = 0;
    for (int i = 0; i < file_count && offset < sizeof(response) - 200; i++) {
        FileMetadata* file = files[i];
        
        // Filter: VIEW and VIEW -l show only accessible files
        // VIEW -a and VIEW -al show all files
        if (!show_all && !acl_check_read(file, client->username)) {
            continue; // Skip files user can't access
        }
        
        if (show_details) {
            // Get fresh file stats from SS (read-only, don't update metadata)
            long size = 0;
            int words = 0;
            time_t mtime = file->last_modified;
            
            StorageServer* ss = ss_get_by_id(&g_nm, file->ss_id);
            if (ss && ss->is_active) {
                char command[BUFFER_SIZE];
                snprintf(command, sizeof(command), "GET_INFO %s\n", file->filename);
                char ss_response[BUFFER_SIZE];
                if (ss_send_command(ss, command, ss_response, sizeof(ss_response)) == 0) {
                    // Parse response into local variables (don't modify shared file metadata)
                    if (sscanf(ss_response, "ACK:INFO %ld %ld %d", &size, &mtime, &words) < 2) {
                        // Fallback to stored values if query fails
                        pthread_rwlock_rdlock(&file->lock);
                        size = file->file_size;
                        words = file->word_count;
                        mtime = file->last_modified;
                        pthread_rwlock_unlock(&file->lock);
                    }
                } else {
                    // SS query failed, use cached values
                    pthread_rwlock_rdlock(&file->lock);
                    size = file->file_size;
                    words = file->word_count;
                    mtime = file->last_modified;
                    pthread_rwlock_unlock(&file->lock);
                }
            } else {
                // SS offline, use cached values
                pthread_rwlock_rdlock(&file->lock);
                size = file->file_size;
                words = file->word_count;
                mtime = file->last_modified;
                pthread_rwlock_unlock(&file->lock);
            }
            
            char time_str[20];
            struct tm* tm_info = localtime(&mtime);
            strftime(time_str, 20, "%Y-%m-%d %H:%M", tm_info);
            
            // Get owner (thread-safe read)
            char owner_copy[MAX_USERNAME_LEN];
            pthread_rwlock_rdlock(&file->lock);
            strncpy(owner_copy, file->owner, MAX_USERNAME_LEN - 1);
            owner_copy[MAX_USERNAME_LEN - 1] = '\0';
            pthread_rwlock_unlock(&file->lock);
            
            // VIEW -l or VIEW -al: Always show owner in details view
            offset += snprintf(response + offset, sizeof(response) - offset,
                              "| %-12s | %-6d | %-6ld | %-20s | %-8s |\n",
                              file->filename, words, size, time_str, owner_copy);
        } else {
            // VIEW or VIEW -a: Just filename
            offset += snprintf(response + offset, sizeof(response) - offset,
                              "--> %s\n", file->filename);
        }
        displayed++;
    }
    
    if (show_details) {
        offset += snprintf(response + offset, sizeof(response) - offset,
                          "|--------------|--------|--------|----------------------|----------|\n");
    }
    
    offset += snprintf(response + offset, sizeof(response) - offset,
                      "\nTotal: %d file(s) displayed\n", displayed);
    
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "VIEW", 
                     args ? args : "", ERR_NONE);
}

// ======================== ACCESS REQUEST MANAGEMENT (BONUS [5]) ========================

int request_add(NameServer* nm, const char* username, const char* filename, const char* flag) {
    if (!nm || !username || !filename || !flag) return -1;
    
    pthread_rwlock_wrlock(&nm->request_lock);
    if (nm->request_count >= MAX_ACCESS_REQUESTS) {
        pthread_rwlock_unlock(&nm->request_lock);
        return -1;
    }
    
    AccessRequest* req = &nm->access_requests[nm->request_count];
    strncpy(req->username, username, MAX_USERNAME_LEN - 1);
    req->username[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(req->filename, filename, MAX_FILENAME_LEN - 1);
    req->filename[MAX_FILENAME_LEN - 1] = '\0';
    strncpy(req->flag, flag, 7);
    req->flag[7] = '\0';
    req->request_time = time(NULL);
    req->is_active = 1;
    nm->request_count++;
    pthread_rwlock_unlock(&nm->request_lock);
    return 0;
}

int request_exists(NameServer* nm, const char* username, const char* filename) {
    if (!nm || !username || !filename) return 0;
    
    pthread_rwlock_rdlock(&nm->request_lock);
    for (int i = 0; i < nm->request_count; i++) {
        if (nm->access_requests[i].is_active &&
            strcmp(nm->access_requests[i].username, username) == 0 &&
            strcmp(nm->access_requests[i].filename, filename) == 0) {
            pthread_rwlock_unlock(&nm->request_lock);
            return 1;
        }
    }
    pthread_rwlock_unlock(&nm->request_lock);
    return 0;
}

int request_remove(NameServer* nm, const char* username, const char* filename) {
    if (!nm || !username || !filename) return -1;
    
    pthread_rwlock_wrlock(&nm->request_lock);
    for (int i = 0; i < nm->request_count; i++) {
        if (nm->access_requests[i].is_active &&
            strcmp(nm->access_requests[i].username, username) == 0 &&
            strcmp(nm->access_requests[i].filename, filename) == 0) {
            nm->access_requests[i].is_active = 0;
            pthread_rwlock_unlock(&nm->request_lock);
            return 0;
        }
    }
    pthread_rwlock_unlock(&nm->request_lock);
    return -1;
}

void request_get_for_owner(NameServer* nm, const char* owner, AccessRequest* results, int* count, int max_count) {
    if (!nm || !owner || !results || !count) return;
    
    *count = 0;
    pthread_rwlock_rdlock(&nm->request_lock);
    for (int i = 0; i < nm->request_count && *count < max_count; i++) {
        if (!nm->access_requests[i].is_active) continue;
        
        // Check if this user owns the file
        FileMetadata* file_info = file_lookup(nm, nm->access_requests[i].filename);
        if (file_info && strcmp(file_info->owner, owner) == 0) {
            results[*count] = nm->access_requests[i];
            (*count)++;
        }
    }
    pthread_rwlock_unlock(&nm->request_lock);
}

// ======================== NOTIFICATION SYSTEM (BONUS: UNIQUE FACTOR) ========================

// ======================== ACCESS REQUEST COMMAND HANDLERS ========================

static void handle_requestaccess_command(Client* client, char* args) {
    char filename[MAX_FILENAME_LEN], flag[8];
    if (sscanf(args, "%255s %7s", filename, flag) != 2) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Usage: REQUESTACCESS <filename> <-R|-W>");
        return;
    }
    
    // Validate flag
    if (strcmp(flag, "-R") != 0 && strcmp(flag, "-W") != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Flag must be -R or -W");
        return;
    }
    
    // Check if file exists
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    
    // Check if already owner or has access
    if (strcmp(file_info->owner, client->username) == 0) {
        send_error(client->socket_fd, ERR_UNKNOWN, "You already own this file");
        return;
    }
    
    if ((strcmp(flag, "-R") == 0 && acl_check_read(file_info, client->username)) ||
        (strcmp(flag, "-W") == 0 && acl_check_write(file_info, client->username))) {
        send_error(client->socket_fd, ERR_UNKNOWN, "You already have this access");
        return;
    }
    
    // Check if request already exists
    if (request_exists(&g_nm, client->username, filename)) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Request already pending");
        return;
    }
    
    // Add request
    if (request_add(&g_nm, client->username, filename, flag) < 0) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to add request");
        return;
    }
    
    send_success(client->socket_fd, "REQUEST_SUBMITTED");
    nm_log("[REQUEST] %s requested %s access to %s (owner: %s)\n", 
           client->username, flag, filename, file_info->owner);
    nm_log_operation(client->ip, client->nm_port, client->username, "REQUESTACCESS", args, ERR_NONE);
}

static void handle_viewrequests_command(Client* client) {
    AccessRequest requests[MAX_ACCESS_REQUESTS];
    int count = 0;
    
    request_get_for_owner(&g_nm, client->username, requests, &count, MAX_ACCESS_REQUESTS);
    
    char response[BUFFER_SIZE * 2];
    int offset = snprintf(response, sizeof(response), "PENDING ACCESS REQUESTS:\n");
    
    if (count == 0) {
        offset += snprintf(response + offset, sizeof(response) - offset, "  (no pending requests)\n");
    } else {
        for (int i = 0; i < count && offset < sizeof(response) - 200; i++) {
            char time_str[20];
            struct tm* tm_info = localtime(&requests[i].request_time);
            strftime(time_str, 20, "%Y-%m-%d %H:%M", tm_info);
            
            offset += snprintf(response + offset, sizeof(response) - offset,
                             "  %s wants %s access to %s (requested: %s)\n",
                             requests[i].username, requests[i].flag, 
                             requests[i].filename, time_str);
        }
        offset += snprintf(response + offset, sizeof(response) - offset,
                         "\nTotal: %d request(s)\n", count);
    }
    
    write(client->socket_fd, response, strlen(response));
    nm_log_operation(client->ip, client->nm_port, client->username, "VIEWREQUESTS", "", ERR_NONE);
}

static void handle_approverequest_command(Client* client, char* args) {
    char username[MAX_USERNAME_LEN], filename[MAX_FILENAME_LEN];
    if (sscanf(args, "%63s %255s", username, filename) != 2) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Usage: APPROVEREQUEST <username> <filename>");
        return;
    }
    
    // Check if file exists and client is owner
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    
    if (strcmp(file_info->owner, client->username) != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "Only owner can approve requests");
        return;
    }
    
    // Find the request
    AccessRequest* req = NULL;
    pthread_rwlock_rdlock(&g_nm.request_lock);
    for (int i = 0; i < g_nm.request_count; i++) {
        if (g_nm.access_requests[i].is_active &&
            strcmp(g_nm.access_requests[i].username, username) == 0 &&
            strcmp(g_nm.access_requests[i].filename, filename) == 0) {
            req = &g_nm.access_requests[i];
            break;
        }
    }
    
    if (!req) {
        pthread_rwlock_unlock(&g_nm.request_lock);
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "Request not found");
        return;
    }
    
    // Grant access based on flag
    int read_access = 1;
    int write_access = (strcmp(req->flag, "-W") == 0) ? 1 : 0;
    char flag_copy[8];
    strncpy(flag_copy, req->flag, 7);
    flag_copy[7] = '\0';
    pthread_rwlock_unlock(&g_nm.request_lock);
    
    if (acl_add_access(file_info, username, read_access, write_access) < 0) {
        send_error(client->socket_fd, ERR_UNKNOWN, "Failed to grant access");
        return;
    }
    
    // Remove request
    request_remove(&g_nm, username, filename);
    
    send_success(client->socket_fd, "REQUEST_APPROVED");
    nm_log("[APPROVE] %s granted %s access to %s for %s\n", 
           client->username, flag_copy, filename, username);
    nm_log_operation(client->ip, client->nm_port, client->username, "APPROVEREQUEST", args, ERR_NONE);
    persist_save_metadata(&g_nm);
}

static void handle_denyrequest_command(Client* client, char* args) {
    char username[MAX_USERNAME_LEN], filename[MAX_FILENAME_LEN];
    if (sscanf(args, "%63s %255s", username, filename) != 2) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED-3, "Usage: DENYREQUEST <username> <filename>");
        return;
    }
    
    // Check if file exists and client is owner
    FileMetadata* file_info = file_lookup(&g_nm, filename);
    if (!file_info) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    
    if (strcmp(file_info->owner, client->username) != 0) {
        send_error(client->socket_fd, ERR_ACCESS_DENIED, "Only owner can deny requests");
        return;
    }
    
    // Check if request exists
    if (!request_exists(&g_nm, username, filename)) {
        send_error(client->socket_fd, ERR_FILE_NOT_FOUND, "Request not found");
        return;
    }
    
    // Remove request
    request_remove(&g_nm, username, filename);
    
    send_success(client->socket_fd, "REQUEST_DENIED");
    nm_log("[DENY] %s denied access request for %s from %s\n", 
           client->username, filename, username);
    nm_log_operation(client->ip, client->nm_port, client->username, "DENYREQUEST", args, ERR_NONE);
}

// ======================== CONNECTION HANDLERS ========================

void* handle_client_connection(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t received = read(client_socket, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        close(client_socket);
        return NULL;
    }
    buffer[received] = '\0';
    char username[MAX_USERNAME_LEN], ip[MAX_IP_LEN];
    int nm_port, ss_port;
    if (sscanf(buffer, "REGISTER_CLIENT %63s %15s %d %d", username, ip, &nm_port, &ss_port) != 4) {
        send_error(client_socket, ERR_ACCESS_DENIED-3, "Invalid registration");
        close(client_socket);
        return NULL;
    }
    if (client_register(&g_nm, username, ip, nm_port, ss_port, client_socket) < 0) {
        send_error(client_socket, ERR_UNKNOWN, "Failed to register");
        close(client_socket);
        return NULL;
    }
    send_success(client_socket, "REGISTRATION_OK");
    Client* client = client_get_by_socket(&g_nm, client_socket);
    if (!client) {
        close(client_socket);
        return NULL;
    }
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        received = read(client_socket, buffer, sizeof(buffer) - 1);
        if (received <= 0) break;
        buffer[received] = '\0';
        char* newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        char command[32], args[BUFFER_SIZE];
        memset(args, 0, sizeof(args));
        if (sscanf(buffer, "%31s", command) != 1) {
            send_error(client_socket, ERR_ACCESS_DENIED-3, "Empty command");
            continue;
        }
        char* space = strchr(buffer, ' ');
        if (space) {
            strncpy(args, space + 1, sizeof(args) - 1);
            args[sizeof(args) - 1] = '\0';
        }
        if (strcmp(command, "VIEW") == 0) handle_view_command(client, args);
        else if (strcmp(command, "READ") == 0) handle_read_command(client, args);
        else if (strcmp(command, "CREATE") == 0) handle_create_command(client, args);
        else if (strcmp(command, "WRITE") == 0) handle_write_command(client, args);
        else if (strcmp(command, "DELETE") == 0) handle_delete_command(client, args);
        else if (strcmp(command, "INFO") == 0) handle_info_command(client, args);
        else if (strcmp(command, "STREAM") == 0) handle_stream_command(client, args);
        else if (strcmp(command, "LIST") == 0) handle_list_command(client);
        else if (strcmp(command, "ADDACCESS") == 0) handle_addaccess_command(client, args);
        else if (strcmp(command, "REMACCESS") == 0) handle_remaccess_command(client, args);
        else if (strcmp(command, "EXEC") == 0) handle_exec_command(client, args);
        else if (strcmp(command, "UNDO") == 0) handle_undo_command(client, args);
        else if (strcmp(command, "CREATEFOLDER") == 0) handle_createfolder_command(client, args);
        else if (strcmp(command, "MOVE") == 0) handle_move_command(client, args);
        else if (strcmp(command, "VIEWFOLDER") == 0) handle_viewfolder_command(client, args);
        // Bonus feature: Checkpoints [15]
        else if (strcmp(command, "CHECKPOINT") == 0) handle_checkpoint_command(client, args);
        else if (strcmp(command, "VIEWCHECKPOINT") == 0) handle_viewcheckpoint_command(client, args);
        else if (strcmp(command, "REVERT") == 0) handle_revert_command(client, args);
        else if (strcmp(command, "LISTCHECKPOINTS") == 0) handle_listcheckpoints_command(client, args);
        else if (strcmp(command, "DIFF") == 0) handle_diff_command(client, args);
        // Bonus feature: Request Access [5]
        else if (strcmp(command, "REQUESTACCESS") == 0) handle_requestaccess_command(client, args);
        else if (strcmp(command, "VIEWREQUESTS") == 0) handle_viewrequests_command(client);
        else if (strcmp(command, "APPROVEREQUEST") == 0) handle_approverequest_command(client, args);
        else if (strcmp(command, "DENYREQUEST") == 0) handle_denyrequest_command(client, args);
        else if (strcmp(command, "QUIT") == 0 || strcmp(command, "EXIT") == 0) {
            send_success(client_socket, "GOODBYE");
            break;
        }
        else send_error(client_socket, ERR_ACCESS_DENIED-3, "Unknown command");
    }
    client_disconnect(&g_nm, client_socket);
    return NULL;
}

void* handle_ss_connection(void* arg) {
    int ss_socket = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE * 2];
    memset(buffer, 0, sizeof(buffer));
    ssize_t received = read(ss_socket, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        close(ss_socket);
        return NULL;
    }
    buffer[received] = '\0';
    if (strncmp(buffer, "REGISTER_SS ", 12) != 0) {
        send_error(ss_socket, ERR_ACCESS_DENIED-3, "Invalid SS registration");
        close(ss_socket);
        return NULL;
    }
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(ss_socket, (struct sockaddr*)&addr, &addr_len);
    char ip[MAX_IP_LEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, MAX_IP_LEN);
    char* parse_ptr = buffer + 12;
    int nm_port, client_port;
    if (sscanf(parse_ptr, "%d %d", &nm_port, &client_port) != 2) {
        send_error(ss_socket, ERR_ACCESS_DENIED-3, "Invalid port format");
        close(ss_socket);
        return NULL;
    }
    char* file_list_start = parse_ptr;
    for (int i = 0; i < 2; i++) {
        file_list_start = strchr(file_list_start, ' ');
        if (file_list_start) file_list_start++;
    }
    int ss_id = ss_register(&g_nm, ip, nm_port, client_port, 
                            file_list_start ? file_list_start : "", ss_socket);
    if (ss_id < 0) {
        send_error(ss_socket, ERR_UNKNOWN, "Failed to register SS");
        close(ss_socket);
        return NULL;
    }
    send_success(ss_socket, "SS_REGISTRATION_OK");
    char heartbeat[16];
    while (1) {
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(ss_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ssize_t n = read(ss_socket, heartbeat, sizeof(heartbeat));
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                StorageServer* ss = ss_get_by_id(&g_nm, ss_id);
                if (ss && ss->is_active) continue;
            }
            break;
        }
    }
    ss_mark_inactive(&g_nm, ss_id);
    close(ss_socket);
    return NULL;
}

// ======================== MAIN NM FUNCTIONS ========================

void nm_init(NameServer* nm) {
    memset(nm, 0, sizeof(NameServer));
    pthread_rwlock_init(&nm->ss_lock, NULL);
    pthread_rwlock_init(&nm->client_lock, NULL);
    pthread_rwlock_init(&nm->trie_lock, NULL);
    nm->file_trie_root = trie_create_node();
    if (!nm->file_trie_root) {
        fprintf(stderr, "Failed to create Trie root\n");
        exit(EXIT_FAILURE);
    }
    nm->search_cache = cache_create(LRU_CACHE_SIZE);
    if (!nm->search_cache) {
        fprintf(stderr, "Failed to create LRU cache\n");
        exit(EXIT_FAILURE);
    }
    nm->ss_count = 0;
    nm->client_count = 0;
    nm->request_count = 0;  // Initialize access request count
    pthread_rwlock_init(&nm->request_lock, NULL);  // Initialize request lock
    nm->running = 1;
    nm_log("Name Server initialized successfully\n");
    
    // Load persistent metadata from disk
    if (persist_load_metadata(nm) < 0) {
        nm_log("[WARNING] Failed to load metadata, starting fresh\n");
    }
}

void nm_cleanup(NameServer* nm) {
    nm_log("Shutting down Name Server...\n");
    nm->running = 0;
    
    // Save metadata before shutdown
    nm_log("Saving metadata to disk...\n");
    persist_save_metadata(nm);
    
    pthread_rwlock_wrlock(&nm->client_lock);
    for (int i = 0; i < nm->client_count; i++) {
        if (nm->clients[i].is_active) close(nm->clients[i].socket_fd);
        pthread_mutex_destroy(&nm->clients[i].lock);
    }
    pthread_rwlock_unlock(&nm->client_lock);
    pthread_rwlock_wrlock(&nm->ss_lock);
    for (int i = 0; i < nm->ss_count; i++) {
        if (nm->storage_servers[i].is_active) close(nm->storage_servers[i].socket_fd);
        pthread_mutex_destroy(&nm->storage_servers[i].lock);
    }
    pthread_rwlock_unlock(&nm->ss_lock);
    if (nm->file_trie_root) trie_free(nm->file_trie_root);
    if (nm->search_cache) cache_free(nm->search_cache);
    pthread_rwlock_destroy(&nm->ss_lock);
    pthread_rwlock_destroy(&nm->client_lock);
    pthread_rwlock_destroy(&nm->trie_lock);
    pthread_rwlock_destroy(&nm->request_lock);  // Cleanup request lock
    if (nm->server_socket > 0) close(nm->server_socket);
    nm_log("Name Server shutdown complete\n");
}

int main(int argc, char* argv[]) {
    printf("=========================================\n");
    printf("  Name Server (NM) - Network File System \n");
    printf("=========================================\n\n");
    nm_log_init("name_server.log");
    nm_init(&g_nm);
    g_nm.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_nm.server_socket < 0) {
        perror("Failed to create server socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(g_nm.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(NM_PORT);
    if (bind(g_nm.server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(g_nm.server_socket, MAX_CLIENTS + MAX_STORAGE_SERVERS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    nm_log("Name Server listening on port %d\n", NM_PORT);
    printf("Name Server is ONLINE on port %d\n", NM_PORT);
    printf("Waiting for Storage Servers and Clients...\n\n");
    while (g_nm.running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int new_socket = accept(g_nm.server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (new_socket < 0) {
            if (errno == EINTR) continue;
            perror("Accept failed");
            continue;
        }
        char client_ip[MAX_IP_LEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, MAX_IP_LEN);
        int client_port = ntohs(client_addr.sin_port);
        nm_log("[CONNECT] New connection from %s:%d\n", client_ip, client_port);
        char peek_buf[16];
        ssize_t peek_size = recv(new_socket, peek_buf, sizeof(peek_buf), MSG_PEEK);
        if (peek_size <= 0) {
            close(new_socket);
            continue;
        }
        pthread_t thread_id;
        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = new_socket;
        if (strncmp(peek_buf, "REGISTER_SS", 11) == 0) {
            if (pthread_create(&thread_id, NULL, handle_ss_connection, socket_ptr) != 0) {
                perror("Failed to create SS handler thread");
                free(socket_ptr);
                close(new_socket);
                continue;
            }
        } else if (strncmp(peek_buf, "REGISTER_CLIENT", 15) == 0) {
            if (pthread_create(&thread_id, NULL, handle_client_connection, socket_ptr) != 0) {
                perror("Failed to create client handler thread");
                free(socket_ptr);
                close(new_socket);
                continue;
            }
        } else if (strncmp(peek_buf, "CHECK_ACCESS", 12) == 0) {
            // Handle CHECK_ACCESS synchronously (quick query from SS)
            char buffer[512];
            ssize_t n = read(new_socket, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                
                // Parse: CHECK_ACCESS <client_ip> <filename> <mode>
                char client_ip_query[MAX_IP_LEN], filename[MAX_FILENAME_LEN], mode[16];
                if (sscanf(buffer, "CHECK_ACCESS %15s %255s %15s", client_ip_query, filename, mode) == 3) {
                    nm_log("[CHECK_ACCESS] IP=%s File=%s Mode=%s\n", client_ip_query, filename, mode);
                    
                    // Look up client by IP to get username
                    Client* client = client_get_by_ip(&g_nm, client_ip_query);
                    if (!client) {
                        write(new_socket, "ACK:NO Client not found\n", 24);
                        nm_log("[CHECK_ACCESS] Client with IP %s not found\n", client_ip_query);
                        close(new_socket);
                        free(socket_ptr);
                        continue;
                    }
                    
                    char check_username[MAX_USERNAME_LEN];
                    strncpy(check_username, client->username, MAX_USERNAME_LEN - 1);
                    check_username[MAX_USERNAME_LEN - 1] = '\0';
                    
                    // Find file
                    FileMetadata* file_info = file_lookup(&g_nm, filename);
                    if (!file_info) {
                        write(new_socket, "ACK:NO File not found\n", 23);
                        nm_log("[CHECK_ACCESS] File not found: %s\n", filename);
                    } else {
                        // Check permissions by username
                        int allowed = 0;
                        if (strcmp(mode, "READ") == 0) {
                            allowed = acl_check_read(file_info, check_username);
                        } else if (strcmp(mode, "WRITE") == 0) {
                            allowed = acl_check_write(file_info, check_username);
                        }
                        
                        if (allowed) {
                            write(new_socket, "ACK:YES\n", 8);
                            nm_log("[CHECK_ACCESS] Granted %s access to %s for %s (IP: %s)\n", mode, filename, check_username, client_ip_query);
                        } else {
                            write(new_socket, "ACK:NO No permission\n", 21);
                            nm_log("[CHECK_ACCESS] Denied %s access to %s for %s (IP: %s)\n", mode, filename, check_username, client_ip_query);
                        }
                    }
                } else {
                    write(new_socket, "ACK:NO Bad request\n", 19);
                }
            }
            close(new_socket);
            free(socket_ptr);
            continue;
        } else {
            nm_log("[ERROR] Unknown connection type from %s:%d\n", client_ip, client_port);
            free(socket_ptr);
            close(new_socket);
            continue;
        }
        pthread_detach(thread_id);
    }
    nm_cleanup(&g_nm);
    nm_log_cleanup();
    printf("\nName Server terminated.\n");
    return 0;
}
