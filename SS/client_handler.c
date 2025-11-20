#include "defs.h"
#include "log.h"
#include "client_handler.h"

// Async notification to NM when file is written (triggers replication to backup SS)
void notify_nm_write(const char* filename) {
    if (g_ss_id < 0) {
        logger("[REPLICATE] SS_ID not set, skipping replication notification\n");
        return;
    }
    
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        logger("[REPLICATE] Failed to create socket to NM for replication\n");
        return;
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(g_nm_port);
    inet_pton(AF_INET, g_nm_ip, &nm_addr.sin_addr);
    
    if (connect(nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        logger("[REPLICATE] Failed to connect to NM for replication\n");
        close(nm_sock);
        return;
    }
    
    char notify_msg[512];
    snprintf(notify_msg, sizeof(notify_msg), "NOTIFY_WRITE %d %s\n", g_ss_id, filename);
    write(nm_sock, notify_msg, strlen(notify_msg));
    close(nm_sock);
    
    logger("[REPLICATE] Notified NM about write to %s\n", filename);
}

int check_permissions_by_username(const char *username, const char *filename, const char *mode) {
    logger("[AccessControl] Checking %s permission for file %s (user: %s)\n", mode, filename, username);

    // 2. Connect to NM
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        logger("[AccessControl] Failed to create socket to NM\n");
        return 0;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, g_nm_ip, &nm_addr.sin_addr) <= 0) {
        logger("[AccessControl] Invalid NM IP: %s\n", g_nm_ip);
        close(nm_sock);
        return 0;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        logger("[AccessControl] Could not reach NM to verify permissions.\n");
        close(nm_sock);
        return 0; // If NM is down, deny access for security
    }

    logger("[AccessControl] Connected to NM\n");

    // 3. Send Query: CHECK_ACCESS_USER <USERNAME> <FILENAME> <MODE>
    char query[512];
    sprintf(query, "CHECK_ACCESS_USER %s %s %s\n", username, filename, mode);
    write(nm_sock, query, strlen(query));
    logger("[AccessControl] Sent query: %s", query);

    // 4. Read Response
    char response[64] = {0};
    ssize_t n = read(nm_sock, response, 63);
    logger("[AccessControl] Received response (%zd bytes): %s\n", n, response);
    close(nm_sock);

    if (strncmp(response, "ACK:YES", 7) == 0) {
        logger("[AccessControl] Access GRANTED\n");
        return 1; // Allowed
    }

    logger("[AccessControl] Denied access to user %s for %s on file %s\n", username, mode, filename);
    return 0; // Denied
}

