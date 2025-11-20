
#ifndef LOG_H
#define LOG_H

void logger_init(const char* log_filename);
void logger(const char* format, ...);

#endif // LOG_H