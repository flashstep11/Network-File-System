# Network File System (NFS)

**Distributed File System Implementation** | CS3301 Operating Systems & Networks | IIIT Hyderabad

A multi-user distributed file system with concurrent editing, access control, and persistent storage.

---

## Features

- **Concurrent Editing** - Multiple users can edit different sentences simultaneously with sentence-level locking
- **Persistent Storage** - Files and metadata survive system restarts
- **Access Control** - Owner-based permissions with READ/WRITE granularity
- **Efficient Search** - Trie (O(m)) + LRU Cache (O(1)) for fast file lookups
- **Checkpoint System** - Save and restore file versions
- **Direct Communication** - Clients connect directly to Storage Servers for data transfer
- **Interactive CLI** - Command history and auto-completion with readline
- **Hierarchical Folders** - Virtual folder structure for organizing files
- **Multi-device Support** - Client can connect to remote NM via IP parameter
- **Comprehensive Logging** - All operations logged with timestamps and details

---

## Unique Features

### REPLACE Command
Interactive word-level replacement without rewriting entire sentences. Users can:
- Replace specific words by index
- Delete words by passing empty strings
- Maintain concurrent editing with sentence-level locks

```bash
nfs:user> REPLACE file.txt 0
replace> 2 updated      # Replace word at index 2
replace> 5 ""           # Delete word at index 5
replace> ECALPER        # Save and exit
```

### DIFF Command
Compare two checkpoint versions line-by-line with Git-style output:
- Shows additions with `+` prefix
- Shows deletions with `-` prefix
- Helpful for tracking changes between versions

```bash
nfs:user> DIFF file.txt v1 v2
--- v1
+++ v2
  This is unchanged.
- Old content
+ New content
```

### Hierarchical Folders
Virtual folder structure maintained by Name Server:
- Create logical folders (`CREATEFOLDER`)
- Move files between folders (`MOVE`)
- List folder contents (`VIEWFOLDER`)
- Browse all folders (`LISTFOLDERS`)

```bash
nfs:user> CREATEFOLDER project
nfs:user> MOVE notes.txt project
nfs:user> VIEWFOLDER project
```

### Command History
Readline integration provides shell-like experience:
- Navigate previous commands with UP/DOWN arrows
- Auto-completion support
- Session-persistent history
- Significantly improves usability during testing

---

## Architecture

```
Name Server (NM)          Storage Servers          Clients
   Port 8080                Ports 8081+
      |                         |
      |   File metadata         |  File storage
      |   Access control        |  Concurrent ops
      |   Routing info          |  Sentence locks
      |_________________________|
               |
               Direct data connection
```

---

## Quick Start

### Build
```bash
make clean && make all
```

### Run (3 terminals)

**Terminal 1:**
```bash
cd NM && ./name_server
```

**Terminal 2:**
```bash
cd SS && ./storage_server 8081
```

**Terminal 3:**
```bash
cd client && ./client
```

---

## Commands

### File Operations
```
CREATE <file>                        Create file
READ <file>                          Read content
DELETE <file>                        Delete file (owner only)
INFO <file>                          Show metadata
VIEW [-a] [-l]                       List files
```

### Editing
```
WRITE <file> <sentence>              Interactive editing
  <word_idx> <content>               Update words
  ETIRW                              Save & exit

UNDO <file>                          Revert changes
```

### Access Control
```
ADDACCESS <-R|-W> <file> <user>      Grant access
REMACCESS <file> <user>              Revoke access
```

### Advanced
```
STREAM <file>                        Stream content word-by-word
EXEC <file>                          Execute as shell script
CHECKPOINT <file> <tag>              Save version
REVERT <file> <tag>                  Restore version
LISTCHECKPOINTS <file>               List versions
DIFF <file> <tag1> <tag2>            Compare versions
LIST                                 Show users
```

### Folders
```
CREATEFOLDER <folder>                Create virtual folder
MOVE <file> <folder>                 Move file to folder
VIEWFOLDER <folder>                  List folder contents
LISTFOLDERS                          Show all folders
```

### Word-Level Editing
```
REPLACE <file> <sentence>            Interactive replacement
  <word_idx> <content>               Replace word
  <word_idx> ""                      Delete word
  ECALPER                            Save & exit
```