void handle_read_command(int client_socket, char* buffer, const char* unused_username) {
    (void)unused_username; // Mark as intentionally unused
    char username[64];
    char filename[256];

    // 1. --- Parse the command ---
    // Command format: READ <username> <filename>\n
    if (sscanf(buffer, "READ %63s %255s", username, filename) != 2) {
        char *err = "ERR:400:BAD_READ_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return;
    }

    logger("[SS-Thread] Read request for: %s (user: %s)\n", filename, username);

    // 2. Check permissions
    if (!check_permissions_by_username(username, filename, "READ")) {
        char *err = "ERR:403:ACCESS_DENIED\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 3. --- Read the file ---
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long file_size = 0;
    char *file_content = read_file_to_string(full_path, &file_size);

    if (file_content != NULL) {
        // --- Success ---
        // Found the file, send all of its content.
        write(client_socket, file_content, file_size);
        
        // Send EOF marker so client knows to stop reading
        write(client_socket, "\nEOF\n", 5); 

        // Clean up
        free(file_content);
    } else {
        // --- Failure ---
        // read_file_to_string returned NULL (e.g., file not found).
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
    }
}
// WRITE: Interactive word-by-word editing with sentence-level locking
// Key feature: Only locks ONE sentence at a time (30 bonus points requirement)
// Validates sentence exists/can be created BEFORE acquiring lock
void handle_write_command(int client_socket, char* buffer, const char* unused_username) {
    (void)unused_username;
    char username[64];
    char filename[256];
    int sentence_num;
    
    if (sscanf(buffer, "WRITE %63s %255s %d", username, filename, &sentence_num) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: WRITE <username> <filename> <sentence_num>)\n", 73);
        return;
    }
    
    logger("[SS-Thread] WRITE request: %s sentence %d (user: %s)\n", filename, sentence_num, username);

    // Check permissions with NM
    if (!check_permissions_by_username(username, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        logger("[SS-Thread] WRITE denied - no permission\n");
        return;
    }

    // Pre-validate sentence exists BEFORE acquiring lock (prevents lock waste)
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long fsize = 0;
    char* temp_data = read_file_to_string(full_path, &fsize);
    
    if (!temp_data) {
        temp_data = strdup(""); // Empty file
        if (!temp_data) {
            write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
            return;
        }
    }
    
    // Check if sentence exists (allow sentence 0 in empty file, or next sentence after last)
    int s_start = 0, s_end = 0;
    int sentence_exists = find_sentence(temp_data, sentence_num, &s_start, &s_end);
    
    if (!sentence_exists) {
        // Sentence doesn't exist - check if it's valid to create
        
        // Count how many sentences exist
        int sentence_count = 0;
        int idx = 0;
        while (find_sentence(temp_data, idx, &s_start, &s_end)) {
            sentence_count++;
            idx++;
        }
        
        // Allow creating sentence 0 in empty file, or the next consecutive sentence
        if (sentence_num != 0 && sentence_num != sentence_count) {
            write(client_socket, "ERROR: Sentence index out of range.\n", 37);
            logger("[SS-Thread] Sentence %d not found in %s (file has %d sentences, can only create sentence %d)\n", 
                   sentence_num, filename, sentence_count, sentence_count);
            free(temp_data);
            return;
        }
        
        // If creating a new sentence (not sentence 0), check that last sentence ends with delimiter
        if (sentence_num > 0) {
            // Find the last sentence
            int last_s_start = 0, last_s_end = 0;
            find_sentence(temp_data, sentence_count - 1, &last_s_start, &last_s_end);
            
            // Check if last sentence ends with a delimiter (., !, ?)
            if (last_s_end >= 0 && 
                temp_data[last_s_end] != '.' && 
                temp_data[last_s_end] != '!' && 
                temp_data[last_s_end] != '?') {
                write(client_socket, "ERROR: Cannot create new sentence. Previous sentence must end with a delimiter (., !, ?).\n", 90);
                logger("[SS-Thread] Cannot create sentence %d - sentence %d doesn't end with delimiter\n", 
                       sentence_num, sentence_count - 1);
                free(temp_data);
                return;
            }
        }
        
        logger("[SS-Thread] Will create new sentence %d\n", sentence_num);
    }
    
    free(temp_data);
    logger("[SS-Thread] Sentence validation passed for sentence %d\n", sentence_num);

    // Acquire sentence lock - only this sentence is locked, others remain accessible
    if (!acquire_sentence_lock(filename, sentence_num)) {
        char *err = "ERR:423:SENTENCE_LOCKED_BY_ANOTHER_USER\n";
        write(client_socket, err, strlen(err));
        logger("[SS-Thread] WRITE denied - sentence locked\n");
        return;
    }
    
    logger("[SS-Thread] Sentence lock acquired for %s sentence %d\n", filename, sentence_num);
    
    // Send ACK to start interactive editing
    const char* ack_msg = "ACK:SENTENCE_LOCKED Enter word updates (word_index content) or ETIRW to finish\n";
    write(client_socket, ack_msg, strlen(ack_msg));

    // Load file - file mutex only for reading, sentence lock for writing
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    if (!f_mutex) {
        write(client_socket, "ERR:500:INTERNAL_ERROR\n", 23);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // Lock file mutex ONLY for reading
    pthread_mutex_lock(f_mutex);
    char* file_data = read_file_to_string(full_path, &fsize);
    
    logger("[SS-Thread] Loaded file %s, size=%ld, data=%p\n", filename, fsize, (void*)file_data);
    
    if (!file_data) {
        file_data = strdup(""); // Empty file
        if (!file_data) {
            write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            return;
        }
        logger("[SS-Thread] File is empty, created empty buffer\n");
    }
    
    // Create backup for UNDO
    char bak_name[512];
    snprintf(bak_name, sizeof(bak_name), "%s%s.bak", STORAGE_ROOT, filename);
    FILE *bak = fopen(bak_name, "w");
    if (bak) { 
        fprintf(bak, "%s", file_data); 
        fclose(bak); 
        logger("[SS-Thread] Backup created: %s\n", bak_name);
    }
    
    // Make a working copy we'll modify
    char* working_data = strdup(file_data);
    if (!working_data) {
        write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
        free(file_data);
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // CRITICAL: Save original sentence count for validation
    int original_sentence_count = 0;
    int tmp_s = 0, tmp_e = 0;
    while (find_sentence(file_data, original_sentence_count, &tmp_s, &tmp_e)) {
        original_sentence_count++;
    }
    logger("[SS-Thread] Original sentence count: %d\n", original_sentence_count);
    
    // UNLOCK file mutex - we're done reading, now edit in memory
    pthread_mutex_unlock(f_mutex);
    free(file_data); // Don't need original anymore
    
    // 5. Interactive editing loop
    char edit_buffer[1024];
    while (1) {
        memset(edit_buffer, 0, sizeof(edit_buffer));
        
        // Set socket to non-blocking with timeout so we can periodically check validity
        struct timeval tv;
        tv.tv_sec = 1;  // Check every 1 second
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t n = read(client_socket, edit_buffer, sizeof(edit_buffer) - 1);
        
        // If timeout (no data), check if sentence structure changed
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Timeout - check validity
            pthread_mutex_lock(f_mutex);
            long check_size;
            char* current_file_data = read_file_to_string(full_path, &check_size);
            pthread_mutex_unlock(f_mutex);
            
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }
                
                // If file sentence count differs from ORIGINAL, someone else changed structure!
                if (file_sentence_count != original_sentence_count) {
                    free(current_file_data);
                    
                    const char* msg = "\n⚠️  ERROR: File structure changed by another user.\nSentence boundaries have shifted. Disconnecting...\n";
                    write(client_socket, msg, strlen(msg));
                    logger("[SS-Thread] CONCURRENT EDIT DETECTED: File sentence count changed from %d to %d\n", 
                           original_sentence_count, file_sentence_count);
                    
                    // Abort this edit session
                    free(working_data);
                    release_sentence_lock(filename, sentence_num);
                    close(client_socket);
                    pthread_exit(NULL);
                }
                
                free(current_file_data);
            }
            
            // No data yet, loop again
            continue;
        }
        
        if (n <= 0) {
            logger("[SS-Thread] Client disconnected during WRITE\n");
            break;
        }
        
        edit_buffer[n] = '\0';
        // Remove trailing newline
        char* nl = strchr(edit_buffer, '\n');
        if (nl) *nl = '\0';
        
        // Check for ETIRW (end editing)
        if (strncmp(edit_buffer, "ETIRW", 5) == 0) {
            logger("[SS-Thread] ETIRW received, finalizing write\n");
            
            // CRITICAL: Before writing, validate our working_data is still valid!
            // Another user might have changed sentence structure while we were editing
            pthread_mutex_lock(f_mutex);
            
            // Read current file state
            long check_size;
            char* current_file_data = read_file_to_string(full_path, &check_size);
            
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }
                
          // If file sentence count differs from ORIGINAL, someone else changed structure!
          if (file_sentence_count != original_sentence_count) {
              // Improved logging for debugging: record both counts before aborting
              logger("[SS-Thread] ETIRW VALIDATION: original=%d, current_file=%d, filename=%s, sentence=%d\n",
                  original_sentence_count, file_sentence_count, filename, sentence_num);

              free(current_file_data);
              pthread_mutex_unlock(f_mutex);
                    
              const char* msg = "ERROR: Cannot save - another user changed the file structure (added/removed delimiters).\nYour edits are discarded.\n";
              write(client_socket, msg, strlen(msg));
              logger("[SS-Thread] ETIRW BLOCKED: File sentence count changed from %d to %d by another user\n", 
                  original_sentence_count, file_sentence_count);
                    
              // Abort without saving
              free(working_data);
              release_sentence_lock(filename, sentence_num);
              close(client_socket);
              pthread_exit(NULL);
          }
                
                free(current_file_data);
            }
            
            // Validation passed - safe to write
            FILE *fp = fopen(full_path, "w");
            if (fp) {
                int write_result = fprintf(fp, "%s", working_data);
                int close_result = fclose(fp);
                
                if (write_result < 0 || close_result != 0) {
                    pthread_mutex_unlock(f_mutex);
                    logger("[SS-Thread] Failed to write file %s: fprintf=%d, fclose=%d, errno=%d (%s)\n", 
                           filename, write_result, close_result, errno, strerror(errno));
                    write(client_socket, "ERR:500:WRITE_FAILED Could not save file\n", 42);
                } else {
                    // CRITICAL: Check if sentence structure changed
                    // If so, invalidate all other sentence locks on this file
                    extern int check_sentence_structure_changed(const char*, const char*);
                    extern void release_all_sentence_locks_for_file(const char*);
                    
                    if (check_sentence_structure_changed(filename, working_data)) {
                        logger("[SS-Thread] Sentence structure changed in %s, invalidating other locks\n", filename);
                        release_all_sentence_locks_for_file(filename);
                    }
                    
                    pthread_mutex_unlock(f_mutex);
                    
                    write(client_socket, "ACK:WRITE_COMPLETE All changes saved\n", 38);
                    logger("[SS-Thread] File %s updated successfully (wrote %d bytes)\n", filename, write_result);
                    
                    // Notify NM for replication (async)
                    notify_nm_write(filename);
                }
            } else {
                pthread_mutex_unlock(f_mutex);
                logger("[SS-Thread] Failed to open file %s for writing: errno=%d (%s)\n", 
                       filename, errno, strerror(errno));
                write(client_socket, "ERR:500:WRITE_FAILED Could not save file\n", 42);
            }
            break;
        }
        
        // Parse word update: <word_index> <content>
        // word_index is 0-indexed (word 0, word 1, etc.)
        int word_index;
        char content[900];
        if (sscanf(edit_buffer, "%d %899[^\n]", &word_index, content) == 2) {
            logger("[SS-Thread] Parse OK: word_index=%d, content='%s', working_data='%s'\n", 
                   word_index, content, working_data);
            
            // Convert \n to actual newlines in content FIRST
            char processed_content[900];
            int src = 0, dst = 0;
            while (content[src] != '\0' && dst < 899) {
                if (content[src] == '\\' && content[src + 1] == 'n') {
                    processed_content[dst++] = '\n';
                    src += 2;
                } else {
                    processed_content[dst++] = content[src++];
                }
            }
            processed_content[dst] = '\0';
            
            // Split content by spaces to get individual words
            // NOTE: We keep newlines WITHIN words, they won't be split
            char* words[100]; // Max 100 words per command
            int word_count = 0;
            char* content_copy = strdup(processed_content);
            
            // Manual tokenization to preserve newlines within words
            char* ptr = content_copy;
            char* word_start = ptr;
            while (*ptr != '\0' && word_count < 100) {
                if (*ptr == ' ') {
                    if (ptr > word_start) {
                        *ptr = '\0';
                        words[word_count++] = strdup(word_start);
                    }
                    ptr++;
                    word_start = ptr;
                } else {
                    ptr++;
                }
            }
            // Last word
            if (ptr > word_start && word_count < 100) {
                words[word_count++] = strdup(word_start);
            }
            
            free(content_copy);
            
            logger("[SS-Thread] Split into %d words\n", word_count);
            
            // Check if any word contains a delimiter (., !, ?) - this creates sentence boundaries
            int delimiter_found = -1; // Index of word containing delimiter
            char* before_delimiter = NULL; // Part before delimiter (including delimiter)
            char* after_delimiter = NULL; // Part after delimiter
            
            for (int i = 0; i < word_count; i++) {
                // Check for all three sentence delimiters
                char* delim_pos = NULL;
                char* period_pos = strchr(words[i], '.');
                char* exclaim_pos = strchr(words[i], '!');
                char* question_pos = strchr(words[i], '?');
                
                // Find the first delimiter (if any)
                delim_pos = period_pos;
                if (exclaim_pos && (!delim_pos || exclaim_pos < delim_pos)) delim_pos = exclaim_pos;
                if (question_pos && (!delim_pos || question_pos < delim_pos)) delim_pos = question_pos;
                
                if (delim_pos) {
                    delimiter_found = i;
                    // Split the word at the delimiter
                    int before_len = (delim_pos - words[i]) + 1; // Include the delimiter
                    before_delimiter = (char*)malloc(before_len + 1);
                    strncpy(before_delimiter, words[i], before_len);
                    before_delimiter[before_len] = '\0';
                    
                    // Check if there's content after the delimiter in the same word
                    if (*(delim_pos + 1) != '\0') {
                        after_delimiter = strdup(delim_pos + 1);
                    }
                    break;
                }
            }
            
            // Special case: If inserting ONLY a standalone delimiter at beginning of sentence
            // This creates an edge case where the sentence gets malformed
            // Solution: Treat it as a regular word insertion, let the delimiter naturally end the sentence
            int is_standalone_delimiter_at_start = (word_count == 1 && delimiter_found == 0 && 
                                                     (strcmp(words[0], ".") == 0 || 
                                                      strcmp(words[0], "!") == 0 || 
                                                      strcmp(words[0], "?") == 0) && 
                                                     word_index == 0);
            
            logger("[SS-Thread] Delimiter analysis: delimiter_found=%d, before='%s', after='%s', standalone_at_start=%d\n",
                   delimiter_found, before_delimiter ? before_delimiter : "NULL", after_delimiter ? after_delimiter : "NULL",
                   is_standalone_delimiter_at_start);
            
            // Handle empty file or append operation
            size_t current_len = strlen(working_data);
            
            // CRITICAL: Re-validate sentence still exists at this index
            // (Another concurrent write might have added delimiters and shifted indices)
            pthread_mutex_lock(f_mutex);
            long check_size;
            char* current_file_data = read_file_to_string(full_path, &check_size);
            pthread_mutex_unlock(f_mutex);
            
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }

                logger("[SS-Thread] Per-update VALIDATION: file_sentence_count=%d, original=%d, editing sentence=%d\n",
                       file_sentence_count, original_sentence_count, sentence_num);

                // If file sentence count differs from ORIGINAL, someone else changed structure!
                if (file_sentence_count != original_sentence_count) {
                    free(current_file_data);

                    write(client_socket, "ERROR: Sentence structure changed due to concurrent edit. Your edits are discarded.\n", 80);
                    logger("[SS-Thread] CONCURRENT EDIT CONFLICT: Detected during per-update (sentence %d) file_count %d != original %d\n",
                           sentence_num, file_sentence_count, original_sentence_count);

                    // Free word tokens
                    for (int i = 0; i < word_count; i++) {
                        free(words[i]);
                    }
                    if (before_delimiter) free(before_delimiter);
                    if (after_delimiter) free(after_delimiter);

                    // Abort this edit session
                    free(working_data);
                    release_sentence_lock(filename, sentence_num);
                    close(client_socket);
                    pthread_exit(NULL);
                }

                free(current_file_data);
            }
            
            // Try to find the sentence first
            int s_start = 0, s_end = 0;
            int sentence_found = find_sentence(working_data, sentence_num, &s_start, &s_end);
            
            logger("[SS-Thread] find_sentence returned %d, s_start=%d, s_end=%d\n", 
                   sentence_found, s_start, s_end);
            
            if (!sentence_found) {
                // Sentence doesn't exist yet - we're creating a new sentence
                logger("[SS-Thread] Creating new sentence %d\n", sentence_num);
                
                // Validate word_index for new sentence creation
                // For a new/empty sentence, only word_index 0 is valid
                if (word_index != 0) {
                    write(client_socket, "ERROR: Word index out of range.\n", 33);
                    logger("[SS-Thread] Invalid word_index %d for new sentence (only 0 allowed)\n", word_index);
                    
                    // Free word tokens
                    for (int i = 0; i < word_count; i++) {
                        free(words[i]);
                    }
                    if (before_delimiter) free(before_delimiter);
                    if (after_delimiter) free(after_delimiter);
                    continue;
                }
                
                char* new_data;
                if (current_len == 0) {
                    // Empty file - just the words
                    int total_len = 0;
                    for (int i = 0; i < word_count; i++) {
                        total_len += strlen(words[i]) + 1; // +1 for space
                    }
                    new_data = (char*)malloc(total_len + 1);
                    if (new_data) {
                        new_data[0] = '\0';
                        for (int i = 0; i < word_count; i++) {
                            if (i > 0) strcat(new_data, " ");
                            strcat(new_data, words[i]);
                        }
                    }
                } else {
                    // File has content - append new sentence with space separator
                    // (Previous sentence already ends with delimiter, validated earlier)
                    int total_len = current_len + 1; // +1 for space
                    for (int i = 0; i < word_count; i++) {
                        total_len += strlen(words[i]) + 1; // +1 for space
                    }
                    
                    new_data = (char*)malloc(total_len + 1);
                    if (new_data) {
                        strcpy(new_data, working_data);
                        strcat(new_data, " ");
                        for (int i = 0; i < word_count; i++) {
                            if (i > 0) strcat(new_data, " ");
                            strcat(new_data, words[i]);
                        }
                    }
                }
                
                if (new_data) {
                    free(working_data);
                    working_data = new_data;
                    write(client_socket, "ACK:WORD_UPDATED\n", 17);
                    logger("[SS-Thread] Added %d words to new sentence %d\n", word_count, sentence_num);
                } else {
                    write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                }
                
                // Free word tokens
                for (int i = 0; i < word_count; i++) {
                    free(words[i]);
                }
                if (before_delimiter) free(before_delimiter);
                if (after_delimiter) free(after_delimiter);
                continue;
            }
            
            // Sentence exists, now INSERT words at the specified position
            // Insert all words from the command at consecutive positions
            {
                // Recalculate sentence boundaries after each insertion (working_data changes!)
                if (!find_sentence(working_data, sentence_num, &s_start, &s_end)) {
                    // If sentence doesn't exist in working_data, it means we're creating it
                    s_start = strlen(working_data);
                    s_end = s_start - 1; // Empty sentence
                }
                
                // Count existing words in the sentence EACH iteration (critical for validation)
                // We must recount because working_data changes after each insertion
                int existing_word_count = 0;
                int pos = s_start;
                while (pos <= s_end) {
                    // Skip spaces
                    while (pos <= s_end && working_data[pos] == ' ') pos++;
                    if (pos > s_end) break;
                    // Check if this is a sentence-ending punctuation at the end
                    if ((working_data[pos] == '.' || working_data[pos] == '!' || working_data[pos] == '?') &&
                        (pos + 1 > s_end || working_data[pos + 1] == ' ')) {
                        // This is a standalone delimiter at the end, don't count it as a word
                        break;
                    }
                    existing_word_count++;
                    // Skip to next space
                    while (pos <= s_end && working_data[pos] != ' ') pos++;
                }
                
              printf("[SS-Thread] Sentence %d has %d existing words\n", sentence_num, existing_word_count);  
                // Validate word_index (0-indexed: valid range is 0 to existing_word_count)
                if (word_index < 0 || word_index > existing_word_count) {
                    write(client_socket, "ERROR: Word index out of range.\n", 33);
                    logger("[SS-Thread] Invalid word_index %d (sentence has %d words, valid range: 0-%d)\n", 
                           word_index, existing_word_count, existing_word_count);
                    
                    // Free word tokens
                    for (int i = 0; i < word_count; i++) {
                        free(words[i]);
                    }
                    if (before_delimiter) free(before_delimiter);
                    if (after_delimiter) free(after_delimiter);
                    continue;
                }
                
                // Find insertion position in the string (word_index is 0-indexed)
                int insert_pos;
                if (word_index == existing_word_count) {
                    // Append to end of sentence - but check if sentence ends with period
                    if (working_data[s_end] == '.' || working_data[s_end] == '!' || working_data[s_end] == '?') {
                        // Insert before the period
                        insert_pos = s_end;
                    } else {
                        // No period, insert after sentence end
                        insert_pos = s_end + 1;
                    }
                } else if (word_index == 0) {
                    // At beginning of sentence (word 0)
                    insert_pos = s_start;
                } else {
                    // At word_index position (0-indexed)
                    int curr_word = 0; // Start from word 0
                    pos = s_start;
                    while (curr_word < word_index && pos <= s_end) {
                        // Skip spaces
                        while (pos <= s_end && working_data[pos] == ' ') pos++;
                        if (curr_word == word_index) break;
                        // Skip word
                        while (pos <= s_end && working_data[pos] != ' ') pos++;
                        curr_word++;
                    }
                    insert_pos = pos;
                }
                
                size_t working_len = strlen(working_data);
                char* new_data = NULL;
                
                // Check if we need to handle period delimiter (sentence splitting)
                if (delimiter_found >= 0) {
                    // We're creating a sentence boundary by INSERTING words with period
                    // Build new content in parts:
                    // 1. Everything before insertion point (before insert_pos)
                    // 2. Words 0 to delimiter_found (with before_delimiter as the last word before period)
                    // 3. " " (single space after period to start new sentence)
                    // 4. after_delimiter (if any) and remaining words (delimiter_found+1 onwards) - new sentence
                    // 5. Rest of original sentence content (from insert_pos to s_end) - continues in new sentence
                    // 6. Rest of file (after s_end)
                    
                    // Calculate size
                    int size_needed = working_len + 100; // Extra buffer
                    for (int i = 0; i < word_count; i++) {
                        size_needed += strlen(words[i]) + 1;
                    }
                    if (before_delimiter) size_needed += strlen(before_delimiter);
                    if (after_delimiter) size_needed += strlen(after_delimiter);
                    
                    new_data = (char*)malloc(size_needed);
                    if (!new_data) {
                        write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                        for (int i = 0; i < word_count; i++) free(words[i]);
                        if (before_delimiter) free(before_delimiter);
                        if (after_delimiter) free(after_delimiter);
                        continue;
                    }
                    
                    int write_pos = 0;
                    
                    // 0. CRITICAL: Copy everything BEFORE the sentence being edited
                    if (s_start > 0) {
                        memcpy(new_data, working_data, s_start);
                        write_pos = s_start;
                    }
                    
                    // 1. Copy everything before insertion point (within the sentence)
                    if (insert_pos > s_start) {
                        memcpy(new_data + write_pos, working_data + s_start, insert_pos - s_start);
                        write_pos += (insert_pos - s_start);
                        // Remove trailing spaces
                        while (write_pos > 0 && new_data[write_pos - 1] == ' ') write_pos--;
                    }
                    
                    // 2. Add words before and including period (0 to delimiter_found)
                    for (int i = 0; i <= delimiter_found; i++) {
                        if (write_pos > 0) {
                            new_data[write_pos++] = ' ';
                        }
                        
                        if (i == delimiter_found) {
                            // Use before_delimiter (includes the period)
                            int len = strlen(before_delimiter);
                            memcpy(new_data + write_pos, before_delimiter, len);
                            write_pos += len;
                        } else {
                            int word_len = strlen(words[i]);
                            memcpy(new_data + write_pos, words[i], word_len);
                            write_pos += word_len;
                        }
                    }
                    
                    // 3. Add space to separate sentences
                    new_data[write_pos++] = ' ';
                    
                    // 4. Start new sentence with after_delimiter and remaining words
                    int new_sentence_start = write_pos;
                    if (after_delimiter) {
                        int len = strlen(after_delimiter);
                        memcpy(new_data + write_pos, after_delimiter, len);
                        write_pos += len;
                    }
                    
                    for (int i = delimiter_found + 1; i < word_count; i++) {
                        if (write_pos > new_sentence_start) {
                            new_data[write_pos++] = ' ';
                        }
                        int word_len = strlen(words[i]);
                        memcpy(new_data + write_pos, words[i], word_len);
                        write_pos += word_len;
                    }
                    
                    // 5. Add rest of original sentence (from insert_pos to s_end) to new sentence
                    if (insert_pos <= s_end) {
                        // Skip leading spaces at insert_pos
                        int copy_from = insert_pos;
                        while (copy_from <= s_end && working_data[copy_from] == ' ') copy_from++;
                        
                        if (copy_from <= s_end) {
                            if (write_pos > new_sentence_start) {
                                new_data[write_pos++] = ' ';
                            }
                            int rest_len = s_end - copy_from + 1;
                            memcpy(new_data + write_pos, working_data + copy_from, rest_len);
                            write_pos += rest_len;
                        }
                    }
                    
                    // 6. Copy rest of file after original sentence
                    if (s_end + 1 < (int)working_len) {
                        strcpy(new_data + write_pos, working_data + s_end + 1);
                    } else {
                        new_data[write_pos] = '\0';
                    }
                    
                    logger("[SS-Thread] Inserted word %d with period delimiter, split sentence %d\n", word_index, sentence_num);
                    
                } else {
                    // No period delimiter - regular insertion
                    // Calculate total length needed
                    int total_insert_len = 0;
                    for (int i = 0; i < word_count; i++) {
                        total_insert_len += strlen(words[i]);
                        if (i > 0 || insert_pos > s_start) total_insert_len++; // space before
                    }
                    if (insert_pos < (int)working_len && working_data[insert_pos] != ' ' && word_count > 0) {
                        total_insert_len++; // space after
                    }
                    
                    int new_total = working_len + total_insert_len + 2;
                    new_data = (char*)malloc(new_total);
                    
                    if (new_data) {
                        // Copy everything before insertion point
                        memcpy(new_data, working_data, insert_pos);
                        int write_pos = insert_pos;
                        
                        // Insert all words
                        for (int i = 0; i < word_count; i++) {
                            // Add space before if needed
                            if (write_pos > s_start && (i > 0 || (insert_pos > s_start && working_data[insert_pos - 1] != ' '))) {
                                new_data[write_pos++] = ' ';
                            }
                            // Add the word
                            int word_len = strlen(words[i]);
                            memcpy(new_data + write_pos, words[i], word_len);
                            write_pos += word_len;
                        }
                        
                        // Add space after if needed
                        if (insert_pos < (int)working_len && working_data[insert_pos] != ' ' && word_count > 0) {
                            new_data[write_pos++] = ' ';
                        }
                        
                        // Copy rest of file
                        if (insert_pos < (int)working_len) {
                            strcpy(new_data + write_pos, working_data + insert_pos);
                        } else {
                            new_data[write_pos] = '\0';
                        }
                    }
                }
                
                if (new_data) {
                    free(working_data);
                    working_data = new_data;
                    write(client_socket, "ACK:WORD_UPDATED\n", 17);
                    logger("[SS-Thread] Inserted %d words at index %d in sentence %d\n", word_count, word_index, sentence_num);
                } else {
                    write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                }
                
                // Free word tokens
                for (int i = 0; i < word_count; i++) {
                    free(words[i]);
                }
                if (before_delimiter) free(before_delimiter);
                if (after_delimiter) free(after_delimiter);
            }
        } else {
            write(client_socket, "ERR:400:BAD_FORMAT Use: word_index content\n", 44);
        }
    }
    
    // Cleanup
    if (working_data) free(working_data);
    
    // Release sentence lock
    release_sentence_lock(filename, sentence_num);
    logger("[SS-Thread] Sentence lock released for %s sentence %d\n", filename, sentence_num);
}

