#include "defs.h" // This has all your system headers
#include "log.h"  // For the logger
#include "client_handler.h" // For its own declaration

int check_permissions(int client_socket, const char *filename, const char *mode) {
    logger("[AccessControl] Checking %s permission for file %s\n", mode, filename);
    
    // 1. Get Client IP Address
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        logger("[AccessControl] getpeername failed\n");
        perror("getpeername failed");
        return 0; // Fail safe
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    logger("[AccessControl] Client IP: %s\n", client_ip);

    // 2. Connect to NM
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        logger("[AccessControl] Failed to create socket to NM\n");
        return 0;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        logger("[AccessControl] Invalid NM IP\n");
        close(nm_sock);
        return 0;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        logger("[AccessControl] Could not reach NM to verify permissions.\n");
        close(nm_sock);
        return 0; // If NM is down, deny access for security
    }
    
    logger("[AccessControl] Connected to NM\n");

    // 3. Send Query: CHECK_ACCESS <IP> <FILENAME> <MODE>
    char query[512];
    sprintf(query, "CHECK_ACCESS %s %s %s\n", client_ip, filename, mode);
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

    logger("[AccessControl] Denied access to %s for %s on file %s\n", client_ip, mode, filename);
    return 0; // Denied
}

// Bonus feature: Notify NM about editing activity (real-time collaborative awareness)
void notify_nm_editing(int client_socket, const char* filename, const char* action, int sentence_idx) {
    // Get client's username by IP (same method as check_permissions)
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    if (getpeername(client_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        logger("[Notify] getpeername failed\n");
        return; // Non-critical, just skip notification
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    // Connect to NM
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        logger("[Notify] Failed to create socket to NM\n");
        return;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr) <= 0) {
        close(nm_sock);
        return;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        close(nm_sock);
        return; // NM unreachable, skip notification
    }
    
    // We need to get username from IP - ask NM via special query
    // For simplicity, we'll send IP and let NM map it
    // Format: NOTIFY_EDIT <client_ip> <filename> <action> <sentence_idx>
    char notify_msg[512];
    snprintf(notify_msg, sizeof(notify_msg), "NOTIFY_EDIT %s %s %s %d\n", 
             client_ip, filename, action, sentence_idx);
    write(nm_sock, notify_msg, strlen(notify_msg));
    
    // Read acknowledgment (optional)
    char ack[64] = {0};
    read(nm_sock, ack, 63);
    close(nm_sock);
    
    logger("[Notify] Sent editing notification: %s\n", notify_msg);
}

