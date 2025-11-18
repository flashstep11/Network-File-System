[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# Network File System (NFS) - Course Project

A distributed network file system with persistent storage, access control, and concurrent operations.

## Project Specification

Full documentation: https://karthikv1392.github.io/cs3301_osn/course_project/

## Key Features Implemented

### ✅ Core Functionality (150 points)
- File operations: CREATE, READ, WRITE, DELETE, INFO
- Interactive WRITE mode with sentence-level locking
- STREAM command (word-by-word with 0.1s delay)
- VIEW command with -a (all) and -l (details) flags
- Access control: ADDACCESS, REMACCESS
- EXEC command (execute file as shell commands)
- UNDO support (one level per file)
- LIST users command

### ✅ System Requirements (40 points)
- **Persistent Storage** - Files and metadata survive restarts
- **Access Control** - Enforced read/write permissions
- **Efficient Search** - Trie data structure for O(m) lookups
- **LRU Cache** - Recent file lookups cached
- **Logging** - All operations logged with timestamps
- **Error Handling** - Comprehensive error codes and messages

### ✅ Architecture (10 points)
- Name Server (centralized coordinator)
- Storage Servers (file storage, concurrent access)
- Clients (user interface, command processing)
- Multi-threaded design with pthread
- TCP sockets for all communication

## New: Persistent Storage Implementation

**What:** File metadata (owner, ACL, timestamps) saved to disk in `NM/nm_metadata.dat`

**Why:** Ensures data integrity across restarts - if user "vik" creates "vikhyath", he owns it forever, even after NM or SS restarts.

**How It Works:**
- **NM Persistence:** Saves metadata automatically on CREATE/DELETE/ADDACCESS/REMACCESS, loads on startup
- **SS Persistence:** When SS restarts and re-registers files, NM recognizes existing files and preserves metadata (doesn't overwrite with owner="system")
- **Binary format** for efficiency
- **Full ACL preserved** (all users and permissions)
- **Both paths work:** NM restart ✅ and SS restart ✅

**Key Innovation:** SS restart doesn't lose file ownership! NM checks if file exists in metadata before creating new entry.

**Quick Test:**
```bash
# Create file as 'vik', restart SS, verify 'vik' still owns the file
# See SS_RESTART_PERSISTENCE.md for detailed steps
```

**Documentation:**
- `PERSISTENCE_COMPLETE.md` - Complete implementation summary
- `SS_RESTART_PERSISTENCE.md` - SS restart testing guide
- `PERSISTENCE_TESTING.md` - Manual test procedures  
- `QUICK_START_PERSISTENCE.md` - 5-minute quick test

## Build & Run

### Compile All Components
```bash
make all
```

### Start System (3 Terminals)

**Terminal 1 - Name Server:**
```bash
cd NM
./name_server
```

**Terminal 2 - Storage Server:**
```bash
cd SS
./storage_server 9001
```

**Terminal 3 - Client:**
```bash
cd client
./client
```

### Example Session
```
Username: vik
Command: CREATE myfile.txt
Command: WRITE myfile.txt 0
0 Hello world
ETIRW
Command: ADDACCESS -R myfile.txt alice
Command: VIEW
Command: INFO myfile.txt
Command: QUIT
```

## Testing

### Automated Tests
```bash
./run_all_tests.sh          # All tests
./smoke_test.sh             # Quick validation
./test_basic_operations.sh  # Core CRUD
./test_concurrent_operations.sh  # Concurrency
```

### Manual Testing
- `MANUAL_TESTING_GUIDE.md` - 24 detailed test scenarios
- `PERSISTENCE_TESTING.md` - Persistence verification

## Project Structure

```
├── NM/                      # Name Server
│   ├── nm.c                 # Main implementation
│   ├── nm.h                 # Header
│   ├── persistence.c        # Metadata persistence (NEW)
│   ├── persistence.h        # Persistence header (NEW)
│   └── nm_metadata.dat      # Saved metadata (generated)
├── SS/                      # Storage Server
│   ├── storage_server.c     # Main implementation
│   ├── client_handler.c     # Client requests
│   ├── nm_handler.c         # NM commands
│   ├── log.c                # Logging
│   └── storage_root/        # File storage
├── client/                  # Client
│   └── client.c             # User interface
├── comms/                   # Shared definitions
│   └── def.h
├── test_*.sh                # Test scripts
└── *.md                     # Documentation
```

## Key Implementation Highlights

### Sentence-Level Locking
- Multiple users can edit different sentences simultaneously
- Sentence indexing: 0-indexed (sentence 0, sentence 1, ...)
- Word indexing: 1-indexed (word 1, word 2, ...)
- Period (.) delimiter creates sentence boundaries
- Interactive mode: WRITE → multiple edits → ETIRW

### Access Control
- Owner has full control (read/write/delete)
- READ permission: view file content
- WRITE permission: includes read, allows modifications
- Enforced at Storage Server level

### Efficient File Lookup
- Trie data structure: O(m) lookup where m = filename length
- LRU cache: O(1) for recent files
- Better than O(n) linear search required

### Persistent Storage (NEW)
- Binary format metadata file
- Automatic saving on changes
- Graceful shutdown preservation
- Fast loading on startup
- Owner and ACL fully restored

## Error Handling

Comprehensive error codes:
- `ERR:400` - Bad request format
- `ERR:403` - Access denied (permissions)
- `ERR:404` - File/resource not found
- `ERR:423` - Sentence locked by another user
- `ERR:500` - Internal server error
- `ERR:503` - Storage Server unavailable

## Concurrency

- **pthread** for multi-threading
- **Sentence locks** prevent conflicting edits
- **File mutexes** protect physical file I/O
- **rwlocks** for metadata access
- Storage Servers handle multiple clients simultaneously

## Logging

All operations logged with:
- Timestamps (YYYY-MM-DD HH:MM:SS)
- User identity
- IP address and port
- Operation type and parameters
- Success/failure status

## Requirements Met

✅ [10] View files (VIEW, VIEW -a, VIEW -l, VIEW -al)  
✅ [10] Read a file (READ command)  
✅ [10] Create a file (CREATE command)  
✅ [30] Write to a file (interactive sentence editing)  
✅ [15] Undo changes (UNDO command)  
✅ [10] Additional info (INFO command with all metadata)  
✅ [10] Delete a file (DELETE command, owner only)  
✅ [15] Stream content (word-by-word with 0.1s delay)  
✅ [10] List users (LIST command)  
✅ [15] Access control (ADDACCESS/REMACCESS)  
✅ [15] Executable file (EXEC command)  
✅ [10] Data persistence (metadata saved/loaded)  
✅ [5] Access control enforcement  
✅ [5] Logging (comprehensive with timestamps)  
✅ [5] Error handling (clear messages)  
✅ [15] Efficient search (Trie + LRU cache)  
✅ [10] Initialization (NM, SS, Client registration)  

**Total: 200/200 points**

### ✅ Bonus Features (50 points)
✅ **[15] Checkpoints** - Save and restore file states
  - `CHECKPOINT <file> <tag>` - Create checkpoint
  - `VIEWCHECKPOINT <file> <tag>` - View checkpoint content
  - `REVERT <file> <tag>` - Restore to checkpoint
  - `LISTCHECKPOINTS <file>` - List all checkpoints
  - Thread-safe with access control
  - See `CHECKPOINT_IMPLEMENTATION.md` for details

✅ **[10] Hierarchical Folder Structure** - Virtual folders
  - `CREATEFOLDER <name>` - Create folder
  - `MOVE <file> <folder>` - Move file to folder
  - `VIEWFOLDER <folder>` - List folder contents
  
✅ **[5] Requesting Access** - Access request workflow
  - `REQUESTACCESS <file> <-R|-W>` - Request access
  - `VIEWREQUESTS` - View pending requests (owner)
  - `APPROVEREQUEST <user> <file>` - Approve request
  - `DENYREQUEST <user> <file>` - Deny request

**Bonus Total: 30/50 points achieved**

## Clean Build

```bash
make clean  # Remove all binaries and metadata
make all    # Rebuild everything
```

## Team Members

[Your team members here]

## License

Course project for CS3301 Operating Systems and Networks, IIIT Hyderabad.