void handle_stream_command(int client_socket, char* buffer, const char* unused_username) {
    (void)unused_username;
    char username[64];
    char filename[256];
    
    // 1. --- Parse the command ---
    // Command format: STREAM <username> <filename>\n
    if (sscanf(buffer, "STREAM %63s %255s", username, filename) != 2) {
        char *err = "ERR:400:BAD_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    // Check permissions
    if (!check_permissions_by_username(username, filename, "READ")) {
        char *err = "ERR:403:ACCESS_DENIED\n";
        write(client_socket, err, strlen(err));
        return;
    }
    
    logger("[SS-Thread] Stream request for: %s\n", filename);

    // 2. --- Read the entire file into memory ---
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long file_size = 0;
    char *file_content = read_file_to_string(full_path, &file_size);

    if (file_content == NULL) {
        // --- Error Handling: File Not Found ---
        char *err = "ERR:404:FILE_NOT_FOUND\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 3. --- Stream the file, preserving formatting ---
    const char *delims = " \t\n\r";
    char *ptr = file_content; // Our "cursor" walking through the file

    while (*ptr != '\0') {
        int bytes_sent;

        // --- a. Find a "word" chunk (non-delimiters) ---
        int word_len = strcspn(ptr, delims);

        if (word_len > 0) {
            bytes_sent = write(client_socket, ptr, word_len);
            if (bytes_sent < 0) {
                perror("[SS-Thread] Client disconnected mid-stream");
                break; // Exit loop
            }
            usleep(100000); // 0.1 second delay
            ptr += word_len; // Move cursor past the word
        }

        // --- b. Find a "delimiter" chunk (whitespace/newlines) ---
        int delim_len = strspn(ptr, delims);

        if (delim_len > 0) {
            bytes_sent = write(client_socket, ptr, delim_len);
            if (bytes_sent < 0) {
                perror("[SS-Thread] Client disconnected mid-stream");
                break; // Exit loop
            }
            ptr += delim_len; // Move cursor past the whitespace
        }
        
        // Safety check: if neither word_len nor delim_len advanced, break to prevent infinite loop
        if (word_len == 0 && delim_len == 0) {
            logger("[SS-Thread] WARNING: Unexpected character in stream, advancing by 1\n");
            ptr++; // Force advance to prevent infinite loop
        }
    }
    
    // 4. --- Send the "STOP" packet ---
    char *stop_packet = "\nSTREAM_END\n";
    write(client_socket, stop_packet, strlen(stop_packet));
    
    // 5. --- Clean up ---
    free(file_content);
}

// ============= CHECKPOINT HANDLERS =============

void handle_checkpoint_command(int client_socket, char* buffer) {
    char username[64], filename[256], tag[64];
    
    if (sscanf(buffer, "CHECKPOINT %63s %255s %63s", username, filename, tag) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: CHECKPOINT <username> <filename> <tag>)\n", 69);
        return;
    }
    
    logger("[SS-Thread] CHECKPOINT request: %s tag=%s (user: %s)\n", filename, tag, username);
    
    // Check permissions
    if (!check_permissions_by_username(username, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }
    
    // Check if file is being edited
    if (is_file_being_edited(filename)) {
        write(client_socket, "ERR:423:CANNOT_CHECKPOINT_FILE_IS_BEING_EDITED\n", 48);
        logger("[SS-Thread] CHECKPOINT blocked - %s has active sentence locks\n", filename);
        return;
    }
    
    if (create_checkpoint(filename, tag)) {
        char response[128];
        snprintf(response, sizeof(response), "ACK:CHECKPOINT_CREATED Checkpoint '%s' created successfully\n", tag);
        write(client_socket, response, strlen(response));
    } else {
        write(client_socket, "ERR:500:CHECKPOINT_FAILED\n", 26);
    }
}

void handle_viewcheckpoint_command(int client_socket, char* buffer) {
    char username[64], filename[256], tag[64];
    
    if (sscanf(buffer, "VIEWCHECKPOINT %63s %255s %63s", username, filename, tag) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: VIEWCHECKPOINT <username> <filename> <tag>)\n", 73);
        return;
    }
    
    logger("[SS-Thread] VIEWCHECKPOINT request: %s tag=%s (user: %s)\n", filename, tag, username);
    
    // Check permissions
    if (!check_permissions_by_username(username, filename, "READ")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }
    
    char* content = get_checkpoint_content(filename, tag);
    if (content) {
        write(client_socket, content, strlen(content));
        write(client_socket, "\nEOF\n", 5);
        free(content);
    } else {
        write(client_socket, "ERR:404:CHECKPOINT_NOT_FOUND\n", 29);
    }
}

void handle_revert_command(int client_socket, char* buffer) {
    char username[64], filename[256], tag[64];
    
    if (sscanf(buffer, "REVERT %63s %255s %63s", username, filename, tag) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: REVERT <username> <filename> <tag>)\n", 65);
        return;
    }
    
    logger("[SS-Thread] REVERT request: %s tag=%s (user: %s)\n", filename, tag, username);
    
    // Check permissions
    if (!check_permissions_by_username(username, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }
    
    // Check if file is being edited
    if (is_file_being_edited(filename)) {
        write(client_socket, "ERR:423:CANNOT_REVERT_FILE_IS_BEING_EDITED\n", 44);
        logger("[SS-Thread] REVERT blocked - %s has active sentence locks\n", filename);
        return;
    }
    
    if (revert_to_checkpoint(filename, tag)) {
        char response[128];
        snprintf(response, sizeof(response), "ACK:REVERT_SUCCESS File reverted to checkpoint '%s'\n", tag);
        write(client_socket, response, strlen(response));
    } else {
        write(client_socket, "ERR:404:CHECKPOINT_NOT_FOUND\n", 29);
    }
}

void handle_listcheckpoints_command(int client_socket, char* buffer) {
    char username[64], filename[256];
    
    if (sscanf(buffer, "LISTCHECKPOINTS %63s %255s", username, filename) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: LISTCHECKPOINTS <username> <filename>)\n", 68);
        return;
    }
    
    logger("[SS-Thread] LISTCHECKPOINTS request: %s (user: %s)\n", filename, username);
    
    // Check permissions
    if (!check_permissions_by_username(username, filename, "READ")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }
    
    char* list = list_checkpoints(filename);
    write(client_socket, list, strlen(list));
    free(list);
}

