
#ifndef NM_H
#define NM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include "../comms/def.h"

// ======================== CONSTANTS ========================
#define NM_PORT 8080
#define BUFFER_SIZE 8192
#define MAX_CLIENTS 100
#define MAX_STORAGE_SERVERS 50
#define MAX_FILENAME_LEN 256
#define MAX_USERNAME_LEN 64
#define MAX_IP_LEN 16
#define LRU_CACHE_SIZE 100

// ======================== DATA STRUCTURES ========================

typedef struct AccessEntry {
    char username[MAX_USERNAME_LEN];
    int can_read;
    int can_write;
    struct AccessEntry* next;
} AccessEntry;

typedef struct FileMetadata {
    char filename[MAX_FILENAME_LEN];  // Full path: "folder/subfolder/file.txt"
    char owner[MAX_USERNAME_LEN];
    int ss_id;
    time_t created_time;
    time_t last_modified;
    time_t last_accessed;
    long file_size;
    int word_count;
    int char_count;
    int is_folder;  // 1 if this is a folder, 0 if file
    AccessEntry* acl;
    pthread_rwlock_t lock;
} FileMetadata;

// Trie node for O(m) file path lookups (m = path length)
// 128-way branching for ASCII characters
typedef struct TrieNode {
    struct TrieNode* children[128];   // ASCII character map
    FileMetadata* file_info;          // Non-NULL if this is end of path
    int is_end_of_word;               // 1 if valid filename ends here
} TrieNode;

// Storage Server state (registered during SS startup)
typedef struct StorageServer {
    int id;                           // Assigned by NM (0, 1, 2, 3...)
    char ip[MAX_IP_LEN];              // Auto-detected via getpeername()
    int nm_port;                      // Port for NM commands
    int client_port;                  // Port for client file operations
    int is_active;                    // 1 = online, 0 = offline (heartbeat failed)
    int socket_fd;                    // Persistent connection to SS
    time_t last_heartbeat;            // Last PING response time
    int backup_ss_id;                 // Backup buddy (0↔1, 2↔3) for replication
    int file_count;                   // For load balancing (pick SS with min files)
    pthread_mutex_t lock;             // Protects this SS struct
} StorageServer;

// Client session (registered during login)
typedef struct Client {
    char username[MAX_USERNAME_LEN];  // Username for ACL checks
    char ip[MAX_IP_LEN];              // Client IP address
    int nm_port;                      // Client's NM port (for mapping IP to user)
    int ss_port;                      // Client's SS port
    int socket_fd;                    // Connection socket (closed after each command)
    time_t connected_time;            // Session start time
    int is_active;                    // 1 = logged in, 0 = disconnected
    pthread_mutex_t lock;             // Protects this client struct
} Client;

// LRU cache node (doubly-linked list)
typedef struct CacheNode {
    char filename[MAX_FILENAME_LEN];
    FileMetadata* file_info;          // Pointer to actual metadata (not copied)
    struct CacheNode* prev;           // For O(1) removal
    struct CacheNode* next;           // For O(1) insertion
    time_t timestamp;                 // Last access time
} CacheNode;

// LRU cache for hot files (avoids repeated trie traversals)
typedef struct LRUCache {
    CacheNode* head;                  // Most recently used
    CacheNode* tail;                  // Least recently used (evict first)
    int size;                         // Current entries
    int capacity;                     // Max entries (LRU_CACHE_SIZE)
    pthread_mutex_t lock;             // Thread-safe cache access
} LRUCache;

// Access request for GRANT command (bonus feature)
// Users request access, owner approves with GRANT
typedef struct AccessRequest {
    char username[MAX_USERNAME_LEN];      // Who wants access
    char filename[MAX_FILENAME_LEN];      // To which file
    char flag[8];                         // "-R" (read) or "-W" (write)
    time_t request_time;                  // When requested
    int is_active;                        // 1 = pending, 0 = approved/denied
} AccessRequest;

#define MAX_ACCESS_REQUESTS 200

// Main Name Server state - the central brain of NFS
typedef struct NameServer {
    // Storage Servers (0-3 typically, paired as 0↔1, 2↔3 for replication)
    StorageServer storage_servers[MAX_STORAGE_SERVERS];
    int ss_count;                         // Number of registered SS
    pthread_rwlock_t ss_lock;             // Protects ss_count and array
    
    // Clients (logged in users)
    Client clients[MAX_CLIENTS];
    int client_count;                     // Number of active clients
    pthread_rwlock_t client_lock;         // Protects client_count and array
    
    // File registry (trie for fast lookup + LRU cache)
    TrieNode* file_trie_root;             // O(m) path lookup
    pthread_rwlock_t trie_lock;           // Protects trie operations
    
    LRUCache* search_cache;               // Hot files for O(1) access
    
    // Access request queue (bonus feature)
    AccessRequest access_requests[MAX_ACCESS_REQUESTS];
    int request_count;
    pthread_rwlock_t request_lock;        // Protects request array
    
    int server_socket;                    // NM listening socket (port 8080)
    int running;                          // 1 = active, 0 = shutdown
} NameServer;

