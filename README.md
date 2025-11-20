# Network File System (NFS) - Implementation & Evaluation Guide# Network File System (NFS) - Implementation & Evaluation Guide# Network File System (NFS) - Implementation Guide# Network File System (NFS) - Distributed Document Collaboration System



> **Course Project | CS3301 Operating Systems and Networks**

> This document is structured to assist with evaluations by explaining the **Where**, **How**, and **Why** of every major component.

> **Course Project | CS3301 Operating Systems and Networks**

---

> This document is structured to assist with evaluations by explaining the **Where**, **How**, and **Why** of every major component.

## 🌟 Unique Factors (Creative & Bonus Features)

> **Evaluation & Implementation Handbook**

This section highlights the custom features implemented beyond the basic specification.

---

### 1. Command History & Auto-Completion

*   **Where:** `client/client.c` (using `readline/readline.h` and `readline/history.h`)> This document is designed to help you understand **exactly how the system works**, where features are implemented, and the design decisions behind them.> **A production-grade distributed file system with concurrent editing, checkpointing, and granular access control**# Network File System (NFS) - Course Project

*   **How:** 

    *   Integrated the GNU Readline library instead of standard `scanf`/`fgets`.## 🌟 Unique Factors (Creative & Bonus Features)

    *   Configured history management to store previous commands in memory during the session.

*   **Why:** 

    *   **Usability:** Allows users to press **Up/Down arrows** to recall complex commands (like `WRITE file.txt 10`) instead of retyping them.

    *   **Efficiency:** Drastically speeds up testing and usage.This section highlights the custom features implemented beyond the basic specification.



### 2. REPLACE Command (Granular Editing)---

*   **Where:** `SS/client_handler.c` (`handle_replace_command`)

*   **How:**### 1. Command History & Auto-Completion

    *   Client sends `REPLACE <file> <sentence_idx>`.

    *   SS acquires a **Sentence Lock** for that specific sentence.*   **Where:** `client/client.c` (using `readline/readline.h` and `readline/history.h`)

    *   User sends `<word_idx> <new_word>` pairs.

    *   SS parses the existing sentence, replaces the specific word at `word_idx`, and reconstructs the string in real-time.*   **How:** 

*   **Why:**

    *   **Precision:** Standard `WRITE` is append-heavy. `REPLACE` allows fixing typos or changing specific data points without rewriting the entire sentence.    *   Integrated the GNU Readline library instead of standard `scanf`/`fgets`.## 🚀 Unique Factors (Bonus & Creative Features)**Course Project** | CS3301 Operating Systems and Networks | IIIT Hyderabad  A distributed network file system with persistent storage, access control, and concurrent operations.

    *   **Concurrency:** Because it uses sentence locks, User A can replace a word in Sentence 0 while User B appends to Sentence 1.

    *   Configured history management to store previous commands in memory during the session.

### 3. DIFF Command (Version Comparison)

*   **Where:** `SS/storage_server.c` (`diff_checkpoints`)*   **Why:** 

*   **How:**

    *   Takes two checkpoint tags as input.    *   **Usability:** Allows users to press **Up/Down arrows** to recall complex commands (like `WRITE file.txt 10`) instead of retyping them.

    *   Retrieves the full content strings from the in-memory checkpoint linked list.

    *   Performs a line-by-line string comparison.    *   **Efficiency:** Drastically speeds up testing and usage.These are the standout features that go beyond the basic specification.**Specification:** https://karthikv1392.github.io/cs3301_osn/course_project/

    *   Generates a patch-style output: lines starting with `+` are additions, `-` are deletions.

*   **Why:**

    *   **Visibility:** A version control system is useless if you can't see *what* changed. This provides Git-like visibility into file history.

### 2. REPLACE Command (Granular Editing)

### 4. Hierarchical Folders (LISTFOLDERS)

*   **Where:** `NM/nm.c` (`handle_createfolder`, `handle_listfolders`, `handle_move`)*   **Where:** `SS/client_handler.c` (`handle_replace_command`)

*   **How:**

    *   **Logical Structure:** The Name Server's Trie maintains the folder hierarchy (e.g., `project/docs/note.txt`).*   **How:**### 1. Command History & Auto-Completion## Project Specification

    *   **Physical Storage:** The Storage Server stores files in a flat or semi-flat structure, but the NM maps the logical paths.

    *   `LISTFOLDERS` performs a recursive traversal of the Trie to display the tree structure.    *   Client sends `REPLACE <file> <sentence_idx>`.

*   **Why:**

    *   **Organization:** Prevents the root directory from becoming cluttered with hundreds of files.    *   SS acquires a **Sentence Lock** for that specific sentence.*   **Where:** `client/client.c` (lines using `readline/readline.h`)

    *   **Namespace Management:** Allows files with the same name to exist in different folders.

    *   User sends `<word_idx> <new_word>` pairs.

---

    *   SS parses the existing sentence, replaces the specific word at `word_idx`, and reconstructs the string in real-time.*   **How:** Integrated the GNU Readline library.---

## 🛠️ Implementation Deep Dive (The "Viva" Section)

*   **Why:**

### 1. Efficient Search (Trie + LRU Cache)

**"How does the Name Server find files so quickly?"**    *   **Precision:** Standard `WRITE` is append-heavy. `REPLACE` allows fixing typos or changing specific data points without rewriting the entire sentence.*   **Why:** To provide a true shell-like experience. Users can use **Up/Down arrows** to navigate previous commands, making testing and usage significantly faster than standard `scanf/fgets`.



*   **Where:** `NM/nm.c` (Functions: `trie_insert`, `trie_search`, `cache_get`, `cache_put`)    *   **Concurrency:** Because it uses sentence locks, User A can replace a word in Sentence 0 while User B appends to Sentence 1.

*   **How:**

    *   **Trie (Prefix Tree):** Every file path is stored as a path in a tree. Searching takes **O(m)** time (m = length of filename), independent of the number of files.Full documentation: https://karthikv1392.github.io/cs3301_osn/course_project/

    *   **LRU Cache:** A doubly-linked list stores the `K` most recently accessed file metadata pointers. Searching here is **O(1)**.

*   **Why:**### 3. DIFF Command (Version Comparison)

    *   **Scalability:** Linear search O(N) becomes too slow when the system has 100,000 files. O(m) is constant relative to system size.

    *   **Locality of Reference:** Users tend to access the same few files repeatedly. The Cache serves these instantly.*   **Where:** `SS/storage_server.c` (`diff_checkpoints`)### 2. REPLACE Command (Granular Editing)



### 2. Concurrency & Locking Strategy*   **How:**

**"How do you prevent race conditions when multiple users edit?"**

    *   Takes two checkpoint tags as input.*   **Where:** `SS/client_handler.c` (`handle_replace_command`)## 🎯 Project Overview

*   **Where:** `SS/storage_server.c` (`acquire_sentence_lock`, `release_sentence_lock`)

