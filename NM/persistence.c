// persistence.c - Persistent storage for Name Server metadata

#include "persistence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// File format (binary for simplicity):
// [HEADER: "NMDATA\n" (8 bytes)]
// [VERSION: 1 (4 bytes)]
// [FILE_COUNT (4 bytes)]
// For each file:
//   [filename length (4 bytes)][filename (variable)]
//   [owner length (4 bytes)][owner (variable)]
//   [ss_id (4 bytes)]
//   [created_time (8 bytes)][last_modified (8 bytes)][last_accessed (8 bytes)]
//   [file_size (8 bytes)][word_count (4 bytes)][char_count (4 bytes)]
//   [ACL_COUNT (4 bytes)]
//   For each ACL entry:
//     [username length (4 bytes)][username (variable)]
//     [can_read (1 byte)][can_write (1 byte)]

int persist_save_file_entry(FILE* fp, FileMetadata* file_info) {
    if (!fp || !file_info) return -1;
    
    // Filename
    int filename_len = strlen(file_info->filename);
    fwrite(&filename_len, sizeof(int), 1, fp);
    fwrite(file_info->filename, 1, filename_len, fp);
    
    // Owner
    int owner_len = strlen(file_info->owner);
    fwrite(&owner_len, sizeof(int), 1, fp);
    fwrite(file_info->owner, 1, owner_len, fp);
    
    // SS ID
    fwrite(&file_info->ss_id, sizeof(int), 1, fp);
    
    // Timestamps
    fwrite(&file_info->created_time, sizeof(time_t), 1, fp);
    fwrite(&file_info->last_modified, sizeof(time_t), 1, fp);
    fwrite(&file_info->last_accessed, sizeof(time_t), 1, fp);
    
    // File stats
    fwrite(&file_info->file_size, sizeof(long), 1, fp);
    fwrite(&file_info->word_count, sizeof(int), 1, fp);
    fwrite(&file_info->char_count, sizeof(int), 1, fp);
    
    // ACL
    int acl_count = 0;
    AccessEntry* ace = file_info->acl;
    while (ace) {
        acl_count++;
        ace = ace->next;
    }
    fwrite(&acl_count, sizeof(int), 1, fp);
    
    ace = file_info->acl;
    while (ace) {
        int username_len = strlen(ace->username);
        fwrite(&username_len, sizeof(int), 1, fp);
        fwrite(ace->username, 1, username_len, fp);
        fwrite(&ace->can_read, sizeof(char), 1, fp);
        fwrite(&ace->can_write, sizeof(char), 1, fp);
        ace = ace->next;
    }
    
    return 0;
}

FileMetadata* persist_load_file_entry(FILE* fp) {
    if (!fp) return NULL;
    
    FileMetadata* file_info = (FileMetadata*)malloc(sizeof(FileMetadata));
    if (!file_info) return NULL;
    
    memset(file_info, 0, sizeof(FileMetadata));
    
    // Filename
    int filename_len;
    if (fread(&filename_len, sizeof(int), 1, fp) != 1) {
        free(file_info);
        return NULL;
    }
    if (filename_len >= MAX_FILENAME_LEN) {
        free(file_info);
        return NULL;
    }
    fread(file_info->filename, 1, filename_len, fp);
    file_info->filename[filename_len] = '\0';
    
    // Owner
    int owner_len;
    if (fread(&owner_len, sizeof(int), 1, fp) != 1) {
        free(file_info);
        return NULL;
    }
    if (owner_len >= MAX_USERNAME_LEN) {
        free(file_info);
        return NULL;
    }
    fread(file_info->owner, 1, owner_len, fp);
    file_info->owner[owner_len] = '\0';
    
    // SS ID
    fread(&file_info->ss_id, sizeof(int), 1, fp);
    
    // Timestamps
    fread(&file_info->created_time, sizeof(time_t), 1, fp);
    fread(&file_info->last_modified, sizeof(time_t), 1, fp);
    fread(&file_info->last_accessed, sizeof(time_t), 1, fp);
    
    // File stats
    fread(&file_info->file_size, sizeof(long), 1, fp);
    fread(&file_info->word_count, sizeof(int), 1, fp);
    fread(&file_info->char_count, sizeof(int), 1, fp);
    
    // Initialize lock
    pthread_rwlock_init(&file_info->lock, NULL);
    
    // ACL
    int acl_count;
    fread(&acl_count, sizeof(int), 1, fp);
    
    file_info->acl = NULL;
    AccessEntry* last_ace = NULL;
    
    for (int i = 0; i < acl_count; i++) {
        AccessEntry* ace = (AccessEntry*)malloc(sizeof(AccessEntry));
        if (!ace) continue;
        
        int username_len;
        fread(&username_len, sizeof(int), 1, fp);
        if (username_len >= MAX_USERNAME_LEN) {
            free(ace);
            continue;
        }
        fread(ace->username, 1, username_len, fp);
        ace->username[username_len] = '\0';
        
        fread(&ace->can_read, sizeof(char), 1, fp);
        fread(&ace->can_write, sizeof(char), 1, fp);
        
        ace->next = NULL;
        
        if (last_ace) {
            last_ace->next = ace;
        } else {
            file_info->acl = ace;
        }
        last_ace = ace;
    }
    
    return file_info;
}

