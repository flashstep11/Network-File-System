// log.c

#include "defs.h" // Includes all system headers
#include "log.h"  // Includes its own declarations
#include <stdarg.h>
// Make these variables STATIC so they are private to this file.
static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

void logger_init(const char* log_filename) {
    // 1. Initialize the mutex
    // (We can skip init since we used PTHREAD_MUTEX_INITIALIZER)

    // 2. Open the log file in "append" mode
    g_log_file = fopen(log_filename, "a");
    if (g_log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    // 3. Log the "start" message
    time_t now = time(NULL);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    
    logger("\n--- LOGGING STARTED: %s ---\n", time_str);
}

void logger(const char* format, ...) {
    // 1. Get the current timestamp
    char time_buffer[26];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_buffer, 26, "[%Y-%m-%d %H:%M:%S]", tm_info);

    // 2. Lock the mutex
    pthread_mutex_lock(&g_log_lock);

    // --- CRITICAL SECTION ---
    
    printf("%s ", time_buffer);        // 3. Print timestamp to console
    fprintf(g_log_file, "%s ", time_buffer); // 3. Print timestamp to log file

    va_list args;

    va_start(args, format);
    vprintf(format, args); // 5. Print message to console
    va_end(args);

    va_start(args, format);
    vfprintf(g_log_file, format, args); // 6. Print message to log file
    va_end(args);

    fflush(g_log_file); // 7. Flush to disk
    
    // --- END CRITICAL SECTION ---
    
    // 8. Release the lock
    pthread_mutex_unlock(&g_log_lock);
}