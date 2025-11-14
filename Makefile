# Master Makefile for Network File System

.PHONY: all clean nm ss client help run-nm run-ss run-client

all: nm ss client

nm:
	@echo "Building Name Server..."
	@cd NM && $(MAKE)

ss:
	@echo "Building Storage Server..."
	@cd SS && $(MAKE)

client:
	@echo "Building Client..."
	@cd client && $(MAKE)

clean:
	@echo "Cleaning all binaries..."
	@cd NM && $(MAKE) clean
	@cd SS && $(MAKE) clean
	@cd client && $(MAKE) clean
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
