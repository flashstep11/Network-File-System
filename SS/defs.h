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
#define BUFFER_SIZE 4096 // Increased for larger files
#define MAX_CLIENTS 10
#define STORAGE_ROOT "storage_root/"

// Helper to get full path
static inline void get_full_path(char *dest, size_t dest_size, const char *filename) {
    snprintf(dest, dest_size, "%s%s", STORAGE_ROOT, filename);
}

// --- NEW LOCKING STRUCTURES ---

// A node representing ONE locked sentence
typedef struct SentenceLockNode {
    char filename[256];
    int sentence_id;
    struct SentenceLockNode* next;
} SentenceLockNode;

// A simplified manager that tracks ALL active sentence locks
typedef struct {
    SentenceLockNode* head;
    pthread_mutex_t manager_lock; // Protects the linked list itself
} SentenceLockManager;

// Global pointers
extern SentenceLockManager* g_sentence_lock_manager; 

// Global SS info for replication notifications
extern int g_ss_id;
extern char g_nm_ip[32];
extern int g_nm_port;

// --- CHECKPOINT STRUCTURES ---
typedef struct CheckpointNode {
    char tag[64];                  // Checkpoint tag/name
    char* content;                 // Saved file content
    time_t timestamp;              // When checkpoint was created
    struct CheckpointNode* next;
} CheckpointNode;

typedef struct FileCheckpoints {
    char filename[256];
    CheckpointNode* checkpoints;   // Linked list of checkpoints
    pthread_mutex_t lock;          // Protect checkpoint list
    struct FileCheckpoints* next;
} FileCheckpoints;

// Global checkpoint manager
extern FileCheckpoints* g_checkpoints_head;
extern pthread_mutex_t g_checkpoints_lock;

// Function Prototypes
void logger(const char* format, ...); // Assuming log.h exists
char* read_file_to_string(const char *filename, long *out_size);
int find_sentence(const char *content, int sentence_num, int *start, int *end);
int find_word(const char *content, int sent_start, int sent_end, int word_index, int *word_start, int *word_end);

// Locking Functions
void init_sentence_locks();
int acquire_sentence_lock(const char* filename, int sentence_id);
void release_sentence_lock(const char* filename, int sentence_id);
int is_file_being_edited(const char* filename);

// Checkpoint Functions
void init_checkpoints();
int create_checkpoint(const char* filename, const char* tag);
char* get_checkpoint_content(const char* filename, const char* tag);
int revert_to_checkpoint(const char* filename, const char* tag);
char* list_checkpoints(const char* filename);
char* diff_checkpoints(const char* filename, const char* tag1, const char* tag2);

// We still need a way to get a physical mutex for the file to prevent data corruption
pthread_mutex_t* get_file_mutex(const char* filename);

#endif