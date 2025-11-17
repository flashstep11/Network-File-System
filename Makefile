# Master Makefile for Network File System

CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -pthread

.PHONY: all clean nm ss client help run-nm run-ss run-client

all: nm ss client

# Name Server
nm: NM/name_server

NM/name_server: NM/nm.c NM/persistence.c NM/nm.h NM/persistence.h
	@echo "Building Name Server..."
	$(CC) $(CFLAGS) NM/nm.c NM/persistence.c -o NM/name_server $(LDFLAGS)
	@echo "Name Server built successfully!"

# Storage Server
ss: SS/storage_server

SS/storage_server: SS/storage_server.c SS/client_handler.c SS/nm_handler.c SS/log.c SS/defs.h SS/client_handler.h SS/nm_handler.h SS/log.h
	@echo "Building Storage Server..."
	$(CC) $(CFLAGS) SS/storage_server.c SS/client_handler.c SS/nm_handler.c SS/log.c -o SS/storage_server $(LDFLAGS)
	@echo "Storage Server built successfully!"

# Client
client: client/client

client/client: client/client.c
	@echo "Building Client..."
	$(CC) $(CFLAGS) client/client.c -o client/client $(LDFLAGS)
	@echo "Client built successfully!"

clean:
	@echo "Cleaning all binaries..."
	rm -f NM/name_server NM/*.o NM/*.log NM/nm_metadata.dat
	rm -f SS/storage_server SS/*.o SS/*.log
	rm -f client/client client/*.o
	@echo "Clean complete!"

run-nm:
	@echo "Starting Name Server..."
	@cd NM && ./name_server

run-ss:
	@echo "Starting Storage Server..."
	@cd SS && mkdir -p storage_root && ./storage_server 9001

run-client:
	@echo "Starting Client..."
	@cd client && ./client

help:
	@echo "Network File System - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make all        - Build all components (NM, SS, Client)"
	@echo "  make nm         - Build Name Server only"
	@echo "  make ss         - Build Storage Server only"
	@echo "  make client     - Build Client only"
	@echo "  make clean      - Remove all binaries"
	@echo "  make run-nm     - Run Name Server"
	@echo "  make run-ss     - Run Storage Server"
	@echo "  make run-client - Run Client"
	@echo "  make help       - Show this help"
	@echo ""
	@echo "Manual Testing:"
	@echo "  Terminal 1: make run-nm"
	@echo "  Terminal 2: make run-ss"
	@echo "  Terminal 3: make run-client"
	@echo ""
	@echo "See MANUAL_TESTING.md for detailed instructions"