*   **How:**    *   Retrieves the full content strings from the in-memory checkpoint linked list.

    *   **Sentence Locks:** A linked list of active locks `{filename, sentence_id, client_id}`.

    *   **File Mutex:** A standard `pthread_mutex` per file.    *   Performs a line-by-line string comparison.*   **How:** 

    *   **The Strategy:**

        1.  When a user wants to edit Sentence X, we check the **Sentence Lock** list.    *   Generates a patch-style output: lines starting with `+` are additions, `-` are deletions.

        2.  If free, we grant the lock. The user can take their time typing.

        3.  Only when the user hits "Save" (`ETIRW`), we acquire the **File Mutex** for the split-second needed to write to disk.*   **Why:**    *   Client sends `REPLACE <file> <sentence_index>`.## Key Features Implemented

*   **Why:**

    *   **Granularity:** Locking the entire file for the duration of a user's edit session (which could be minutes) would block everyone else. Sentence locking allows high concurrency.    *   **Visibility:** A version control system is useless if you can't see *what* changed. This provides Git-like visibility into file history.

    *   **Data Safety:** The File Mutex ensures that even if two threads try to write to disk at the exact same microsecond, the OS file buffer doesn't get corrupted.

    *   SS locks that specific sentence.

### 3. Client-Server Communication Flow

**"Does the Name Server become a bottleneck?"**### 4. Hierarchical Folders (LISTFOLDERS)



*   **Where:** `client/client.c` and `NM/nm.c`*   **Where:** `NM/nm.c` (`handle_createfolder`, `handle_listfolders`, `handle_move`)    *   User sends `<word_index> <new_word>` updates.This is a fully-functional **distributed network file system** similar to Google Docs, implementing:

*   **How:**

    *   **Control Plane (NM):** Handles metadata, permissions, and lookup. Messages are small (e.g., "Where is file.txt?").*   **How:**

    *   **Data Plane (SS):** Handles actual file content. Messages are large (e.g., "Here is the 1GB video file").

    *   **Flow:** Client asks NM -> NM checks ACL & returns SS IP -> Client connects directly to SS.    *   **Logical Structure:** The Name Server's Trie maintains the folder hierarchy (e.g., `project/docs/note.txt`).    *   SS reconstructs the sentence in real-time.

*   **Why:**

    *   **Performance:** If all file data passed through the NM, the NM would be overwhelmed. Direct Client-SS connection distributes the load.    *   **Physical Storage:** The Storage Server stores files in a flat or semi-flat structure, but the NM maps the logical paths.



### 4. Persistence & Crash Recovery    *   `LISTFOLDERS` performs a recursive traversal of the Trie to display the tree structure.*   **Why:** Standard `WRITE` appends or overwrites. `REPLACE` allows fixing a typo in the middle of a paragraph without rewriting the whole thing.- **Multi-user concurrent editing** with sentence-level locking### ✅ Core Functionality (150 points)

**"What happens if the server restarts?"**

*   **Why:**

*   **Where:** `NM/persistence.c` and `SS/storage_server.c`

*   **How:**    *   **Organization:** Prevents the root directory from becoming cluttered with hundreds of files.

    *   **NM Persistence:** On every metadata change (CREATE, ADDACCESS), the NM serializes the Trie and ACLs to a disk file (`nm_metadata.dat`). On startup, it reloads this.

    *   **SS Persistence:** Files are stored as standard OS files. On startup, SS scans its directory and re-registers available files with the NM.    *   **Namespace Management:** Allows files with the same name to exist in different folders.

*   **Why:**

    *   **Reliability:** A distributed file system must survive power outages or crashes without losing data or ownership information.### 3. DIFF Command (Version Comparison)- **Persistent storage** with crash recovery- File operations: CREATE, READ, WRITE, DELETE, INFO



------



## 🔄 Step-by-Step Workflow: The `WRITE` Command*   **Where:** `SS/storage_server.c` (`diff_checkpoints`)



1.  **Client:** User types `WRITE doc.txt 1`.## 🛠️ Implementation Deep Dive (The "Viva" Section)

2.  **Client:** Sends `WRITE doc.txt` request to **Name Server (NM)**.

3.  **NM:** *   **How:** - **Fine-grained access control** (owner, read, write permissions)- Interactive WRITE mode with sentence-level locking

    *   Checks **LRU Cache** then **Trie** to find `doc.txt`.

    *   Checks **ACL**: Does this user have WRITE permission?### 1. Efficient Search (Trie + LRU Cache)

    *   Returns **Storage Server (SS)** IP and Port to Client.

4.  **Client:** Connects directly to **SS**.**"How does the Name Server find files so quickly?"**    *   Retrieves content of two checkpoints from memory.

5.  **Client:** Sends `WRITE_REQ doc.txt sentence_1` to SS.

6.  **SS:** 

    *   Checks **Sentence Lock List**. Is Sentence 1 locked?

    *   If No: Adds lock for this user. Sends `ACK`.*   **Where:** `NM/nm.c` (Functions: `trie_insert`, `trie_search`, `cache_get`, `cache_put`)    *   Performs a line-by-line string comparison.- **Checkpoint system** for version control- STREAM command (word-by-word with 0.1s delay)

    *   If Yes: Sends `ERR_LOCKED`.

7.  **Client:** Enters interactive mode. User types words.*   **How:**

8.  **Client:** User types `ETIRW` (Write backwards) to finish.

9.  **Client:** Sends all data to SS.    *   **Trie (Prefix Tree):** Every file path is stored as a path in a tree. Searching takes **O(m)** time (m = length of filename), independent of the number of files.    *   Outputs lines prefixed with `+` (added) or `-` (removed).

10. **SS:** 

    *   Acquires **File Mutex**.    *   **LRU Cache:** A doubly-linked list stores the `K` most recently accessed file metadata pointers. Searching here is **O(1)**.

    *   Reads file, updates Sentence 1, writes back to disk.

    *   Releases **File Mutex**.*   **Why:***   **Why:** Essential for a version control system. Users need to know *what* changed between versions, not just that a version exists.- **Efficient file lookup** using Trie + LRU cache (O(m) complexity)- VIEW command with -a (all) and -l (details) flags

    *   Removes **Sentence Lock**.

    *   Sends `SUCCESS`.    *   **Scalability:** Linear search O(N) becomes too slow when the system has 100,000 files. O(m) is constant relative to system size.



---    *   **Locality of Reference:** Users tend to access the same few files repeatedly. The Cache serves these instantly.



## 📂 Project Structure Map



*   **`NM/` (Name Server)**### 2. Concurrency & Locking Strategy### 4. Hierarchical Folders (LISTFOLDERS)- **Hierarchical folder structure** for organization- Access control: ADDACCESS, REMACCESS

    *   `nm.c`: Main entry point. Handles Client/SS connections.

    *   `trie.c` / `cache.c`: Search data structures (often inside nm.c).**"How do you prevent race conditions when multiple users edit?"**

    *   `persistence.c`: Saving/Loading metadata.

