// defs.h - Storage Server data structures and utility functions
// Includes sentence-level locking and checkpoint system for file versioning

#ifndef DEFS_H
#define DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#define NM_IP "127.0.0.1"
#define NM_PORT 8080
#define BUFFER_SIZE 4096                 // Increased for large file operations
#define MAX_CLIENTS 10                   // Max concurrent client connections per SS

// Storage root changes per SS (e.g., storage_root_8081, storage_root_8082)
extern char STORAGE_ROOT[64];

// Helper: Construct full filesystem path from virtual path
static inline void get_full_path(char *dest, size_t dest_size, const char *filename) {
    snprintf(dest, dest_size, "%s%s", STORAGE_ROOT, filename);
}

// Helper: Extract filename from path ("folder/file.txt" → "file.txt")
static inline void get_base_filename(char *dest, size_t dest_size, const char *path) {
    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        strncpy(dest, last_slash + 1, dest_size - 1);
    } else {
        strncpy(dest, path, dest_size - 1);
    }
    dest[dest_size - 1] = '\0';
}

// Sentence lock node (linked list tracking active locks)
// KEY: Allows concurrent writes to DIFFERENT sentences (30 bonus points)
typedef struct SentenceLockNode {
    char filename[256];              // Which file
    int sentence_id;                 // Which sentence (0, 1, 2...)
    struct SentenceLockNode* next;   // Next locked sentence
} SentenceLockNode;

// Sentence lock manager (global registry of all locks)
typedef struct {
    SentenceLockNode* head;          // Linked list of active locks
    pthread_mutex_t manager_lock;    // Protects list operations
} SentenceLockManager;

extern SentenceLockManager* g_sentence_lock_manager; 

// SS identity (set during NM registration)
extern int g_ss_id;                  // SS0, SS1, SS2, SS3
extern char g_nm_ip[32];             // NM IP for callbacks
extern int g_nm_port;                // NM port (8080)

// Checkpoint system for file versioning (CREATE <tag>, REVERT <tag>, DIFF)
typedef struct CheckpointNode {
    char tag[64];                    // User-defined tag name
    char* content;                   // Snapshot of file content
    time_t timestamp;                // When checkpoint was created
    struct CheckpointNode* next;     // Next checkpoint for this file
} CheckpointNode;

typedef struct FileCheckpoints {
    char filename[256];              // Which file
    CheckpointNode* checkpoints;     // Linked list of versions
    pthread_mutex_t lock;            // Protects checkpoint operations
    struct FileCheckpoints* next;    // Next file in global list
} FileCheckpoints;

extern FileCheckpoints* g_checkpoints_head;
extern pthread_mutex_t g_checkpoints_lock;

// === Function prototypes ===

// Logging
void logger(const char* format, ...);                    // Thread-safe SS logging

// File parsing utilities
char* read_file_to_string(const char *filename, long *out_size);  // Load entire file
int find_sentence(const char *content, int sentence_num, int *start, int *end);  // Locate sentence by index
int find_word(const char *content, int sent_start, int sent_end, int word_index, int *word_start, int *word_end);  // Locate word

// Sentence locking (critical for concurrent WRITE support)
void init_sentence_locks();                              // Initialize lock manager
int acquire_sentence_lock(const char* filename, int sentence_id);  // Try lock (0=fail, 1=success)
void release_sentence_lock(const char* filename, int sentence_id);  // Release lock
int is_file_being_edited(const char* filename);         // Check if any sentence locked

// Checkpoint/versioning functions
void init_checkpoints();                                 // Initialize checkpoint system
int create_checkpoint(const char* filename, const char* tag);  // Save current version
char* get_checkpoint_content(const char* filename, const char* tag);  // Retrieve version
int revert_to_checkpoint(const char* filename, const char* tag);  // Restore version
char* list_checkpoints(const char* filename);           // List all tags
char* diff_checkpoints(const char* filename, const char* tag1, const char* tag2);  // Compare versions

// File mutex (for read consistency, sentence locks handle write concurrency)
pthread_mutex_t* get_file_mutex(const char* filename);  // Get or create file mutex

#endif