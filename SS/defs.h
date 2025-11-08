// defs.h

#ifndef DEFS_H
#define DEFS_H

// 1. Include all system headers here
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
#include <stdarg.h>
#include <errno.h> // Good to have for strerror()

// 2. Define Types
typedef struct FileLockNode {
    char* filename;
    pthread_mutex_t file_lock;
    struct FileLockNode* next;
} FileLockNode;

typedef struct {
    FileLockNode* head;
    pthread_mutex_t list_lock;
} LockManager;

// 3. Define Constants
#define NM_IP "127.0.0.1"
#define NM_PORT 8080
#define BUFFER_SIZE 2048 // I saw you changed this, good idea
#define MAX_CLIENTS 10

// 4. Declare Global Variables
// 'extern' says "this variable exists, but is *defined* in a .c file"
extern LockManager* g_lock_manager;
char* read_file_to_string(const char *filename, long *out_size);
int find_sentence(const char *content, int sentence_num, int *start, int *end);
int find_word(const char *content, int sent_start, int sent_end, int word_index, 
              int *word_start, int *word_end);
void lock_manager_init(void);
pthread_mutex_t* get_lock_for_file(const char *filename);
#endif // DEFS_H