*   **`SS/` (Storage Server)***   **Where:** `NM/nm.c` (`handle_createfolder`, `handle_listfolders`)

    *   `storage_server.c`: Main entry point. File I/O.

    *   `client_handler.c`: Handles READ, WRITE, REPLACE, STREAM.*   **Where:** `SS/storage_server.c` (`acquire_sentence_lock`, `release_sentence_lock`)

    *   `nm_handler.c`: Handles CREATE, DELETE, COPY instructions from NM.

*   **`client/` (Client)***   **How:***   **How:** - **Real-time streaming** and asynchronous operations- EXEC command (execute file as shell commands)

    *   `client.c`: User interface, command parsing, network logic.

*   **`comms/`**    *   **Sentence Locks:** A linked list of active locks `{filename, sentence_id, client_id}`.

    *   `def.h`: Common definitions (Message types, Error codes, Structs).

    *   **File Mutex:** A standard `pthread_mutex` per file.    *   NM maintains a logical directory structure in the Trie.

---

    *   **The Strategy:**

## 💻 Build & Run

        1.  When a user wants to edit Sentence X, we check the **Sentence Lock** list.    *   Files are stored physically flat on SS but logically nested in NM.- UNDO support (one level per file)

1.  **Compile:**

    ```bash        2.  If free, we grant the lock. The user can take their time typing.

    make all

    ```        3.  Only when the user hits "Save" (`ETIRW`), we acquire the **File Mutex** for the split-second needed to write to disk.*   **Why:** To allow organizing files into projects/categories, preventing namespace pollution in the root directory.



2.  **Run Name Server:***   **Why:**

    ```bash

    cd NM && ./name_server    *   **Granularity:** Locking the entire file for the duration of a user's edit session (which could be minutes) would block everyone else. Sentence locking allows high concurrency.---- LIST users command

    ```

    *   **Data Safety:** The File Mutex ensures that even if two threads try to write to disk at the exact same microsecond, the OS file buffer doesn't get corrupted.

3.  **Run Storage Server:**

    ```bash---

    cd SS && ./storage_server 9001

    ```### 3. Client-Server Communication Flow



4.  **Run Client:****"Does the Name Server become a bottleneck?"**

    ```bash

    cd client && ./client

    ```

*   **Where:** `client/client.c` and `NM/nm.c`## 🛠️ Implementation Deep Dive (For Evaluation)

*   **How:**

    *   **Control Plane (NM):** Handles metadata, permissions, and lookup. Messages are small (e.g., "Where is file.txt?").## 📊 Implementation Score Breakdown### ✅ System Requirements (40 points)

    *   **Data Plane (SS):** Handles actual file content. Messages are large (e.g., "Here is the 1GB video file").

    *   **Flow:** Client asks NM -> NM checks ACL & returns SS IP -> Client connects directly to SS.### 1. Efficient Search (Trie + LRU Cache)

*   **Why:**

    *   **Performance:** If all file data passed through the NM, the NM would be overwhelmed. Direct Client-SS connection distributes the load.**"How do you find files quickly?"**- **Persistent Storage** - Files and metadata survive restarts



### 4. Persistence & Crash Recovery

**"What happens if the server restarts?"**

*   **Where:** `NM/nm.c` (lines 89-285)### ✅ Core Functionalities (150/150 points)- **Access Control** - Enforced read/write permissions

*   **Where:** `NM/persistence.c` and `SS/storage_server.c`

*   **How:***   **Implementation:**

    *   **NM Persistence:** On every metadata change (CREATE, ADDACCESS), the NM serializes the Trie and ACLs to a disk file (`nm_metadata.dat`). On startup, it reloads this.

    *   **SS Persistence:** Files are stored as standard OS files. On startup, SS scans its directory and re-registers available files with the NM.    *   **Trie (Prefix Tree):** Used for main storage. Lookup is **O(m)** (m = filename length), which is much faster than O(n) array search.- **Efficient Search** - Trie data structure for O(m) lookups

*   **Why:**

    *   **Reliability:** A distributed file system must survive power outages or crashes without losing data or ownership information.    *   **LRU Cache:** A doubly-linked list stores the most recently accessed files. Lookup is **O(1)**.



---*   **Why:** In a distributed system with thousands of files, searching an array for every request would be a bottleneck. The Cache handles hot files instantly.| Feature | Points | Status | Implementation Details |- **LRU Cache** - Recent file lookups cached



## 🔄 Step-by-Step Workflow: The `WRITE` Command



1.  **Client:** User types `WRITE doc.txt 1`.### 2. Concurrency & Sentence Locking|---------|--------|--------|------------------------|- **Logging** - All operations logged with timestamps

2.  **Client:** Sends `WRITE doc.txt` request to **Name Server (NM)**.

3.  **NM:** **"How do multiple users edit the same file?"**

    *   Checks **LRU Cache** then **Trie** to find `doc.txt`.

    *   Checks **ACL**: Does this user have WRITE permission?| **VIEW** (list files) | 10 | ✅ | With `-a` (all), `-l` (details), `-al` flags |- **Error Handling** - Comprehensive error codes and messages

    *   Returns **Storage Server (SS)** IP and Port to Client.

4.  **Client:** Connects directly to **SS**.*   **Where:** `SS/storage_server.c` (`acquire_sentence_lock`, `is_file_being_edited`)

5.  **Client:** Sends `WRITE_REQ doc.txt sentence_1` to SS.

6.  **SS:** *   **Implementation:**| **READ** (file content) | 10 | ✅ | Direct SS connection, permission-checked |

    *   Checks **Sentence Lock List**. Is Sentence 1 locked?

    *   If No: Adds lock for this user. Sends `ACK`.    *   **File Mutex:** Protects `fopen`/`fwrite` operations to prevent data corruption on disk.

    *   If Yes: Sends `ERR_LOCKED`.

7.  **Client:** Enters interactive mode. User types words.    *   **Sentence Locks:** A linked list of active locks `{filename, sentence_id, user_id}`.| **CREATE** (new file) | 10 | ✅ | Owner auto-assigned, empty file creation |### ✅ Architecture (10 points)

8.  **Client:** User types `ETIRW` (Write backwards) to finish.

9.  **Client:** Sends all data to SS.    *   **Logic:** User A can edit Sentence 0 while User B edits Sentence 1.

10. **SS:** 

    *   Acquires **File Mutex**.*   **Why:** | **WRITE** (edit file) | 30 | ✅ | **Interactive mode**, sentence locking, word-level editing |- Name Server (centralized coordinator)

    *   Reads file, updates Sentence 1, writes back to disk.

    *   Releases **File Mutex**.    *   **Granularity:** Locking the whole file for editing (like standard OS locking) kills collaboration.

    *   Removes **Sentence Lock**.

    *   Sends `SUCCESS`.    *   **Performance:** We only lock the *file mutex* for the split second of writing to disk, not during the user's "thinking time".| **UNDO** (revert changes) | 15 | ✅ | File-level undo, `.bak` backup system |- Storage Servers (file storage, concurrent access)



