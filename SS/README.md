 storage_server.c (The "Heart" and "Brain" of the SS)

This file contains the main() function, which is the entry point for the entire Storage Server. It also contains all the core helper functions (like locking and file parsing) that other parts of the program use.

1. The main() Function: The Startup Sequence

This function runs a strict protocol to get the SS online and registered:

    Init Logging: It calls logger_init() so it can start logging its actions.

    Init Locks: It calls init_lock_systems() to create the managers for both sentence-level and file-level locks.

    Connect to NS: It acts as a client to your Naming Server.

        It creates a single TCP socket (socket()).

        It looks up your NS's address (hardcoded in defs.h).

        It connects to your NS (connect()). This one connection will stay open forever.

    Send Registration: As soon as it connects, it builds and sends the REGISTER_SS message.

        It scans its own directory (opendir(".")) for all data files (filtering out .c, .o, and executables).

        It sends one line: REGISTER_SS <my_port> [file1.txt] [file2.txt] ...\n.

        Your (NS) Job: You must accept() this connection, read() this line, parse the port and file list, and add this SS to your list of active servers.

    "Split in Two": After you (NS) send an ACK back, the SS splits its work.

        Private Thread: It spawns a new thread (pthread_create) to run handle_nm_commands. It hands this thread the persistent socket to you. This thread's only job is to listen for your trusted commands.

        Public Server: The main thread continues on and calls start_client_storage_server_loop. Its job is now to become a public server and listen for clients.

2. The Locking System (The Most Important Part)

This file implements the two-level locking system to allow concurrent editing.

    init_lock_systems(): Allocates memory for the g_sentence_lock_manager and sets up the mutex for the g_file_mutexes list.

    acquire_sentence_lock() (The "Hotel Logbook"):
        Used in handle_write() to check whether a sentence is locked or not .
        It locks the list of sentence locks (manager_lock). (so that no other thread can perform edit the linked list) 
        If locked , then that sentence will be present here .        
        It scans the list for a matching (filename, sentence_id).
        If found, it returns 0 (fail).
        If not found, it creates a new node, adds it to the list, unlocks, and returns 1 (success).
    release_sentence_lock():
        after we r done with editing the sentence we remove the lock
        Locks the list, finds the matching node, removes it from the list (prev->next = curr->next), and free()s it.
    get_file_mutex() (The "File Cabinet Key"):

        This is the Physical Lock.
        while writing , if a sentence lock is obtained by a threaad , then it waits for the file lock to be released.
        It locks the list of mutexes (g_file_mutex_list_lock).
        It scans the list for a mutex matching the filename.
        If found, it unlocks the list and returns the mutex.
        If not found, it mallocs a new node, calls pthread_mutex_init() to create a new mutex for this file, adds it to the list, unlocks, and returns the new mutex.
    Helper_functions:
        read_file_to_string(): A utility that opens a file, finds its size (fseek, ftell), allocates memory, reads the entire file into that memory, and returns it as a string.

        find_sentence() / find_word(): These are the parsers for the WRITE command. They are complex string-scanning functions that find the exact start and end byte offsets for a given sentence or word number.
nm_handler.c:
This file handles your trusted commands. It's the "body" obeying the "brain."

    handle_nm_commands():

        This function is the entry point for the "Private" thread.

        Its entire life is a while(read(...)) loop, listening on the single, persistent socket that main() gave it.

        It read()s a command from you (e.g., CREATE file.txt\n).

        It uses strncmp to dispatch to the correct function. It does no permissions checks because it trusts you.

    handle_create_command():

        Your (NS) Job: You send this when a client asks to create a file, and you've determined this SS is where it should live.

        How it works: It first tries fopen(filename, "r") to check if the file already exists.

        What you get: It replies ERR:409:FILE_ALREADY_EXISTS if it does, or ACK:CREATE_OK after it successfully creates the empty file.

    handle_delete_command():

        Your (NS) Job: You send this when a client (who has permission) asks to delete a file.

        How it works: It first locks the physical file mutex (get_file_mutex) to prevent a race condition with a client WRITE. Then it calls remove().

        What you get: ACK:DELETE_OK or ERR:404:DELETE_FILE_FAILED.

    handle_get_info_command():

        Your (NS) Job: You send this when a client requests INFO on a file.

        How it works: It runs stat() on the file to get its physical metadata.

        What you get: ACK:INFO <size> <mod_time> <access_time>\n. You will need to take this, combine it with your logical data (Owner, Access List), and send the final formatted result to the client.