void handle_diff_command(int client_socket, char* buffer) {
    char filename[256], tag1[64], tag2[64];
    
    if (sscanf(buffer, "DIFF %255s %63s %63s", filename, tag1, tag2) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: DIFF <filename> <tag1> <tag2>)\n", 60);
        return;
    }
    
    logger("[SS-Thread] DIFF request: %s (%s vs %s)\n", filename, tag1, tag2);
    
    // NM already verified permissions before sending client here
    
    char* diff = diff_checkpoints(filename, tag1, tag2);
    write(client_socket, diff, strlen(diff));
    free(diff);
}

// ============= REPLACE COMMAND (BONUS) =============

void handle_replace_command(int client_socket, char* buffer) {
    char username[64];
    char filename[256];
    int sentence_num;
    
    // Parse: REPLACE <username> <filename> <sentence_num>
    if (sscanf(buffer, "REPLACE %63s %255s %d", username, filename, &sentence_num) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: REPLACE <username> <filename> <sentence_num>)\n", 75);
        return;
    }
    
    logger("[SS-Thread] REPLACE request: %s sentence %d (user: %s)\n", filename, sentence_num, username);

    // Check WRITE permissions (replacing requires write access)
    if (!check_permissions_by_username(username, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        logger("[SS-Thread] REPLACE denied - no permission\n");
        return;
    }

    // Load file and validate sentence exists
    char full_path[512];
    get_full_path(full_path, sizeof(full_path), filename);
    
    long fsize = 0;
    char* file_data = read_file_to_string(full_path, &fsize);
    
    if (!file_data) {
        write(client_socket, "ERR:404:FILE_NOT_FOUND\n", 23);
        return;
    }
    
    // Check if sentence exists
    int s_start = 0, s_end = 0;
    if (!find_sentence(file_data, sentence_num, &s_start, &s_end)) {
        write(client_socket, "ERROR: Sentence index out of range.\n", 37);
        logger("[SS-Thread] Sentence %d not found in %s\n", sentence_num, filename);
        free(file_data);
        return;
    }
    
    free(file_data); // We'll reload it after acquiring lock
    
    // Acquire sentence lock
    if (!acquire_sentence_lock(filename, sentence_num)) {
        write(client_socket, "ERR:423:SENTENCE_LOCKED_BY_ANOTHER_USER\n", 40);
        logger("[SS-Thread] REPLACE denied - sentence locked\n");
        return;
    }
    
    logger("[SS-Thread] Sentence lock acquired for %s sentence %d\n", filename, sentence_num);
    
    // Send ACK to client to start interactive editing
    const char* ack_msg = "ACK:SENTENCE_LOCKED Enter word updates (word_index content) or ECALPER to finish\n\n"
                          "=== INTERACTIVE REPLACE MODE ===\n"
                          "Format: <word_index> <content>\n"
                          "Example: 0 replacement (replaces 1st word)\n"
                          "         2 \"\" (deletes 3rd word)\n"
                          "Type 'ECALPER' to save and exit\n"
                          "Word indices are 0-based\n"
                          "==============================\n\n";
    write(client_socket, ack_msg, strlen(ack_msg));

    // Get file mutex and load file
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    if (!f_mutex) {
        write(client_socket, "ERR:500:INTERNAL_ERROR\n", 23);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // Lock ONLY for reading
    pthread_mutex_lock(f_mutex);
    file_data = read_file_to_string(full_path, &fsize);
    if (!file_data) {
        write(client_socket, "ERR:500:FILE_READ_ERROR\n", 24);
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // Create backup for UNDO
    char bak_name[512];
    snprintf(bak_name, sizeof(bak_name), "%s%s.bak", STORAGE_ROOT, filename);
    FILE *bak = fopen(bak_name, "w");
    if (bak) { 
        fprintf(bak, "%s", file_data); 
        fclose(bak); 
        logger("[SS-Thread] Backup created: %s\n", bak_name);
    }
    
    // Make a working copy
    char* working_data = strdup(file_data);
    
    // CRITICAL: Save original sentence count for validation
    int original_sentence_count = 0;
    int tmp_s = 0, tmp_e = 0;
    while (find_sentence(file_data, original_sentence_count, &tmp_s, &tmp_e)) {
        original_sentence_count++;
    }
    logger("[SS-Thread] Original sentence count (REPLACE): %d\n", original_sentence_count);
    
    free(file_data);
    
    // Unlock - we're done reading
    pthread_mutex_unlock(f_mutex);
    
    if (!working_data) {
        write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
        release_sentence_lock(filename, sentence_num);
        return;
    }
    
    // Interactive editing loop
    char edit_buffer[1024];
    int changes_made = 0;
    
    while (1) {
        memset(edit_buffer, 0, sizeof(edit_buffer));
        
        // Set socket to non-blocking with timeout so we can periodically check validity
        struct timeval tv;
        tv.tv_sec = 1;  // Check every 1 second
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t n = read(client_socket, edit_buffer, sizeof(edit_buffer) - 1);
        
        // If timeout (no data), check if sentence structure changed
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Timeout - check validity
            pthread_mutex_lock(f_mutex);
            long check_size;
            char* current_file_data = read_file_to_string(full_path, &check_size);
            pthread_mutex_unlock(f_mutex);
            
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }
                
                // If file sentence count differs from ORIGINAL, someone else changed structure!
                if (file_sentence_count != original_sentence_count) {
                    free(current_file_data);
                    
                    const char* msg = "\n⚠️  ERROR: File structure changed by another user.\nSentence boundaries have shifted. Disconnecting...\n";
                    write(client_socket, msg, strlen(msg));
                    logger("[SS-Thread] CONCURRENT EDIT DETECTED in REPLACE: File sentence count changed from %d to %d\n", 
                           original_sentence_count, file_sentence_count);
                    
                    // Abort this edit session
                    free(working_data);
                    release_sentence_lock(filename, sentence_num);
                    close(client_socket);
                    pthread_exit(NULL);
                }
                
                free(current_file_data);
            }
            
            // No data yet, loop again
            continue;
        }
        
        if (n <= 0) {
            logger("[SS-Thread] Client disconnected during REPLACE\n");
            break;
        }
        
        edit_buffer[n] = '\0';
        char* nl = strchr(edit_buffer, '\n');
        if (nl) *nl = '\0';
        
        // Check for ECALPER (end editing)
        if (strncmp(edit_buffer, "ECALPER", 7) == 0) {
            logger("[SS-Thread] ECALPER received, finalizing replace\n");
            
            // CRITICAL: Before writing, validate our working_data is still valid!
            pthread_mutex_lock(f_mutex);
            
            // Read current file state
            long check_size;
            char* current_file_data = read_file_to_string(full_path, &check_size);
            
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }
                
          // If file sentence count differs from ORIGINAL, someone else changed structure!
          if (file_sentence_count != original_sentence_count) {
              // Improved logging for debugging: record both counts before aborting
              logger("[SS-Thread] ECALPER VALIDATION: original=%d, current_file=%d, filename=%s, sentence=%d\n",
                  original_sentence_count, file_sentence_count, filename, sentence_num);

              free(current_file_data);
              pthread_mutex_unlock(f_mutex);
                    
              const char* msg = "ERROR: Cannot save - another user changed the file structure (added/removed delimiters).\nYour edits are discarded.\n";
              write(client_socket, msg, strlen(msg));
              logger("[SS-Thread] ECALPER BLOCKED: File sentence count changed from %d to %d by another user\n", 
                  original_sentence_count, file_sentence_count);
                    
              // Abort without saving
              free(working_data);
              release_sentence_lock(filename, sentence_num);
              close(client_socket);
              pthread_exit(NULL);
          }
                
                free(current_file_data);
            }
            
            // Validation passed - safe to write
            FILE *fp = fopen(full_path, "w");
            if (fp) {
                fprintf(fp, "%s", working_data);
                fclose(fp);
                
                // CRITICAL: Check if sentence structure changed
                // If so, invalidate all other sentence locks on this file
                extern int check_sentence_structure_changed(const char*, const char*);
                extern void release_all_sentence_locks_for_file(const char*);
                
                if (check_sentence_structure_changed(filename, working_data)) {
                    logger("[SS-Thread] Sentence structure changed in %s, invalidating other locks\n", filename);
                    release_all_sentence_locks_for_file(filename);
                }
                
                pthread_mutex_unlock(f_mutex);
                
                write(client_socket, "ACK:REPLACE_COMPLETE All changes saved.\n", 41);
                logger("[SS-Thread] File %s updated successfully (%d changes)\n", filename, changes_made);
            } else {
                pthread_mutex_unlock(f_mutex);
                write(client_socket, "ERR:500:WRITE_FAILED Could not save file\n", 42);
                logger("[SS-Thread] Failed to write file %s\n", filename);
            }
            break;
        }
        
        // Parse word replacement: <word_index> <content>
        // word_index is 0-indexed
        int word_index;
        char content[900];
        content[0] = '\0';
        
        int scan_result = sscanf(edit_buffer, "%d %899[^\n]", &word_index, content);
        if (scan_result < 1) {
            write(client_socket, "ERR:400:INVALID_FORMAT (Use: <word_index> <content> or ECALPER)\n", 65);
            continue;
        }
        
        // Process \n escape sequences in content (same as WRITE)
        if (scan_result >= 2) {
            char processed_content[900];
            int src = 0, dst = 0;
            while (content[src] != '\0' && dst < 899) {
                if (content[src] == '\\' && content[src + 1] == 'n') {
                    processed_content[dst++] = '\n';
                    src += 2;
                } else {
                    processed_content[dst++] = content[src++];
                }
            }
            processed_content[dst] = '\0';
            strcpy(content, processed_content);
        }
        
        logger("[SS-Thread] Replace word %d with '%s'\n", word_index, content);
        
        // CRITICAL: Re-validate sentence still exists at this index
        // (Another concurrent write might have added delimiters and shifted indices)
        pthread_mutex_lock(f_mutex);
        long check_size;
        char* current_file_data = read_file_to_string(full_path, &check_size);
        pthread_mutex_unlock(f_mutex);
        
            if (current_file_data) {
                // Count sentences in current file
                int file_sentence_count = 0;
                int tmp_s = 0, tmp_e = 0;
                while (find_sentence(current_file_data, file_sentence_count, &tmp_s, &tmp_e)) {
                    file_sentence_count++;
                }

                logger("[SS-Thread] REPLACE per-update VALIDATION: file=%d, original=%d, sentence=%d\n",
                       file_sentence_count, original_sentence_count, sentence_num);

                // If file sentence count differs from ORIGINAL, someone else changed structure!
                if (file_sentence_count != original_sentence_count) {
                    free(current_file_data);

                    write(client_socket, "ERROR: Sentence structure changed due to concurrent edit. Your edits are discarded.\n", 80);
                    logger("[SS-Thread] CONCURRENT EDIT CONFLICT in REPLACE: file_count %d != original %d (sentence %d)\n",
                           file_sentence_count, original_sentence_count, sentence_num);

                    // Abort this edit session
                    free(working_data);
                    release_sentence_lock(filename, sentence_num);
                    close(client_socket);
                    pthread_exit(NULL);
                }

                free(current_file_data);
            }
        
        // Find the sentence again (it may have changed)
        find_sentence(working_data, sentence_num, &s_start, &s_end);
        
        // Extract sentence
        int sent_len = s_end - s_start + 1;
        char* sentence = (char*)malloc(sent_len + 1);
        strncpy(sentence, working_data + s_start, sent_len);
        sentence[sent_len] = '\0';
        
        // Split sentence into words (0-indexed)
        char* words[200];
        int word_count = 0;
        char* sent_copy = strdup(sentence);
        char* token = strtok(sent_copy, " ");
        
        while (token && word_count < 200) {
            words[word_count++] = strdup(token);
            token = strtok(NULL, " ");
        }
        free(sent_copy);
        
        logger("[SS-Thread] Sentence has %d words\n", word_count);
        
        // Validate word_index (0-indexed)
        if (word_index < 0 || word_index >= word_count) {
            write(client_socket, "ERR:400:WORD_INDEX_OUT_OF_RANGE\n", 32);
            logger("[SS-Thread] Word index %d out of range (0-%d)\n", word_index, word_count - 1);
            for (int i = 0; i < word_count; i++) free(words[i]);
            free(sentence);
            continue;
        }
        
        // Check if content is "" (delete word)
        int is_delete = (scan_result == 1 || strcmp(content, "\"\"") == 0 || strlen(content) == 0);
        
        if (is_delete) {
            // Delete the word
            logger("[SS-Thread] Deleting word %d\n", word_index);
            free(words[word_index]);
            // Shift words left
            for (int i = word_index; i < word_count - 1; i++) {
                words[i] = words[i + 1];
            }
            word_count--;
            write(client_socket, "✓ ACK:WORD_DELETED\n", 20);
        } else {
            // Replace the word
            logger("[SS-Thread] Replacing word %d with '%s'\n", word_index, content);
            free(words[word_index]);
            words[word_index] = strdup(content);
            write(client_socket, "✓ ACK:WORD_UPDATED\n", 20);
        }
        
        changes_made++;
        
        // Rebuild sentence
        char new_sentence[4096];
        new_sentence[0] = '\0';
        for (int i = 0; i < word_count; i++) {
            if (i > 0) strcat(new_sentence, " ");
            strcat(new_sentence, words[i]);
        }
        
        // Rebuild working_data
        int before_len = s_start;
        int after_start = s_end + 1;
        int after_len = strlen(working_data) - after_start;
        
        char* new_data = (char*)malloc(before_len + strlen(new_sentence) + after_len + 10);
        new_data[0] = '\0';
        
        // Copy before sentence
        strncat(new_data, working_data, before_len);
        // Add new sentence
        strcat(new_data, new_sentence);
        // Copy after sentence
        if (after_len > 0) {
            strcat(new_data, working_data + after_start);
        }
        
        free(working_data);
        working_data = new_data;
        
        logger("[SS-Thread] Sentence rebuilt: %s\n", new_sentence);
        
        // Cleanup
        for (int i = 0; i < word_count; i++) free(words[i]);
        free(sentence);
    }
    
    // Cleanup
    free(working_data);
    release_sentence_lock(filename, sentence_num);
    logger("[SS-Thread] REPLACE complete, lock released\n");
}