---



## 📂 Project Structure Map### 3. Client-SS Direct Communication| **INFO** (file metadata) | 10 | ✅ | Owner, size, timestamps, ACL, last access |- Clients (user interface, command processing)



*   **`NM/` (Name Server)****"Does all data go through the Name Server?"**

    *   `nm.c`: Main entry point. Handles Client/SS connections.

    *   `trie.c` / `cache.c`: Search data structures (often inside nm.c).| **DELETE** (remove file) | 10 | ✅ | Owner-only, metadata cleanup |- Multi-threaded design with pthread

    *   `persistence.c`: Saving/Loading metadata.

*   **`SS/` (Storage Server)***   **Where:** `client/client.c` (`connect_to_ss`) and `NM/nm.c` (`handle_read`, `handle_write`)

    *   `storage_server.c`: Main entry point. File I/O.

    *   `client_handler.c`: Handles READ, WRITE, REPLACE, STREAM.*   **Implementation:**| **STREAM** (live content) | 15 | ✅ | Word-by-word with 0.1s delay + STOP packet |- TCP sockets for all communication

    *   `nm_handler.c`: Handles CREATE, DELETE, COPY instructions from NM.

*   **`client/` (Client)**    1.  Client asks NM for file info.

    *   `client.c`: User interface, command parsing, network logic.

*   **`comms/`**    2.  NM returns **SS IP and Port**.| **LIST** (users) | 10 | ✅ | All connected users display |

    *   `def.h`: Common definitions (Message types, Error codes, Structs).

    3.  Client `connect()`s directly to SS.

---

*   **Why:** | **Access Control** | 15 | ✅ | ADDACCESS/REMACCESS with -R/-W flags |## New: Persistent Storage Implementation

## 💻 Build & Run

    *   **Scalability:** If NM handled file data, it would crash under load.

1.  **Compile:**

    ```bash    *   **Speed:** Direct connection reduces latency.| **EXEC** (run as commands) | 15 | ✅ | Shell execution on NM, output piped to client |

    make all

    ```



2.  **Run Name Server:**### 4. Checkpoints (In-Memory Versioning)**What:** File metadata (owner, ACL, timestamps) saved to disk in `NM/nm_metadata.dat`

    ```bash

    cd NM && ./name_server**"How are versions stored?"**

    ```

### ✅ System Requirements 

3.  **Run Storage Server:**

    ```bash*   **Where:** `SS/storage_server.c` (`create_checkpoint`, `revert_to_checkpoint`)

    cd SS && ./storage_server 9001

    ```*   **Implementation:****Why:** Ensures data integrity across restarts - if user "vik" creates "vikhyath", he owns it forever, even after NM or SS restarts.



4.  **Run Client:**    *   Each file has a linked list of `CheckpointNode` structs in memory.

    ```bash

    cd client && ./client    *   Each node contains a full copy of the file content string at that time.| Requirement | Points | Status | Implementation |

    ```

*   **Why:** In-memory storage is extremely fast for saving/reverting. (Note: Non-persistent across SS restarts, but persistent across client disconnects).

|-------------|--------|--------|----------------|**How It Works:**

### 5. Access Control (ACL)

**"How are permissions enforced?"**| **Data Persistence** | 10 | ✅ | Files survive restarts, stored in `storage_root_<port>/` |- **NM Persistence:** Saves metadata automatically on CREATE/DELETE/ADDACCESS/REMACCESS, loads on startup



