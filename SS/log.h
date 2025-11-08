// log.h

#ifndef LOG_H
#define LOG_H

// This file only needs to declare the functions.
// It doesn't need all the other system headers.

/*
 * @brief Initializes the logging system.
 */
void logger_init(const char* log_filename);

/*
 * @brief Thread-safe logging function (replaces printf).
 */
void logger(const char* format, ...);

#endif // LOG_H