// ============= END CHECKPOINT HANDLERS =============

void *handle_client_request(void *arg) {
    // 1. Get the client socket from the argument
    int client_socket = *(int *)arg;
    free(arg); // Free the heap memory for the socket pointer

    char buffer[BUFFER_SIZE];
    int read_size;

    logger("[SS-Thread] New client connected.\n");

    // 2. Loop to read commands from this client
    while ((read_size = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        // Null-terminate the received string
        buffer[read_size] = '\0';
        logger("[SS-Thread] Received command: %s", buffer);
        
        if (strncmp(buffer, "CREATE", 6) == 0 || strncmp(buffer, "DELETE", 6) == 0 ) {
            char *err = "ERR:401:UNAUTHORIZED (Clients cannot use this command)\n";
            write(client_socket, err, strlen(err));
            continue; // Ignore and wait for next command
        }
        else if (strncmp(buffer, "UNDO", 4) == 0) {
            handle_undo_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "READ", 4) == 0) {
            handle_read_command(client_socket, buffer, "");
        }
        else if (strncmp(buffer, "WRITE", 5) == 0) {
          handle_write_command(client_socket, buffer, "");
        } 
        else if (strncmp(buffer, "REPLACE", 7) == 0) {
            handle_replace_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "STREAM", 6) == 0) {
            handle_stream_command(client_socket, buffer, "");
        }
        else if (strncmp(buffer, "CHECKPOINT", 10) == 0) {
            handle_checkpoint_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "VIEWCHECKPOINT", 14) == 0) {
            handle_viewcheckpoint_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "REVERT", 6) == 0) {
            handle_revert_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "LISTCHECKPOINTS", 15) == 0) {
            handle_listcheckpoints_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "DIFF", 4) == 0) {
            handle_diff_command(client_socket, buffer);
        }
        else {
            // Unknown command
            char *err = "ERR:400:UNKNOWN_COMMAND\n";
            write(client_socket, err, strlen(err));
        }
        memset(buffer, 0, BUFFER_SIZE);
    }

    // 3. Handle client disconnect
    if (read_size == 0) {
        logger("[SS-Thread] Client connection closed (command completed).\n");
    } else if (read_size == -1) {
        perror("[SS-Thread] read failed");
    }

    // 4. Clean up: close the socket and exit the thread
    close(client_socket);
    pthread_exit(NULL);
}