log.c (The Thread-Safe Logger)

This is a simple utility to make logging easy and safe.

    logger_init(): Called once by main(). Opens the storage_server.log file in "append" mode.

    logger():

        This is the function everyone else calls (e.g., logger("Client connected\n");).

        Key Feature: It uses a single, private mutex (g_log_lock).

        Before writing, it pthread_mutex_lock()s.

        It prints the timestamp and message to both the console (printf) and the log file (fprintf).

        It then pthread_mutex_unlock()s.

        Why? This lock prevents two threads from trying to write at the same time and "garbling" the log file (e.g., [Time] Thread 1... [Time] ...Message ...Thread 2 Message...).
📄 client_handler.c: The Public Client Server

This file's entire responsibility is to manage the public-facing side of the Storage Server. It handles all incoming client connections, creates threads to manage them, and processes client commands like READ, WRITE, STREAM, and UNDO.

1. start_client_storage_server_loop(int my_port)

This function is the entry point for the client-facing server. It's called by main() in storage_server.c after the Naming Server (NS) registration is complete.

Code Flow:

    socket(): Creates the main server TCP socket (server_fd).

    bind(): Binds this socket to the my_port that was passed in and INADDR_ANY, which means it listens for connections on all available network interfaces on the machine.

    listen(): Puts the socket into a listening state, making it ready to accept incoming client connections up to a queue limit of MAX_CLIENTS.

    while(1) Loop (The "Accept" Loop): This is the primary loop for the main server thread. It does only one thing:

        accept(): This is a blocking call. The main thread pauses here, waiting for a new client to connect. When a client connects, accept() creates a new socket (client_socket) just for that client and returns.

        malloc(sizeof(int)): It allocates memory on the heap for the new client_socket ID. This is critical for multi-threading; if it passed the client_socket variable directly, every thread would see the same variable, which would be overwritten by the next client.

        pthread_create(): It spawns a new, independent thread. It tells this thread to start running the handle_client_request function and passes it the pointer to the p_client_socket as its argument.

        pthread_detach(): It detaches the new thread. This tells the OS to automatically clean up all of the thread's resources when it finishes. The main thread does not need to wait for it or join() it.

    The loop immediately repeats, returning to the accept() call to wait for the next client.

2. handle_client_request(void *arg)

This function is the entire "life" of a single client thread. Every connected client has one of these functions running just for them.

Code Flow:

    Setup: It receives the arg (which is the p_client_socket pointer), copies the socket ID into a local variable, and immediately free()s the heap memory that start_client_storage_server_loop allocated.

    while((read_size = read(...))) Loop: This is the command-reading loop for this one client.

    strncmp Dispatcher: It uses an if/else if block to check the first few characters of the buffer (e.g., "READ", "WRITE") to identify the command.

    Security Check: It contains an explicit check to reject commands like CREATE and DELETE if they come from a client, as those are reserved for the NS.

    Delegation: It calls the appropriate handler function (e.g., handle_read_command(client_socket, buffer)) to do the actual work.

    memset(buffer, 0, ...): After a command is processed, it clears the buffer, ready for the next command from this client.

    Exit: If read() returns 0 (client disconnected) or -1 (error), the while loop breaks. The function then close()s its client_socket and calls pthread_exit(NULL) to terminate the thread.

3. check_permissions(int client_socket, const char *filename, const char *mode)

This is a critical, synchronous helper function. It pauses the client's thread to ask the Naming Server for permission.

Code Flow:

    Get Client IP: Uses getpeername() on the client_socket to find the client's IP address.

    Connect to NS: It opens a new, temporary TCP connection to the Naming Server (using socket(), connect(), and the NM_IP/NM_PORT from defs.h).

    Send Query: It formats and write()s the permission query string: CHECK_ACCESS <ip> <filename> <mode>\n.

    Wait for Reply: It read()s the NS's response into a buffer.

    close(nm_sock): It immediately closes the temporary connection to the NS.

    Return Value: It strncmps the response. If it's exactly "ACK:YES", it returns 1 (true). For any other reply (like ACK:NO, a connection failure, or a garbled response), it returns 0 (false).