// Helper to collect all files from trie for saving
static void trie_collect_all_files(TrieNode* node, FileMetadata** files, int* count, int max_count) {
    if (!node || *count >= max_count) return;
    
    if (node->is_end_of_word && node->file_info) {
        files[(*count)++] = node->file_info;
    }
    
    for (int i = 0; i < 128; i++) {
        if (node->children[i]) {
            trie_collect_all_files(node->children[i], files, count, max_count);
        }
    }
}

int persist_save_metadata(NameServer* nm) {
    if (!nm) return -1;
    
    FILE* fp = fopen(METADATA_FILE, "wb");
    if (!fp) {
        perror("Failed to open metadata file for writing");
        return -1;
    }
    
    // Write header
    const char* header = "NMDATA\n";
    fwrite(header, 1, 8, fp);
    
    // Write version
    int version = 1;
    fwrite(&version, sizeof(int), 1, fp);
    
    // Collect all files
    FileMetadata* files[10000];
    int file_count = 0;
    
    pthread_rwlock_rdlock(&nm->trie_lock);
    trie_collect_all_files(nm->file_trie_root, files, &file_count, 10000);
    pthread_rwlock_unlock(&nm->trie_lock);
    
    // Write file count
    fwrite(&file_count, sizeof(int), 1, fp);
    
    // Write each file
    for (int i = 0; i < file_count; i++) {
        pthread_rwlock_rdlock(&files[i]->lock);
        persist_save_file_entry(fp, files[i]);
        pthread_rwlock_unlock(&files[i]->lock);
    }
    
    fclose(fp);
    nm_log("[PERSIST] Saved %d files to %s\n", file_count, METADATA_FILE);
    return 0;
}

int persist_load_metadata(NameServer* nm) {
    if (!nm) return -1;
    
    FILE* fp = fopen(METADATA_FILE, "rb");
    if (!fp) {
        nm_log("[PERSIST] No metadata file found (first run or empty system)\n");
        return 0; // Not an error - first run
    }
    
    // Read and verify header
    char header[9] = {0};
    fread(header, 1, 8, fp);
    if (strcmp(header, "NMDATA\n") != 0) {
        nm_log("[PERSIST] Invalid metadata file format\n");
        fclose(fp);
        return -1;
    }
    
    // Read version
    int version;
    fread(&version, sizeof(int), 1, fp);
    if (version != 1) {
        nm_log("[PERSIST] Unsupported metadata version: %d\n", version);
        fclose(fp);
        return -1;
    }
    
    // Read file count
    int file_count;
    fread(&file_count, sizeof(int), 1, fp);
    
    nm_log("[PERSIST] Loading %d files from metadata...\n", file_count);
    
    // Load each file
    int loaded = 0;
    for (int i = 0; i < file_count; i++) {
        FileMetadata* file_info = persist_load_file_entry(fp);
        if (file_info) {
            // Add to trie and cache
            pthread_rwlock_wrlock(&nm->trie_lock);
            trie_insert(nm->file_trie_root, file_info->filename, file_info);
            pthread_rwlock_unlock(&nm->trie_lock);
            cache_put(nm->search_cache, file_info->filename, file_info);
            loaded++;
            
            nm_log("[PERSIST] Loaded: %s (owner=%s, ss_id=%d)\n", 
                   file_info->filename, file_info->owner, file_info->ss_id);
        }
    }
    
    fclose(fp);
    nm_log("[PERSIST] Successfully loaded %d/%d files\n", loaded, file_count);
    return 0;
}