---

## Implementation

### Efficient Lookup
- **Trie:** O(m) search (m = path length)
- **LRU Cache:** O(1) for hot files
- Combined for optimal performance

### Sentence-Level Locking
- Users edit different sentences concurrently
- Sentence lock: held during editing
- File mutex: only during disk I/O

### Persistence
- Metadata: `nm_metadata.dat` (binary)
- Files: `storage_root_<port>/`
- Survives restarts

### Direct SS Communication
- Client queries NM for file location
- NM returns SS IP and port
- Client connects to SS directly
- Avoids NM bottleneck

---

## Project Structure

```
NM/                          Name Server (Central Coordinator)
  nm.c                       • Main implementation (~2800 lines)
                             • Trie for file lookup (O(m) complexity)
                             • LRU Cache for hot files (O(1) access)
                             • Access Control Lists (ACL) management
                             • Client/SS registry and routing
                             • Command handlers (CREATE/DELETE/INFO/VIEW/LIST)
                             • Heartbeat monitoring for SS health
                             • Request queue for access permissions
  
  nm.h                       • Data structures (Trie, Cache, FileMetadata, ACL)
                             • Function prototypes
                             • Constants and configuration
  
  persistence.c              • Save metadata to nm_metadata.dat (binary format)
                             • Load metadata on startup
                             • Preserves ownership and ACLs across restarts
  
  persistence.h              • Persistence function declarations

SS/                          Storage Server (File Operations)
  storage_server.c           • Main server initialization
                             • Registration with Name Server
                             • File I/O utilities
                             • Sentence lock manager (concurrent editing)
                             • Checkpoint storage (in-memory linked lists)
                             • Helper functions for file operations
  
  client_handler.c           • Client request handlers (~1700 lines)
                             • READ/WRITE/STREAM/UNDO operations
                             • REPLACE command (word-level editing)
                             • CHECKPOINT/REVERT/DIFF/LISTCHECKPOINTS
                             • Permission validation via NM
                             • Sentence locking for concurrent access
  
  client_handler.h           • Client handler function declarations
  
  nm_handler.c               • NM command handlers (~600 lines)
                             • CREATE/DELETE file commands from NM
                             • MOVE/COPY operations for files
                             • GET_CONTENT for EXEC command
                             • Folder operations support
  
  nm_handler.h               • NM handler function declarations
  
  log.c                      • Thread-safe logging system
                             • Timestamped operation logs
  
  log.h                      • Logging function declarations
  
  defs.h                     • Shared constants and definitions
                             • Sentence lock structures
                             • Checkpoint structures
                             • File mutex management
                             • Helper macros

client/                      Client Interface
  client.c                   • Interactive CLI (~900 lines)
                             • Readline integration (command history)
                             • Command parser and dispatcher
                             • Direct SS connections for READ/WRITE/STREAM
                             • NM queries for CREATE/DELETE/INFO/VIEW
                             • Interactive modes (WRITE/REPLACE)
                             • Multi-device support (NM IP parameter)

comms/                       Shared Protocol Definitions
  def.h                      • Message types (MSG_*)
                             • Error codes (ERR_400, ERR_403, etc.)
                             • Packet header structures
```

---

## Example

```bash
$ ./client
Username: alice

nfs:alice> CREATE notes.txt
nfs:alice> WRITE notes.txt 0
edit> 0 Hello world
edit> ETIRW
nfs:alice> CHECKPOINT notes.txt v1
nfs:alice> ADDACCESS -R notes.txt bob
nfs:alice> INFO notes.txt
File: notes.txt
Owner: alice (RW), bob (R)
nfs:alice> STREAM notes.txt
Hello world
```

---

## Technical Details

**Concurrency:**
- Sentence locks per (file, sentence)
- File mutexes per file
- RW locks for metadata

**Error Codes:**
- 400 (Bad request), 403 (Access denied)
- 404 (Not found), 423 (Locked)
- 500 (Internal), 503 (SS offline)

**Storage:**
- Files: `storage_root_<port>/`
- Metadata: `nm_metadata.dat`
- Logs: `*.log`

---

## Team

Vikhyath Pattipaty and Havish Balaga [Freedom Fighters] | CS3301 OSN | IIIT Hyderabad