void start_client_storage_server_loop(int my_port) {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;

    // 1. Create the SS's own server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("SS Server: socket failed");
        exit(EXIT_FAILURE);
    }

    // Optional: Allow port reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }

    // 2. Bind the server socket to *your* port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(my_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("SS Server: bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("SS Server: listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    logger("\n--- Storage Server is now ONLINE. Listening on port %d ---\n\n", my_port);

    // 4. Main accept() loop
    // This loop runs forever, waiting for new connections
    while (1) {
        client_addr_len = sizeof(client_addr);
        
        // accept() blocks and waits for a new connection
        client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("SS Server: accept failed");
            continue; // Skip this failed connection and wait for the next
        }

        logger("[SS-Main] New connection accepted! Spawning thread...\n");

        // 5. Create a new thread for this client
        pthread_t thread_id;
        
        // We must pass the client_socket on the heap
        // so it's unique for each thread.
        int *p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_create(&thread_id, NULL, handle_client_request, (void *)p_client_socket) < 0) {
            perror("pthread_create failed");
            free(p_client_socket); // Clean up if thread creation failed
            close(client_socket);
        }

        // Detach the thread - we don't need to join() it.
        // The OS will reclaim its resources when it exits.
        pthread_detach(thread_id);
    }

    // This code is never reached
    close(server_fd);
}
