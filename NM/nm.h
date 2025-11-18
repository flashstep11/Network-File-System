// nm.h - All Name Server definitions (consolidated)

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

typedef struct TrieNode {
    struct TrieNode* children[128];
    FileMetadata* file_info;
    int is_end_of_word;
} TrieNode;

typedef struct StorageServer {
    int id;
    char ip[MAX_IP_LEN];
    int nm_port;
    int client_port;
    int is_active;
    int socket_fd;
    time_t last_heartbeat;
    int backup_ss_id;  // ID of backup buddy for replication (-1 if none)
    int file_count;  // Track number of files on this SS
    pthread_mutex_t lock;
} StorageServer;

typedef struct Client {
    char username[MAX_USERNAME_LEN];
    char ip[MAX_IP_LEN];
    int nm_port;
    int ss_port;
    int socket_fd;
    time_t connected_time;
    int is_active;
    pthread_mutex_t lock;
} Client;

typedef struct CacheNode {
    char filename[MAX_FILENAME_LEN];
    FileMetadata* file_info;
    struct CacheNode* prev;
    struct CacheNode* next;
    time_t timestamp;
} CacheNode;

typedef struct LRUCache {
    CacheNode* head;
    CacheNode* tail;
    int size;
    int capacity;
    pthread_mutex_t lock;
} LRUCache;

// Access request structure for bonus feature [5]
typedef struct AccessRequest {
    char username[MAX_USERNAME_LEN];      // Who is requesting
    char filename[MAX_FILENAME_LEN];      // Which file
    char flag[8];                         // "-R" or "-W"
    time_t request_time;
    int is_active;                        // 1 if pending, 0 if processed
} AccessRequest;

#define MAX_ACCESS_REQUESTS 200

typedef struct NameServer {
    StorageServer storage_servers[MAX_STORAGE_SERVERS];
    int ss_count;
    pthread_rwlock_t ss_lock;
    
    Client clients[MAX_CLIENTS];
    int client_count;
    pthread_rwlock_t client_lock;
    
    TrieNode* file_trie_root;
    pthread_rwlock_t trie_lock;
    
    LRUCache* search_cache;
    
    // Access requests (bonus feature)
    AccessRequest access_requests[MAX_ACCESS_REQUESTS];
    int request_count;
    pthread_rwlock_t request_lock;
    
    int server_socket;
    int running;
} NameServer;

// ======================== GLOBALS ========================
extern NameServer g_nm;
extern FILE* g_log_file;
extern pthread_mutex_t g_log_lock;

// ======================== FUNCTION PROTOTYPES ========================

// Logging
void nm_log_init(const char* log_filename);
void nm_log(const char* format, ...);
void nm_log_operation(const char* client_ip, int client_port, const char* username, 
                      const char* operation, const char* details, int status);
const char* error_code_to_string(int error_code);
void send_error(int socket_fd, int error_code, const char* message);
void send_success(int socket_fd, const char* message);
void nm_log_cleanup();

// Trie operations
TrieNode* trie_create_node(void);
void trie_insert(TrieNode* root, const char* filename, FileMetadata* file_info);
FileMetadata* trie_search(TrieNode* root, const char* filename);
void trie_delete(TrieNode* root, const char* filename);
void trie_free(TrieNode* node);

// LRU Cache operations
LRUCache* cache_create(int capacity);
FileMetadata* cache_get(LRUCache* cache, const char* filename);
void cache_put(LRUCache* cache, const char* filename, FileMetadata* file_info);
void cache_invalidate(LRUCache* cache, const char* filename);
void cache_free(LRUCache* cache);

// Storage Server management
int ss_register(NameServer* nm, const char* ip, int nm_port, int client_port, 
                const char* file_list, int socket_fd);
StorageServer* ss_get_by_id(NameServer* nm, int ss_id);
StorageServer* ss_get_for_file(NameServer* nm, const char* filename);
void ss_mark_inactive(NameServer* nm, int ss_id);
int ss_send_command(StorageServer* ss, const char* command, char* response, int response_size);

// Client management
int client_register(NameServer* nm, const char* username, const char* ip, 
                    int nm_port, int ss_port, int socket_fd);
Client* client_get_by_socket(NameServer* nm, int socket_fd);
void client_disconnect(NameServer* nm, int socket_fd);

// File operations
FileMetadata* file_create_metadata(const char* filename, const char* owner, int ss_id);
int file_add_to_registry(NameServer* nm, FileMetadata* file_info);
FileMetadata* file_lookup(NameServer* nm, const char* filename);
int file_delete_from_registry(NameServer* nm, const char* filename);
void file_update_stats(FileMetadata* file_info, long size, int words, int chars);

// Access control
int acl_check_read(FileMetadata* file_info, const char* username);
int acl_check_write(FileMetadata* file_info, const char* username);
int acl_add_access(FileMetadata* file_info, const char* username, int read, int write);
int acl_remove_access(FileMetadata* file_info, const char* username);

// Access request management (bonus feature [5])
int request_add(NameServer* nm, const char* username, const char* filename, const char* flag);
int request_exists(NameServer* nm, const char* username, const char* filename);
int request_remove(NameServer* nm, const char* username, const char* filename);
void request_get_for_owner(NameServer* nm, const char* owner, AccessRequest* results, int* count, int max_count);

// Notification system (bonus: The Unique Factor)
void notify_clients_editing(NameServer* nm, const char* filename, const char* editor_username, 
                            const char* action, int sentence_idx);

// Command handlers (pthread functions return void*)
void* handle_client_connection(void* arg);
void* handle_ss_connection(void* arg);

// Main NM functions
void nm_init(NameServer* nm);
void nm_cleanup(NameServer* nm);

#endif // NM_H