void handle_read_command(int client_socket, char* buffer) {
    char filename[256]; // Use 256 for a standard filename buffer

    // 1. --- Parse the command ---
    // Command format: READ <filename>\n
    if (sscanf(buffer, "READ %255s", filename) != 1) {
        char *err = "ERR:400:BAD_READ_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return; // Return from this function
    }

    logger("[SS-Thread] Read request for: %s\n", filename);

    if (!check_permissions(client_socket, filename, "READ")) {
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
void handle_write_command(int client_socket, char* buffer) {
    char filename[256];
    int sentence_num;
    
    // 1. Parse - Interactive mode: WRITE <filename> <sentence_num>
    // Client will then send multiple "<word_index> <content>" lines
    // Note: sentence_num is 0-indexed, word_index is 1-indexed
    if (sscanf(buffer, "WRITE %255s %d", filename, &sentence_num) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: WRITE <filename> <sentence_num>)\n", 61);
        return;
    }
    
    logger("[SS-Thread] WRITE request: %s sentence %d\n", filename, sentence_num);

    // 2. ACCESS CONTROL
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        logger("[SS-Thread] WRITE denied - no permission\n");
        return;
    }

    // 3. PRE-VALIDATION: Check if sentence exists before acquiring lock
    // Load file to validate sentence exists (or if it's sentence 0 which can be created)
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
        
        // If creating a new sentence (not sentence 0), check that last sentence ends with period
        if (sentence_num > 0) {
            // Find the last sentence
            int last_s_start = 0, last_s_end = 0;
            find_sentence(temp_data, sentence_count - 1, &last_s_start, &last_s_end);
            
            // Check if last sentence ends with a period
            if (last_s_end >= 0 && temp_data[last_s_end] != '.') {
                write(client_socket, "ERROR: Cannot create new sentence. Previous sentence must end with a period.\n", 78);
                logger("[SS-Thread] Cannot create sentence %d - sentence %d doesn't end with period\n", 
                       sentence_num, sentence_count - 1);
                free(temp_data);
                return;
            }
        }
        
        logger("[SS-Thread] Will create new sentence %d\n", sentence_num);
    }
    
    free(temp_data); // Free the temporary validation data
    logger("[SS-Thread] Sentence validation passed for sentence %d\n", sentence_num);

    // 4. SENTENCE LOCK (The Critical Requirement)
    if (!acquire_sentence_lock(filename, sentence_num)) {
        char *err = "ERR:423:SENTENCE_LOCKED_BY_ANOTHER_USER\n";
        write(client_socket, err, strlen(err));
        logger("[SS-Thread] WRITE denied - sentence locked\n");
        return;
    }
    
    logger("[SS-Thread] Sentence lock acquired for %s sentence %d\n", filename, sentence_num);
    
    // **BONUS: Notify NM that editing started (collaborative awareness)**
    notify_nm_editing(client_socket, filename, "START", sentence_num);
    
    // Send ACK to client to start interactive editing
    const char* ack_msg = "ACK:SENTENCE_LOCKED Enter word updates (word_index content) or ETIRW to finish\n";
    write(client_socket, ack_msg, strlen(ack_msg));

    // 5. Load file and create backup
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    if (!f_mutex) {
        write(client_socket, "ERR:500:INTERNAL_ERROR\n", 23);
        release_sentence_lock(filename, sentence_num);
        return;
    }
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
    
    // 5. Interactive editing loop
    char edit_buffer[1024];
    while (1) {
        memset(edit_buffer, 0, sizeof(edit_buffer));
        ssize_t n = read(client_socket, edit_buffer, sizeof(edit_buffer) - 1);
        
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
            
            // Write working data to file
            FILE *fp = fopen(full_path, "w");
            if (fp) {
                fprintf(fp, "%s", working_data);
                fclose(fp);
                write(client_socket, "ACK:WRITE_COMPLETE All changes saved\n", 38);
                logger("[SS-Thread] File %s updated successfully\n", filename);
            } else {
                write(client_socket, "ERR:500:WRITE_FAILED Could not save file\n", 42);
                logger("[SS-Thread] Failed to write file %s\n", filename);
            }
            break;
        }
        
        // Parse word update: <word_index> <content>
        // word_index is 1-indexed (word 1, word 2, etc.)
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
            
            // Check if any word contains a period - this creates sentence boundaries
            int period_found = -1; // Index of word containing period
            char* before_period = NULL; // Part before period (including period)
            char* after_period = NULL; // Part after period
            
            for (int i = 0; i < word_count; i++) {
                char* period_pos = strchr(words[i], '.');
                if (period_pos) {
                    period_found = i;
                    // Split the word at the period
                    int before_len = (period_pos - words[i]) + 1; // Include the period
                    before_period = (char*)malloc(before_len + 1);
                    strncpy(before_period, words[i], before_len);
                    before_period[before_len] = '\0';
                    
                    // Check if there's content after the period in the same word
                    if (*(period_pos + 1) != '\0') {
                        after_period = strdup(period_pos + 1);
                    }
                    break;
                }
            }
            
            logger("[SS-Thread] Period analysis: period_found=%d, before='%s', after='%s'\n",
                   period_found, before_period ? before_period : "NULL", after_period ? after_period : "NULL");
            
            // Handle empty file or append operation
            size_t current_len = strlen(working_data);
            
            // Try to find the sentence first
            int s_start = 0, s_end = 0;
            int sentence_found = find_sentence(working_data, sentence_num, &s_start, &s_end);
            
            logger("[SS-Thread] find_sentence returned %d, s_start=%d, s_end=%d\n", 
                   sentence_found, s_start, s_end);
            
            if (!sentence_found) {
                // Sentence doesn't exist yet - we're creating a new sentence
                logger("[SS-Thread] Creating new sentence %d\n", sentence_num);
                
                char* new_data;
                if (current_len == 0) {
                    // Empty file - join all words with spaces
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
                    // File has content - append as new sentence with period delimiter
                    // Add ". " before the new sentence if last char isn't a period
                    int needs_period = (current_len > 0 && working_data[current_len - 1] != '.');
                    
                    int total_len = current_len;
                    for (int i = 0; i < word_count; i++) {
                        total_len += strlen(words[i]) + 1; // +1 for space
                    }
                    if (needs_period) total_len += 2; // for ". "
                    
                    new_data = (char*)malloc(total_len + 1);
                    if (new_data) {
                        strcpy(new_data, working_data);
                        if (needs_period) {
                            strcat(new_data, ". ");
                        } else {
                            strcat(new_data, " ");
                        }
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
                if (before_period) free(before_period);
                if (after_period) free(after_period);
                continue;
            }
            
            // Sentence exists, now INSERT words at the specified position
            // Insert all words from the command at consecutive positions
            {
                // Count existing words in the sentence (excluding standalone delimiters)
                int existing_word_count = 0;
                int pos = s_start;
                while (pos <= s_end) {
                    // Skip spaces
                    while (pos <= s_end && working_data[pos] == ' ') pos++;
                    if (pos <= s_end) {
                        // Check if this is a standalone delimiter (period, !, ?)
                        if ((working_data[pos] == '.' || working_data[pos] == '!' || working_data[pos] == '?') &&
                            (pos + 1 > s_end || working_data[pos + 1] == ' ')) {
                            // This is a standalone delimiter at the end, don't count it as a word
                            break;
                        }
                        existing_word_count++;
                        // Skip to next space
                        while (pos <= s_end && working_data[pos] != ' ') pos++;
                    }
                }
              printf("[SS-Thread] Sentence %d has %d existing words\n", sentence_num, existing_word_count);  
                // Validate word_index (1-indexed: valid range is 1 to existing_word_count+1)
                if (word_index < 1 || word_index > existing_word_count + 1) {
                    write(client_socket, "ERROR: Word index out of range.\n", 33);
                    logger("[SS-Thread] Invalid word_index %d (sentence has %d words, valid range: 1-%d)\n", 
                           word_index, existing_word_count, existing_word_count + 1);
                    
                    // Free word tokens
                    for (int i = 0; i < word_count; i++) {
                        free(words[i]);
                    }
                    if (before_period) free(before_period);
                    if (after_period) free(after_period);
                    continue;
                }
                
                // Find insertion position in the string (word_index is 1-indexed)
                int insert_pos;
                if (word_index == existing_word_count + 1) {
                    // Append to end of sentence - but check if sentence ends with period
                    if (working_data[s_end] == '.' || working_data[s_end] == '!' || working_data[s_end] == '?') {
                        // Insert before the period
                        insert_pos = s_end;
                    } else {
                        // No period, insert after sentence end
                        insert_pos = s_end + 1;
                    }
                } else if (word_index == 1) {
                    // At beginning of sentence (word 1)
                    insert_pos = s_start;
                } else {
                    // At word_index position (convert 1-indexed to position)
                    int curr_word = 1; // Start from word 1
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
                if (period_found >= 0) {
                    // We're creating a sentence boundary by INSERTING words with period
                    // Build new content in parts:
                    // 1. Everything before insertion point (before insert_pos)
                    // 2. Words 0 to period_found (with before_period as the last word before period)
                    // 3. " " (single space after period to start new sentence)
                    // 4. after_period (if any) and remaining words (period_found+1 onwards) - new sentence
                    // 5. Rest of original sentence content (from insert_pos to s_end) - continues in new sentence
                    // 6. Rest of file (after s_end)
                    
                    // Calculate size
                    int size_needed = working_len + 100; // Extra buffer
                    for (int i = 0; i < word_count; i++) {
                        size_needed += strlen(words[i]) + 1;
                    }
                    if (before_period) size_needed += strlen(before_period);
                    if (after_period) size_needed += strlen(after_period);
                    
                    new_data = (char*)malloc(size_needed);
                    if (!new_data) {
                        write(client_socket, "ERR:500:MEMORY_ERROR\n", 21);
                        for (int i = 0; i < word_count; i++) free(words[i]);
                        if (before_period) free(before_period);
                        if (after_period) free(after_period);
                        continue;
                    }
                    
                    int write_pos = 0;
                    
                    // 1. Copy everything before insertion point
                    if (insert_pos > s_start) {
                        memcpy(new_data, working_data, insert_pos);
                        write_pos = insert_pos;
                        // Remove trailing spaces
                        while (write_pos > 0 && new_data[write_pos - 1] == ' ') write_pos--;
                    }
                    
                    // 2. Add words before and including period (0 to period_found)
                    for (int i = 0; i <= period_found; i++) {
                        if (write_pos > 0) {
                            new_data[write_pos++] = ' ';
                        }
                        
                        if (i == period_found) {
                            // Use before_period (includes the period)
                            int len = strlen(before_period);
                            memcpy(new_data + write_pos, before_period, len);
                            write_pos += len;
                        } else {
                            int word_len = strlen(words[i]);
                            memcpy(new_data + write_pos, words[i], word_len);
                            write_pos += word_len;
                        }
                    }
                    
                    // 3. Add space to separate sentences
                    new_data[write_pos++] = ' ';
                    
                    // 4. Start new sentence with after_period and remaining words
                    int new_sentence_start = write_pos;
                    if (after_period) {
                        int len = strlen(after_period);
                        memcpy(new_data + write_pos, after_period, len);
                        write_pos += len;
                    }
                    
                    for (int i = period_found + 1; i < word_count; i++) {
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
                if (before_period) free(before_period);
                if (after_period) free(after_period);
            }
        } else {
            write(client_socket, "ERR:400:BAD_FORMAT Use: word_index content\n", 44);
        }
    }
    
    // Cleanup
    if (file_data) free(file_data);
    if (working_data) free(working_data);
    
    // **BONUS: Notify NM that editing finished (collaborative awareness)**
    notify_nm_editing(client_socket, filename, "END", sentence_num);
    
    // Release locks
    pthread_mutex_unlock(f_mutex);
    release_sentence_lock(filename, sentence_num);
    logger("[SS-Thread] Sentence lock released for %s sentence %d\n", filename, sentence_num);
}
void handle_write_command_old_single_shot(int client_socket, char* buffer) {
    // OLD IMPLEMENTATION - KEPT FOR REFERENCE
    char filename[256];
    int sentence_num, word_index;
    // 1. Parse
    // Command: WRITE <filename> <sentence_num> <word_index> <content...>
    if (sscanf(buffer, "WRITE %255s %d %d", filename, &sentence_num, &word_index) != 3) {
        write(client_socket, "ERR:400:BAD_REQUEST\n", 20);
        return;
    }

    // Find start of content
    char *content = buffer;
    int spaces = 0;
    while(*content && spaces < 4){ if(*content++ == ' ') spaces++; }
    // Trim newline
    char *nl = strrchr(content, '\n');
    if(nl) *nl = '\0';

    // 2. ACCESS CONTROL
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }

    // 3. SENTENCE LOCK (The Critical Requirement)
    // This check ensures no one else is editing THIS sentence right now.
    if (!acquire_sentence_lock(filename, sentence_num)) {
        // Fulfills "Attempting to write a locked sentence" error requirement
        char *err = "ERR:503:SENTENCE_LOCKED_BY_ANOTHER_USER\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // 4. Perform the Read-Modify-Write
    // We grab the physical file mutex now.
    // Even though this part is serialized, the "Sentence Lock" above allowed
    // us to conceptually separate the locking domains.
    pthread_mutex_t* f_mutex = get_file_mutex(filename);
    pthread_mutex_lock(f_mutex); 

    long fsize;
    char* file_data = read_file_to_string(filename, &fsize);
    
    if (!file_data) {
        if (sentence_num == 0) { // Creating/Appending to empty file allowed?
             file_data = strdup("");
        } else {
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            write(client_socket, "ERR:404:FILE_NOT_FOUND\n", 23);
            return;
        }
    }

    // --- LOGIC TO REPLACE WORD/SENTENCE ---
    int s_start, s_end;
    char *new_file_data = NULL;

    if (find_sentence(file_data, sentence_num, &s_start, &s_end)) {
        // Sentence found. Now find word? 
        // Note: Requirement 30 says "update content at a word level".
        // If word_index is valid, replace word. 
        // Implementation of string manipulation is standard C (malloc, strncpy, strcat)...
        // [Omitted for brevity, but assume new_file_data is created here]
        
        // Placeholder logic: Append content for testing
        // Ideally: new_file_data = replace_word_in_str(file_data, s_start, ... content);
        int w_start, w_end;
    
    // Attempt to find the specific word within the found sentence
    if (find_word(file_data, s_start, s_end, word_index, &w_start, &w_end)) {
        
        // --- 1. Calculate new size ---
        int len_before = w_start;                      // Data before the word
        int len_new_content = strlen(content);         // The new text
        int len_after = strlen(file_data + w_end + 1); // Data after the word (including null term)
        
        // --- 2. Allocate Memory ---
        new_file_data = (char *)malloc(len_before + len_new_content + len_after + 1);
        if (new_file_data == NULL) {
            write(client_socket, "ERR:500:MEMORY_ALLOC_FAILED\n", 28);
            pthread_mutex_unlock(f_mutex);
            release_sentence_lock(filename, sentence_num);
            free(file_data);
            return;
        }

        // --- 3. Construct New String ---
        // Copy the part before the word
        memcpy(new_file_data, file_data, len_before);
        
        // Copy the new word/content
        memcpy(new_file_data + len_before, content, len_new_content);
        
        // Copy the part after the word (strcpy handles the null terminator)
        strcpy(new_file_data + len_before + len_new_content, file_data + w_end + 1);

    } else {
        // --- Word Index Not Found ---
        // As per specifications, this is an error (unless you implement specific append logic)
        write(client_socket, "ERR:404:WORD_INDEX_OUT_OF_BOUNDS\n", 33);
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        free(file_data);
        return;
    }
    } 
    else {
        // Error handling for invalid sentence index
        pthread_mutex_unlock(f_mutex);
        release_sentence_lock(filename, sentence_num);
        free(file_data);
        write(client_socket, "ERR:404:SENTENCE_NOT_FOUND\n", 27);
        return;
    }

    // 5. Create Backup (For UNDO)
    char bak_name[300];
    sprintf(bak_name, "%s.bak", filename);
    FILE *bak = fopen(bak_name, "w");
    if (bak) { fprintf(bak, "%s", file_data); fclose(bak); }

    // 6. Write New Data
    // 6. Write New Data to Disk
    FILE *fp = fopen(filename, "w");
    if (fp) {
        // Use the new constructed string
        if (new_file_data) {
            fprintf(fp, "%s", new_file_data);
        } else {
            // Fallback for empty file creation cases if necessary
            fprintf(fp, "%s", file_data); 
        }
        fclose(fp);
        write(client_socket, "ACK:WRITE_SUCCESS\n", 18);
    } else {
        write(client_socket, "ERR:500:WRITE_FAILED\n", 21);
    }

    // Clean up
    if (file_data) free(file_data);
    if (new_file_data) free(new_file_data);
    // if(new_file_data) free(new_file_data);

    // 7. RELEASE LOCKS
    pthread_mutex_unlock(f_mutex);
    release_sentence_lock(filename, sentence_num);
}

void handle_stream_command(int client_socket, char* buffer) {
    char filename[256];
    
    // 1. --- Parse the command ---
    // Command format: STREAM <filename>\n
    if (sscanf(buffer, "STREAM %255s", filename) != 1) {
        char *err = "ERR:400:BAD_COMMAND_FORMAT\n";
        write(client_socket, err, strlen(err));
        return; // Return from this function
    }
    if (!check_permissions(client_socket, filename, "READ")) {
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
    char filename[256], tag[64];
    
    if (sscanf(buffer, "CHECKPOINT %255s %63s", filename, tag) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: CHECKPOINT <filename> <tag>)\n", 58);
        return;
    }
    
    logger("[SS-Thread] CHECKPOINT request: %s tag=%s\n", filename, tag);
    
    // Check permissions
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
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
    char filename[256], tag[64];
    
    if (sscanf(buffer, "VIEWCHECKPOINT %255s %63s", filename, tag) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: VIEWCHECKPOINT <filename> <tag>)\n", 62);
        return;
    }
    
    logger("[SS-Thread] VIEWCHECKPOINT request: %s tag=%s\n", filename, tag);
    
    // Check permissions
    if (!check_permissions(client_socket, filename, "READ")) {
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
    char filename[256], tag[64];
    
    if (sscanf(buffer, "REVERT %255s %63s", filename, tag) != 2) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: REVERT <filename> <tag>)\n", 54);
        return;
    }
    
    logger("[SS-Thread] REVERT request: %s tag=%s\n", filename, tag);
    
    // Check permissions
    if (!check_permissions(client_socket, filename, "WRITE")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
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
    char filename[256];
    
    if (sscanf(buffer, "LISTCHECKPOINTS %255s", filename) != 1) {
        write(client_socket, "ERR:400:BAD_REQUEST (Usage: LISTCHECKPOINTS <filename>)\n", 57);
        return;
    }
    
    logger("[SS-Thread] LISTCHECKPOINTS request: %s\n", filename);
    
    // Check permissions
    if (!check_permissions(client_socket, filename, "READ")) {
        write(client_socket, "ERR:403:ACCESS_DENIED\n", 22);
        return;
    }
    
    char* list = list_checkpoints(filename);
    write(client_socket, list, strlen(list));
    free(list);
}

// ============= END CHECKPOINT HANDLERS =============

void *handle_client_request(void *arg) {
    // 1. Get the client socket from the argument
    int client_socket = *(int *)arg;
    free(arg); // Free the heap memory for the socket pointer

    char buffer[BUFFER_SIZE];
    int read_size;

    logger("[SS-Thread] New client connected. Waiting for commands...\n");

    // 2. Loop to read commands from this client
    // read() blocks until data is received or connection is closed
    while ((read_size = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        // Null-terminate the received string
        buffer[read_size] = '\0';
        logger("[SS-Thread] Received command: %s", buffer);
        if (strncmp(buffer, "CREATE", 6) == 0 || strncmp(buffer, "DELETE", 6) == 0 )
        {
            char *err = "ERR:401:UNAUTHORIZED (Clients cannot use this command)\n";
            write(client_socket, err, strlen(err));
            continue; // Ignore and wait for next command
        }
        else if (strncmp(buffer, "UNDO", 4) == 0) {
            handle_undo_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "READ", 4) == 0) {
            handle_read_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "WRITE", 5) == 0) {
          handle_write_command(client_socket, buffer);
        } 
        else if (strncmp(buffer, "STREAM", 6) == 0) {
            // Just call your new function
            handle_stream_command(client_socket, buffer);
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
        else {
            // Unknown command
            char *err = "ERR:400:UNKNOWN_COMMAND\n";
            write(client_socket, err, strlen(err));
        }
         // 3. Send a generic ACK back to the client
        // write(client_socket, "ACK: Command received and processed.\n", 37);
        // Clear the buffer for the next read
        memset(buffer, 0, BUFFER_SIZE);
    }

    // 4. Handle client disconnect
    if (read_size == 0) {
        logger("[SS-Thread] Client disconnected.\n");
    } else if (read_size == -1) {
        perror("[SS-Thread] read failed");
    }

    // 5. Clean up: close the socket and exit the thread
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
