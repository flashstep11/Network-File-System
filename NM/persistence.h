#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "nm.h"

// Persistence module for Name Server metadata
// Stores file metadata, ACL, and ownership information to disk

#define METADATA_FILE "nm_metadata.dat"

// Save all file metadata to disk
int persist_save_metadata(NameServer* nm);

// Load all file metadata from disk
int persist_load_metadata(NameServer* nm);

// Helper: Serialize a single file's metadata
int persist_save_file_entry(FILE* fp, FileMetadata* file_info);

// Helper: Deserialize a single file's metadata
FileMetadata* persist_load_file_entry(FILE* fp);

#endif // PERSISTENCE_H