*   **Where:** `NM/nm.c` (`check_access`)| **Access Control** | 5 | ✅ | Enforced at SS level via NM permission check |- **SS Persistence:** When SS restarts and re-registers files, NM recognizes existing files and preserves metadata (doesn't overwrite with owner="system")

*   **Implementation:**

    *   `FileMetadata` struct contains a linked list of `AccessControlEntry`.| **Logging** | 5 | ✅ | Comprehensive logs with timestamps, IP, operations |- **Binary format** for efficiency

    *   Every request (READ, WRITE, DELETE) checks this list first.

*   **Why:** Security. Even if a client knows the SS IP, the SS validates the request with NM (or NM pre-validates before sending IP) to ensure unauthorized users can't touch data.| **Error Handling** | 5 | ✅ | Standardized error codes (400, 403, 404, 423, 500, 503) |- **Full ACL preserved** (all users and permissions)



---| **Efficient Search** | 15 | ✅ | **Trie O(m)** + **LRU Cache O(1)** |- **Both paths work:** NM restart ✅ and SS restart ✅



## 📖 Step-by-Step Workflow (How it works)



### Scenario: User A writes to `doc.txt`### ✅ Architecture Specification (10/10 points)**Key Innovation:** SS restart doesn't lose file ownership! NM checks if file exists in metadata before creating new entry.



1.  **Lookup:** Client sends `WRITE doc.txt` to **NM**.

2.  **Validation:** 

    *   NM checks Trie: Does file exist?| Component | Status | Implementation |**Quick Test:**

    *   NM checks ACL: Does User A have write permission?

3.  **Routing:** NM responds with `SS_IP: 127.0.0.1` and `SS_PORT: 9001`.|-----------|--------|----------------|```bash

4.  **Connection:** Client opens a TCP socket to **SS**.

5.  **Locking:** | **Initialization** | ✅ | SS→NM registration, Client→NM with username |# Create file as 'vik', restart SS, verify 'vik' still owns the file

    *   Client sends `WRITE_REQ doc.txt sentence_0`.

    *   SS checks `SentenceLock` list. If free, adds lock and sends `ACK`.| **Name Server** | ✅ | Central coordinator, metadata manager, ACL enforcement |# See SS_RESTART_PERSISTENCE.md for detailed steps

6.  **Editing:** 

    *   Client enters interactive loop.| **Storage Server** | ✅ | File storage, concurrent client handling, sentence locking |```

    *   Sends `0 Hello` -> SS updates buffer.

    *   Sends `1 World` -> SS updates buffer.| **Client** | ✅ | Interactive CLI with readline, command history |

7.  **Commit:** 

    *   Client sends `ETIRW`.**Documentation:**

    *   SS acquires **File Mutex**, writes buffer to disk, releases mutex.

    *   SS removes **Sentence Lock**.---- `PERSISTENCE_COMPLETE.md` - Complete implementation summary

8.  **Completion:** SS sends `WRITE_COMPLETE` to Client.

- `SS_RESTART_PERSISTENCE.md` - SS restart testing guide

---

## 🌟 Bonus Features

## 💻 Build & Run Instructions

- `QUICK_START_PERSISTENCE.md` - 5-minute quick test

### 1. Compile

```bash### ✅ [15 pts] Checkpoints - Version Control System

make all

```**Full checkpoint/restore functionality stored in-memory:**## Build & Run



### 2. Run Name Server (Terminal 1)

```bash

cd NM```bash### Compile All Components

./name_server

```CHECKPOINT <filename> <tag>              # Save current state```bash



### 3. Run Storage Server (Terminal 2)VIEWCHECKPOINT <filename> <tag>          # View saved versionmake all

```bash

cd SSREVERT <filename> <tag>                  # Restore to checkpoint```

./storage_server 9001

```LISTCHECKPOINTS <filename>               # List all checkpoints



### 4. Run Client (Terminal 3)```### Start System (3 Terminals)

```bash

cd client

./client

```**Implementation:****Terminal 1 - Name Server:**



---- In-memory linked list per file```bash



## 📂 Project Structure Map- Thread-safe with mutex lockscd NM



*   **`NM/`**: The Brain.- Blocked during active editing sessions./name_server

    *   `nm.c`: Main logic, Trie, Cache, ACLs.

    *   `persistence.c`: Saving metadata to disk.- Unlimited checkpoints per file```

*   **`SS/`**: The Muscle.

    *   `storage_server.c`: File I/O, Checkpoints.

    *   `client_handler.c`: Handling READ/WRITE/REPLACE requests.

*   **`client/`**: The Face.### ✅ [10 pts] DIFF - Compare Checkpoints (Creative Bonus)**Terminal 2 - Storage Server:**

    *   `client.c`: UI, Input parsing, Network handling.

*   **`comms/`**: The Language.**Innovative diff functionality between any two checkpoints:**```bash

    *   `def.h`: Shared message formats and error codes.

cd SS

---

```bash./storage_server 9001                    # Connect to localhost (127.0.0.1)

## 🧪 Testing Guide

DIFF <filename> <tag1> <tag2>            # Compare two versions# OR if NM is on different machine:

*   **`./run_all_tests.sh`**: Runs the full suite.

*   **`./smoke_test.sh`**: Quick check of basic features.```./storage_server 9001 <NM_IP_ADDRESS>   # e.g., ./storage_server 9001 192.168.1.100

*   **Manual Test:**

    1.  `CREATE test.txt````

    2.  `WRITE test.txt 0` -> Type words -> `ETIRW`

    3.  `CHECKPOINT test.txt v1`**Algorithm:**

    4.  `WRITE test.txt 0` -> Change words -> `ETIRW`

    5.  `DIFF test.txt v1 CURRENT` -> See changes.- Line-by-line diff with `+` (additions) and `-` (deletions)**Terminal 3 - Client:**


- Word-level comparison for granular changes```bash

- Efficient string matchingcd client

./client                    # Connect to localhost (127.0.0.1)

### ✅ [10 pts] Hierarchical Folders# OR if NM is on different machine:

**Virtual folder structure for file organization:**./client <NM_IP_ADDRESS>   # e.g., ./client 192.168.1.100

```

```bash

CREATEFOLDER <foldername>                # Create new folder### Multi-Device/Network Setup ✅

MOVE <filename> <foldername>             # Move file to folder

VIEWFOLDER <foldername>                  # List folder contents**Good News:** The system **already supports multi-device deployment**!

LISTFOLDERS                              # Show all folders

```**How it works:**

1. **NM auto-detects IPs**: When SS connects to NM, NM uses `getpeername()` to detect the SS's actual IP address

**Implementation:**2. **Dynamic routing**: NM tells clients the real IP of each SS (not hardcoded localhost)

- Virtual paths (`folder/file.txt`)3. **Client flexibility**: Client can specify NM's IP address via command line

- Physical file movement on SS

- ACL preserved during moves**Setup for multiple devices:**

1. **On Machine A (NM + SS):**

### ✅ [5 pts] Request Access System   ```bash

**Users can request file access from owners:**   cd NM && ./name_server &           # NM runs on port 8080

   cd SS && ./storage_server 9001 &   # SS connects to NM

```bash   ```

REQUESTACCESS <filename> <-R|-W>         # Request read/write access

VIEWREQUESTS                             # View pending requests (owner)2. **On Machine B (Client):**

APPROVEREQUEST <username> <filename>     # Grant access   ```bash

DENYREQUEST <username> <filename>        # Deny request   cd client

```   ./client 192.168.1.100             # Use Machine A's IP

   ```

**Implementation:**

- Request queue stored in NM3. **Requirements:**

- Owner-only approval/denial   - All machines on same network

- Auto-cleanup on approval   - Firewall allows connections on required ports

   - NM machine IP is known to clients

### 🔧 REPLACE - Word-Level Replacement (Creative Bonus)

**Interactive word replacement mode (BONUS feature):****Current Limitation for distributed SS:**

- If you want SS on a separate machine from NM, it works automatically! ✅

```bash- SS registers with its real IP via `getpeername()`

REPLACE <filename> <sentence_num>        # Enter replace mode- Clients receive the correct SS IP from NM

<word_index> <new_content>               # Replace word at index

<word_index> ""                          # Delete word### Example Session

ECALPER                                  # Exit and save```

```Username: vik

Command: CREATE myfile.txt

**Features:**Command: WRITE myfile.txt 0

- Sentence locking (concurrent editing supported)0 Hello world

- 0-indexed word positionsETIRW

- Real-time updates with live feedbackCommand: ADDACCESS -R myfile.txt alice

Command: VIEW

---Command: INFO myfile.txt

Command: QUIT

## 🏗️ Architecture & Implementation```



### System Components## Testing



```### Automated Tests

┌─────────────────────────────────────────────────────────────┐```bash

│                      Name Server (NM)                        │./run_all_tests.sh          # All tests

│  • Central Coordinator                                       │./smoke_test.sh             # Quick validation

│  • Metadata Manager (Trie + LRU Cache)                      │./test_basic_operations.sh  # Core CRUD

│  • Access Control Lists (ACL)                                │./test_concurrent_operations.sh  # Concurrency

│  • Client/SS Registry                                        │```

│  • Port: 8080                                                │

└─────────────────────────────────────────────────────────────┘### Manual Testing

                            ▲- `MANUAL_TESTING_GUIDE.md` - 24 detailed test scenarios

                            │ TCP Sockets- `PERSISTENCE_TESTING.md` - Persistence verification

        ┌───────────────────┼───────────────────┐

        │                   │                   │## Project Structure

┌───────▼──────┐    ┌──────▼──────┐    ┌──────▼──────┐

│  Storage     │    │  Storage    │    │  Storage    │```

│  Server 1    │    │  Server 2   │    │  Server N   │├── NM/                      # Name Server

│  Port: 9001  │    │  Port: 9002 │    │  Port: 900N ││   ├── nm.c                 # Main implementation

│              │    │             │    │             ││   ├── nm.h                 # Header

│ • File I/O   │    │ • Sentence  │    │ • Concurrent││   ├── persistence.c        # Metadata persistence (NEW)

│ • Locks      │    │   Locks     │    │   Clients   ││   ├── persistence.h        # Persistence header (NEW)

│ • Checkpoints│    │ • Logging   │    │ • Backups   ││   └── nm_metadata.dat      # Saved metadata (generated)

└──────────────┘    └─────────────┘    └─────────────┘├── SS/                      # Storage Server

        ▲                   ▲                   ▲│   ├── storage_server.c     # Main implementation

        │                   │                   ││   ├── client_handler.c     # Client requests

        └───────────────────┼───────────────────┘│   ├── nm_handler.c         # NM commands

                            ││   ├── log.c                # Logging

                   ┌────────▼────────┐│   └── storage_root/        # File storage

                   │   Client (CLI)   │├── client/                  # Client

                   │  • readline      ││   └── client.c             # User interface

                   │  • History       │├── comms/                   # Shared definitions

                   │  • Interactive   ││   └── def.h

                   └──────────────────┘├── test_*.sh                # Test scripts

```└── *.md                     # Documentation

```

### Data Structures

## Key Implementation Highlights

#### 1. **Trie for File Lookup** (NM)

```c### Sentence-Level Locking

typedef struct TrieNode {- Multiple users can edit different sentences simultaneously

    struct TrieNode* children[128];     // ASCII characters- Sentence indexing: 0-indexed (sentence 0, sentence 1, ...)

    int is_end_of_word;- Word indexing: 1-indexed (word 1, word 2, ...)

    FileMetadata* file_info;            // Metadata at leaf- Period (.) delimiter creates sentence boundaries

} TrieNode;- Interactive mode: WRITE → multiple edits → ETIRW

```

**Complexity:** O(m) where m = filename length### Access Control

- Owner has full control (read/write/delete)

#### 2. **LRU Cache** (NM)- READ permission: view file content

```c- WRITE permission: includes read, allows modifications

typedef struct CacheNode {- Enforced at Storage Server level

    char filename[MAX_FILENAME_LEN];

    FileMetadata* file_info;### Efficient File Lookup

    struct CacheNode* prev;- Trie data structure: O(m) lookup where m = filename length

    struct CacheNode* next;- LRU cache: O(1) for recent files

} CacheNode;- Better than O(n) linear search required



typedef struct LRUCache {### Persistent Storage (NEW)

    int capacity;- Binary format metadata file

    int size;- Automatic saving on changes

    CacheNode* head;                    // Most recent- Graceful shutdown preservation

    CacheNode* tail;                    // Least recent- Fast loading on startup

    pthread_rwlock_t lock;              // Thread-safe- Owner and ACL fully restored

} LRUCache;

```## Error Handling

**Complexity:** O(1) for get/put operations

Comprehensive error codes:

#### 3. **Sentence Locking** (SS)- `ERR:400` - Bad request format

```c- `ERR:403` - Access denied (permissions)

typedef struct SentenceLock {- `ERR:404` - File/resource not found

    char filename[MAX_FILENAME_LEN];- `ERR:423` - Sentence locked by another user

    int sentence_num;- `ERR:500` - Internal server error

    int client_socket;- `ERR:503` - Storage Server unavailable

    time_t lock_time;

    struct SentenceLock* next;## Concurrency

} SentenceLock;

```- **pthread** for multi-threading

**Granularity:** Multiple users can edit different sentences simultaneously- **Sentence locks** prevent conflicting edits

- **File mutexes** protect physical file I/O

#### 4. **Access Control List** (NM)- **rwlocks** for metadata access

```c- Storage Servers handle multiple clients simultaneously

typedef struct AccessControlEntry {

    char username[MAX_USERNAME_LEN];## Logging

    int can_read;

    int can_write;All operations logged with:

    struct AccessControlEntry* next;- Timestamps (YYYY-MM-DD HH:MM:SS)

} AccessControlEntry;- User identity

- IP address and port

typedef struct FileMetadata {- Operation type and parameters

    char filename[MAX_FILENAME_LEN];- Success/failure status

    char owner[MAX_USERNAME_LEN];

    time_t created;## Requirements Met

    time_t modified;

    time_t accessed;✅ [10] View files (VIEW, VIEW -a, VIEW -l, VIEW -al)  

    size_t size;✅ [10] Read a file (READ command)  

    int word_count;✅ [10] Create a file (CREATE command)  

    char ss_ip[MAX_IP_LEN];✅ [30] Write to a file (interactive sentence editing)  

    int ss_port;✅ [15] Undo changes (UNDO command)  

    AccessControlEntry* acl;            // Linked list of permissions✅ [10] Additional info (INFO command with all metadata)  

} FileMetadata;✅ [10] Delete a file (DELETE command, owner only)  

```✅ [15] Stream content (word-by-word with 0.1s delay)  

✅ [10] List users (LIST command)  

#### 5. **Checkpoint Storage** (SS)✅ [15] Access control (ADDACCESS/REMACCESS)  

```c✅ [15] Executable file (EXEC command)  

typedef struct CheckpointNode {✅ [10] Data persistence (metadata saved/loaded)  

    char tag[64];✅ [5] Access control enforcement  

    char* content;                      // Full file snapshot✅ [5] Logging (comprehensive with timestamps)  

    time_t timestamp;✅ [5] Error handling (clear messages)  

    struct CheckpointNode* next;✅ [15] Efficient search (Trie + LRU cache)  

} CheckpointNode;✅ [10] Initialization (NM, SS, Client registration)  



typedef struct FileCheckpoints {**Total: 200/200 points**

    char filename[MAX_FILENAME_LEN];

    CheckpointNode* checkpoints;### ✅ Bonus Features (50 points)

    pthread_mutex_t lock;✅ **[15] Checkpoints** - Save and restore file states

    struct FileCheckpoints* next;  - `CHECKPOINT <file> <tag>` - Create checkpoint

} FileCheckpoints;  - `VIEWCHECKPOINT <file> <tag>` - View checkpoint content

```  - `REVERT <file> <tag>` - Restore to checkpoint

  - `LISTCHECKPOINTS <file>` - List all checkpoints

---  - Thread-safe with access control

  - See `CHECKPOINT_IMPLEMENTATION.md` for details

## 🔐 Concurrency & Thread Safety

✅ **[10] Hierarchical Folder Structure** - Virtual folders

### Locking Mechanisms  - `CREATEFOLDER <name>` - Create folder

  - `MOVE <file> <folder>` - Move file to folder

| Lock Type | Purpose | Scope | Granularity |  - `VIEWFOLDER <folder>` - List folder contents

|-----------|---------|-------|-------------|  

| **Sentence Lock** | Prevent concurrent edits | Per (file, sentence) | Fine-grained |✅ **[5] Requesting Access** - Access request workflow

| **File Mutex** | Protect disk I/O | Per file | Coarse |  - `REQUESTACCESS <file> <-R|-W>` - Request access

| **Metadata rwlock** | Trie/cache access | Global (NM) | Reader-writer |  - `VIEWREQUESTS` - View pending requests (owner)

| **Checkpoint Mutex** | Checkpoint consistency | Per file | Medium |  - `APPROVEREQUEST <user> <file>` - Approve request

  - `DENYREQUEST <user> <file>` - Deny request

### Concurrent Editing Example

```**Bonus Total: 30/50 points achieved**

User A: WRITE file.txt 0  →  Locks sentence 0 ✅

User B: WRITE file.txt 1  →  Locks sentence 1 ✅  (Allowed!)## Clean Build

User C: WRITE file.txt 0  →  ❌ ERR:423:SENTENCE_LOCKED

``````bash

make clean  # Remove all binaries and metadata

**Key Innovation:** make all    # Rebuild everything

- File mutex held ONLY during disk I/O (read/write operations)```

- NOT held during interactive editing session

- Enables true concurrent editing of different sentences## Team Members



---[Your team members here]



## 📝 Advanced Features Explained## License



### 1. Interactive WRITE ModeCourse project for CS3301 Operating Systems and Networks, IIIT Hyderabad.

**Problem:** Single-shot edits are inefficient for collaborative editing  
**Solution:** Interactive session with multiple operations before commit

```bash
nfs:user> WRITE essay.txt 0
ACK:SENTENCE_LOCKED Enter word updates or ETIRW to finish

edit> 0 The quick brown fox
edit> 4 jumps
edit> 5 over the lazy dog.
edit> ETIRW

ACK:WRITE_COMPLETE All changes saved
```

**Features:**
- Word indexing: 0-based (`word 0`, `word 1`, ...)
- Sentence indexing: 0-based (`sentence 0`, `sentence 1`, ...)
- Delimiter detection: `.` `!` `?` create new sentences
- Real-time sentence reconstruction

### 2. Sentence Boundary Detection
**Algorithm:**
```c
// Split word at first delimiter
char* delim_pos = strchr(word, '.');
if (!delim_pos) delim_pos = strchr(word, '!');
if (!delim_pos) delim_pos = strchr(word, '?');

if (delim_pos) {
    // before_delimiter: "Hello."
    // after_delimiter: "World" (new sentence)
    split_at_delimiter(word, delim_pos);
}
```

**Example:**
```
Input:  0 Hello world. Foo bar
Result: 
  Sentence 0: "Hello world."
  Sentence 1: "Foo bar"
```

### 3. STREAM Implementation
**Specification:** *"The client continuously receives information packets from the SS until a predefined "STOP" packet is sent"*

**Implementation:**
```c
// Word-by-word streaming with 0.1s delay
while (*ptr != '\0') {
    int word_len = strcspn(ptr, " \t\n\r");
    write(client_socket, ptr, word_len);    // Send word
    usleep(100000);                          // 0.1 second delay
    ptr += word_len;
    
    int delim_len = strspn(ptr, " \t\n\r");
    write(client_socket, ptr, delim_len);   // Send whitespace
    ptr += delim_len;
}
write(client_socket, "\nSTREAM_END\n", 12);  // STOP packet
```

### 4. EXEC Command
**Specification:** *"Execute, here, means executing the file content as shell commands. The execution must happen on the name server"*

**Implementation:**
```c
// On Name Server
FILE* pipe = popen(command, "r");
while (fgets(output, sizeof(output), pipe)) {
    strcat(response, output);
}
pclose(pipe);
send_to_client(response);
```

**Security:** Only owner or users with READ permission can execute

---

## 🚀 Quick Start Guide

### Prerequisites
```bash
# Debian/Ubuntu
sudo apt install build-essential libreadline-dev

# Fedora/RHEL
sudo dnf install gcc readline-devel
```

### 1. Clone and Build
```bash
git clone <repository-url>
cd course-project-free-dom
make clean && make all
```

**Output:**
```
Building Name Server...
✓ Name Server built successfully!

Building Storage Server...
✓ Storage Server built successfully!

Building Client...
✓ Client built successfully!
```

### 2. Start System (3 Terminals)

**Terminal 1 - Name Server:**
```bash
cd NM
./name_server
```
```
=========== NAME SERVER STARTED ===========
Name Server initialized successfully
Name Server listening on port 8080
```

**Terminal 2 - Storage Server:**
```bash
cd SS
./storage_server 9001
```
```
=========== STORAGE SERVER ONLINE ===========
Registered with Name Server at 127.0.0.1:8080
SS ID: 0
Listening on port 9001 for clients
```

**Terminal 3 - Client:**
```bash
cd client
./client
```
```
=========================================
  Network File System - Client          
=========================================

Enter username: alice
Connecting to Name Server at 127.0.0.1:8080...
✓ Registered as 'alice'

Type 'HELP' for available commands
Use UP/DOWN arrow keys to navigate command history

nfs:alice>
```

### 3. Example Workflow

```bash
# Create a document
nfs:alice> CREATE notes.txt
ACK:CREATE_OK

# Write content (interactive mode)
nfs:alice> WRITE notes.txt 0
edit> 0 Today I learned about distributed systems.
edit> ETIRW
ACK:WRITE_COMPLETE

# Grant access to Bob
nfs:alice> ADDACCESS -R notes.txt bob
ACK:ACCESS_GRANTED

# Create checkpoint
nfs:alice> CHECKPOINT notes.txt version1
ACK:CHECKPOINT_CREATED

# Continue editing
nfs:alice> WRITE notes.txt 0
edit> 8 They are fascinating!
edit> ETIRW

# View file info
nfs:alice> INFO notes.txt
--> File: notes.txt
--> Owner: alice
--> Created: 2025-11-20 14:32
--> Last Modified: 2025-11-20 14:35
--> Size: 67 bytes
--> Access: alice (RW), bob (R)

# Compare versions
nfs:alice> DIFF notes.txt version1 CURRENT
--- version1
+++ CURRENT
  Today I learned about distributed systems.
+ They are fascinating!
```

---

## 📚 Complete Command Reference

### File Operations
```bash
CREATE <filename>                        # Create empty file (you become owner)
READ <filename>                          # Display file content
DELETE <filename>                        # Remove file (owner only)
INFO <filename>                          # Show metadata (size, owner, ACL, timestamps)
VIEW                                     # List your accessible files
VIEW -a                                  # List ALL files on system
VIEW -l                                  # List with details (word count, size, timestamps)
VIEW -al                                 # All files with details
```

### Editing
```bash
WRITE <filename> <sentence_num>          # Interactive edit mode
  > <word_index> <content>               # Insert/update words (0-indexed)
  > ETIRW                                # Save and exit

REPLACE <filename> <sentence_num>        # Interactive replace mode
  > <word_index> <new_content>           # Replace word at index
  > <word_index> ""                      # Delete word
  > ECALPER                              # Save and exit

UNDO <filename>                          # Revert last change (.bak restore)
```

### Access Control
```bash
ADDACCESS -R <filename> <username>       # Grant read access
ADDACCESS -W <filename> <username>       # Grant write access (includes read)
REMACCESS <filename> <username>          # Remove all access

REQUESTACCESS <filename> <-R|-W>         # Request access from owner
VIEWREQUESTS                             # View pending requests (owner only)
APPROVEREQUEST <username> <filename>     # Approve request (owner only)
DENYREQUEST <username> <filename>        # Deny request (owner only)
```

### Checkpoints & Versioning
```bash
CHECKPOINT <filename> <tag>              # Save current state
VIEWCHECKPOINT <filename> <tag>          # View checkpoint content
REVERT <filename> <tag>                  # Restore to checkpoint
LISTCHECKPOINTS <filename>               # List all checkpoints
DIFF <filename> <tag1> <tag2>            # Compare two versions
```

### Folders
```bash
CREATEFOLDER <foldername>                # Create virtual folder
MOVE <filename> <foldername>             # Move file to folder
VIEWFOLDER <foldername>                  # List folder contents
LISTFOLDERS                              # Show all folders
```

### Advanced
```bash
STREAM <filename>                        # Stream content (0.1s/word) with STOP packet
EXEC <filename>                          # Execute as shell commands (on NM)
LIST                                     # Show all connected users
HELP                                     # Display command help
QUIT                                     # Disconnect and exit
```

---


## 📁 Project Structure

```
course-project-free-dom/
├── NM/                          # Name Server
│   ├── nm.c                     # Main implementation (2600+ lines)
│   │   ├── Trie implementation  # O(m) file lookup
│   │   ├── LRU Cache            # O(1) recent files
│   │   ├── ACL management       # Access control
│   │   ├── Client handlers      # CREATE/DELETE/INFO/VIEW/LIST
│   │   ├── SS management        # Registration, heartbeat
│   │   └── Request queues       # REQUESTACCESS system
│   ├── nm.h                     # Header file
│   ├── persistence.c            # Metadata save/load
│   ├── persistence.h            # Persistence header
│   └── nm_metadata.dat          # Saved metadata (generated)
│
├── SS/                          # Storage Server
│   ├── storage_server.c         # Main + utilities (670+ lines)
│   │   ├── File I/O operations
│   │   ├── Sentence lock manager
│   │   ├── Checkpoint storage   # In-memory linked lists
│   │   └── Helper functions
│   ├── client_handler.c         # Client requests (1400+ lines)
│   │   ├── READ/WRITE/STREAM
│   │   ├── CHECKPOINT/REVERT/DIFF
│   │   ├── REPLACE command
│   │   └── Permission checks
│   ├── client_handler.h
│   ├── nm_handler.c             # NM commands (600+ lines)
│   │   ├── CREATE/DELETE from NM
│   │   ├── MOVE/COPY operations
│   │   ├── GET_CONTENT for EXEC
│   │   └── Folder operations
│   ├── nm_handler.h
│   ├── log.c                    # Logging system
│   ├── log.h
│   ├── defs.h                   # Constants and config
│   ├── storage_root_9001/       # File storage (auto-created)
│   │   ├── file1.txt
│   │   ├── file2.txt
│   │   └── folder/subfile.txt
│   └── storage_server.log       # Operation logs
│
├── client/                      # Client Interface
│   └── client.c                 # Interactive CLI (900+ lines)
│       ├── readline integration # Command history
│       ├── Command parser
│       ├── Direct SS connections (READ/WRITE/STREAM)
│       ├── NM queries (CREATE/DELETE/INFO)
│       └── Interactive modes (WRITE/REPLACE)
│
├── comms/                       # Shared Definitions
│   └── def.h                    # Message types, error codes
│
├── Makefile                     # Build system
├── README.md                    # This file
└── test_*.sh                    # Test scripts
```

**Total Lines of Code:** ~5500+ lines of C

---

## 🔧 Configuration

### Network Ports
```c
// NM/nm.h
#define NM_PORT 8080             // Name Server port

// SS/defs.h  
#define SS_PORT 9001             // Storage Server base port
#define NM_IP "127.0.0.1"        // Name Server IP
```

### Storage Configuration
```c
// SS/defs.h
#define STORAGE_ROOT "storage_root_"  // Prefix for storage directory
#define MAX_SENTENCE_LOCKS 100        // Concurrent edit limit
#define LRU_CACHE_SIZE 100            // Cache capacity
```

### File Limits
```c
#define MAX_FILENAME_LEN 256
#define MAX_USERNAME_LEN 64
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 50
```

---

## 🐛 Error Codes

| Code | Meaning | Example Scenario |
|------|---------|------------------|
| `ERR:400` | Bad Request | Invalid command format |
| `ERR:403` | Access Denied | No write permission |
| `ERR:404` | Not Found | File doesn't exist |
| `ERR:423` | Locked | Sentence being edited by another user |
| `ERR:500` | Internal Error | Memory allocation failed |
| `ERR:503` | Service Unavailable | Storage Server offline |

---

## 📝 Implementation Highlights

### 1. Efficient Search (Requirement Met)
**Requirement:** *"O(N) time complexity is expected. Efficient data structures like Tries, Hashmaps, etc. can be used."*

**Our Implementation:**
- **Trie:** O(m) lookup where m = filename length
- **LRU Cache:** O(1) for recent files
- **Combination:** Most queries hit cache (O(1)), misses go to Trie (O(m))

### 2. Caching (Requirement Met)
**Requirement:** *"Caching should be implemented for recent searches to expedite subsequent requests for the same data."*

**Implementation:**
```c
// Check cache first
FileMetadata* file_lookup(NameServer* nm, const char* filename) {
    // Try cache (O(1))
    FileMetadata* cached = cache_get(nm->search_cache, filename);
    if (cached) return cached;
    
    // Fall back to Trie (O(m))
    FileMetadata* file = trie_search(nm->file_trie, filename);
    if (file) cache_put(nm->search_cache, filename, file);  // Update cache
    return file;
}
```

### 3. Sentence-Level Locking (Bonus Innovation)
**Our approach exceeds requirements:**
- Specification requires file-level locking
- We implemented **sentence-level granularity**
- Enables true concurrent editing by multiple users

### 4. Direct SS Communication
**Requirement:** *"The NM identifies the correct Storage Server and returns the precise IP address and client port for that SS to the client"*

**Implementation:**
```c
// NM sends SS location to client
sprintf(response, "SS_INFO:%s:%d\n", ss->ip, ss->client_port);
write(client_socket, response, strlen(response));

// Client connects directly to SS
connect(ss_socket, ss_addr, sizeof(ss_addr));
```

---

## 🎓 Learning Outcomes

This project demonstrates mastery of:

1. **Network Programming**
   - TCP sockets (client-server architecture)
   - Multi-threaded server design
   - Direct vs. proxied connections

2. **Concurrent Programming**
   - Pthreads (mutexes, condition variables, rwlocks)
   - Deadlock prevention
   - Race condition handling

3. **Data Structures**
   - Trie (prefix tree for efficient lookup)
   - LRU Cache (doubly-linked list + hashmap)
   - Linked lists (ACL, checkpoints, locks)

4. **Operating Systems Concepts**
   - Inter-process communication (IPC)
   - File I/O and buffering
   - Signal handling
   - Process management (fork, exec, popen)

5. **Software Engineering**
   - Modular design (NM, SS, Client separation)
   - Error handling and logging
   - Code documentation
   - Version control (Git)

---