// Global Name Server instance (single instance per NM process)
extern NameServer g_nm;
extern FILE* g_log_file;                  // name_server.log
extern pthread_mutex_t g_log_lock;        // Thread-safe logging

// Function prototypes organized by subsystem

// === Logging subsystem ===
void nm_log_init(const char* log_filename);             // Initialize log file
void nm_log(const char* format, ...);                   // Thread-safe logging
void nm_log_operation(const char* client_ip, int client_port, const char* username, 
                      const char* operation, const char* details, int status);
const char* error_code_to_string(int error_code);       // ERR_FILE_NOT_FOUND → "File not found"
void send_error(int socket_fd, int error_code, const char* message);  // Send ERR:code:msg
void send_success(int socket_fd, const char* message);  // Send ACK:msg
void nm_log_cleanup();                                  // Close log file

// === Trie operations (file path lookup) ===
TrieNode* trie_create_node(void);                       // Allocate new trie node
void trie_insert(TrieNode* root, const char* filename, FileMetadata* file_info);  // Add file
FileMetadata* trie_search(TrieNode* root, const char* filename);  // Find file (O(m))
void trie_delete(TrieNode* root, const char* filename); // Remove file from trie
void trie_free(TrieNode* node);                         // Recursive cleanup

// === LRU Cache (hot file optimization) ===
LRUCache* cache_create(int capacity);                   // Initialize cache
FileMetadata* cache_get(LRUCache* cache, const char* filename);  // O(1) lookup
void cache_put(LRUCache* cache, const char* filename, FileMetadata* file_info);  // Add/update
void cache_invalidate(LRUCache* cache, const char* filename);  // Remove from cache
void cache_free(LRUCache* cache);                       // Cleanup

// === Storage Server management ===
int ss_register(NameServer* nm, const char* ip, int nm_port, int client_port, 
                const char* file_list, int socket_fd);  // Register new SS
StorageServer* ss_get_by_id(NameServer* nm, int ss_id); // Get SS by ID
StorageServer* ss_get_for_file(NameServer* nm, const char* filename);  // Which SS has file?
void ss_mark_inactive(NameServer* nm, int ss_id);       // Mark SS offline (heartbeat failed)
int ss_send_command(StorageServer* ss, const char* command, char* response, int response_size);  // Send cmd to SS

// === Client management ===
int client_register(NameServer* nm, const char* username, const char* ip, 
                    int nm_port, int ss_port, int socket_fd);  // Register client login
Client* client_get_by_socket(NameServer* nm, int socket_fd);  // Find client by socket
void client_disconnect(NameServer* nm, int socket_fd);  // Handle logout

// === File metadata operations ===
FileMetadata* file_create_metadata(const char* filename, const char* owner, int ss_id);  // New file metadata
int file_add_to_registry(NameServer* nm, FileMetadata* file_info);  // Add to trie
FileMetadata* file_lookup(NameServer* nm, const char* filename);  // Search trie + cache
int file_delete_from_registry(NameServer* nm, const char* filename);  // Remove from registry
void file_update_stats(FileMetadata* file_info, long size, int words, int chars);  // Update metadata

// === Access Control List (ACL) ===
int acl_check_read(FileMetadata* file_info, const char* username);   // Can user read?
int acl_check_write(FileMetadata* file_info, const char* username);  // Can user write?
int acl_add_access(FileMetadata* file_info, const char* username, int read, int write);  // Grant permission
int acl_remove_access(FileMetadata* file_info, const char* username);  // Revoke permission

// === Access request queue (bonus: REQUEST/GRANT) ===
int request_add(NameServer* nm, const char* username, const char* filename, const char* flag);  // Add request
int request_exists(NameServer* nm, const char* username, const char* filename);  // Check pending request
int request_remove(NameServer* nm, const char* username, const char* filename);  // Remove request (approved/denied)
void request_get_for_owner(NameServer* nm, const char* owner, AccessRequest* results, int* count, int max_count);  // List requests

// === Notification system (bonus: real-time alerts) ===
void notify_clients_editing(NameServer* nm, const char* filename, const char* editor_username, 
                            const char* action, int sentence_idx);  // Notify concurrent editors

// === Connection handlers (thread entry points) ===
void* handle_client_connection(void* arg);              // Client command handler thread
void* handle_ss_connection(void* arg);                  // SS registration thread

// === Lifecycle ===
void nm_init(NameServer* nm);                           // Initialize NM state
void nm_cleanup(NameServer* nm);                        // Shutdown and cleanup

#endif // NM_H