4. Command Handlers

These are the functions that do the actual work.

handle_read_command(int client_socket, char* buffer)

    Code Flow:

        sscanf(buffer, "READ %255s", filename): Parses the command to get the filename.

        check_permissions(client_socket, filename, "READ"): Asks the NS for "READ" permission. If it fails (returns 0), it sends ERR:403:ACCESS_DENIED to the client and returns.

        read_file_to_string(filename, &file_size): Calls the helper function from storage_server.c to read the entire file into a memory buffer.

        Response: If the buffer is NULL, it sends ERR:404:FILE_NOT_FOUND. Otherwise, it write()s the entire file_content buffer to the client_socket.

handle_write_command(int client_socket, char* buffer)

This is the most complex function in this file.

    Code Flow:

        Parse: sscanf() extracts filename, sentence_num, and word_index.

        Find Content: A while loop (skipping 4 spaces) and strrchr (trimming \n) are used to find the start of the actual content string.

        check_permissions(..., "WRITE"): Asks the NS for "WRITE" permission. Fails with ERR:403.

        acquire_sentence_lock(...): Attempts to get the Logical Lock from storage_server.c. If it fails (returns 0), it means another user is already editing this sentence. It sends ERR:503:SENTENCE_LOCKED and returns.

        get_file_mutex(...) -> pthread_mutex_lock(): Acquires the Physical Lock. This call blocks (pauses the thread) if another thread is anywhere in its critical section for this file (e.g., another WRITE or UNDO).

        Read-Modify-Write:

            read_file_to_string() gets the current file data.

            find_sentence() and find_word() are called to find the byte offsets of the target text.

            If found, it mallocs a new_file_data buffer and constructs the new file content in memory by combining the part before the word, the new content, and the part after the word.

            If find_word or find_sentence fails, it sends an ERR:404 and releases the locks.

        Backup: It opens filename.bak (in "w" mode) and writes the original file_data to it. This is for the UNDO command.

        Commit: It fopen(filename, "w") (which truncates/erases the file) and writes the new new_file_data to it.

        Response: Sends ACK:WRITE_SUCCESS or an ERR:500 if the write failed.

        Cleanup: It free()s both the file_data (original) and new_file_data (modified) buffers.

        Release Locks: It calls pthread_mutex_unlock(f_mutex) and release_sentence_lock(...) in that order.

handle_stream_command(int client_socket, char* buffer)

    Code Flow:

        sscanf() to get filename.

        check_permissions(..., "READ"). Fails with ERR:403.

        read_file_to_string() to get the content.

        Streaming Loop: It loops through the file_content buffer:

            strcspn(ptr, delims): Finds the length of the next "word" (any characters that are not delimiters).

            write(): Sends that word.

            usleep(100000): Pauses for 0.1 seconds.

            strspn(ptr, delims): Finds the length of the whitespace that follows (any characters that are delimiters).

            write(): Sends the whitespace/newlines (this preserves formatting).

        Stop Packet: After the loop, it sends \nSTREAM_END\n to tell the client it's finished.

        free(file_content).

handle_undo_command(int client_socket, char* buffer)

    Code Flow:

        sscanf() to get filename.

        check_permissions(..., "WRITE"): Checks for WRITE permission, as UNDO is a destructive (write) operation. Fails with ERR:403.

        sprintf(bak_filename, "%s.bak", filename): Builds the backup file's name.

        get_file_mutex(...) -> pthread_mutex_lock(): Acquires the Physical Lock to prevent a race condition with a WRITE.

        rename(bak_filename, filename): This is the core logic. It's an atomic OS call to replace the main file with the backup file. It will fail if bak_filename doesn't exist.

        Response: Sends ACK:UNDO_OK on success or ERR:404:UNDO_FAILED on failure.

        Release Lock: pthread_mutex_unlock(file_lock).