// client_handler.h

#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

// Announces that this function exists, so main() can call it.
void start_client_storage_server_loop(int my_port);

// UNDO command handler (implemented in nm_handler.c)
void handle_undo_command(int client_socket, char* buffer);

// Permission checking (implemented in client_handler.c)
int check_permissions(int client_socket, const char *filename, const char *mode);

#endif // CLIENT_HANDLER_H