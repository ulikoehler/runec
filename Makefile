# Makefile for runec - setuid helper for capabilities
#
# Usage:
#   make              # Build with default settings
#   make debug        # Build with debug logging enabled
#   make install      # Build and install to /usr/local/bin (requires sudo)
#   make uninstall    # Remove from /usr/local/bin (requires sudo)
#   make clean        # Remove build artifacts
#
# Options (can be overridden):
#   make ENABLE_CAP_NET_RAW=0      # Disable CAP_NET_RAW
#   make ENABLE_CAP_NET_ADMIN=0    # Disable CAP_NET_ADMIN
#   make ENABLE_CAP_SYS_NICE=0     # Disable CAP_SYS_NICE
#   make ENABLE_DEBUG_LOG=1        # Enable debug logging
#   make PREFIX=/opt/local         # Change install location

# Configuration
CC := gcc
CFLAGS := -Wall -Wextra -O2
LDFLAGS := -lcap

# Install configuration
PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
INSTALL_BIN := $(BINDIR)/runec

# Capability build options (default: both enabled, debug off)
ENABLE_CAP_NET_RAW ?= 1
ENABLE_CAP_NET_ADMIN ?= 1
ENABLE_CAP_SYS_NICE ?= 1
ENABLE_DEBUG_LOG ?= 0

# Construct compiler flags
DEFINES := -DENABLE_CAP_NET_RAW=$(ENABLE_CAP_NET_RAW) \
           -DENABLE_CAP_NET_ADMIN=$(ENABLE_CAP_NET_ADMIN) \
           -DENABLE_CAP_SYS_NICE=$(ENABLE_CAP_SYS_NICE) \
           -DENABLE_DEBUG_LOG=$(ENABLE_DEBUG_LOG)

# Source files
SRC := runec.c
OBJ := runec.o
TARGET := runec

# Build rules
.PHONY: all debug clean install uninstall test help

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(TARGET)
	@echo ""
	@echo "Built $(TARGET) with:"
	@if [ "$(ENABLE_CAP_NET_RAW)" = "1" ]; then echo "  - CAP_NET_RAW"; fi
	@if [ "$(ENABLE_CAP_NET_ADMIN)" = "1" ]; then echo "  - CAP_NET_ADMIN"; fi
	@if [ "$(ENABLE_CAP_SYS_NICE)" = "1" ]; then echo "  - CAP_SYS_NICE"; fi
	@if [ "$(ENABLE_DEBUG_LOG)" = "1" ]; then echo "  - Debug logging ENABLED"; fi
	@echo ""

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) $(DEFINES) -c $(SRC) -o $(OBJ)

# Build with debug logging enabled
debug:
	@$(MAKE) ENABLE_DEBUG_LOG=1

# Install to system (requires root)
install: $(TARGET)
	@echo "Installing $(TARGET) to $(INSTALL_BIN)..."
	@if [ ! -d "$(BINDIR)" ]; then \
		echo "Creating directory $(BINDIR)..."; \
		sudo mkdir -p "$(BINDIR)"; \
	fi
	sudo install -o root -g root -m 4755 $(TARGET) $(INSTALL_BIN)
	@echo ""
	@echo "Setting file capabilities..."
	@CAPS=""; \
	if [ "$(ENABLE_CAP_NET_RAW)" = "1" ]; then CAPS="cap_net_raw"; fi; \
	if [ "$(ENABLE_CAP_NET_ADMIN)" = "1" ]; then \
		if [ -n "$$CAPS" ]; then CAPS="$$CAPS,cap_net_admin"; \
		else CAPS="cap_net_admin"; fi; \
	fi; \
	if [ "$(ENABLE_CAP_SYS_NICE)" = "1" ]; then \
		if [ -n "$$CAPS" ]; then CAPS="$$CAPS,cap_sys_nice"; \
		else CAPS="cap_sys_nice"; fi; \
	fi; \
	if [ -n "$$CAPS" ]; then \
		sudo setcap "$$CAPS+ep" $(INSTALL_BIN) || \
		echo "Warning: setcap failed - relying on setuid-root only"; \
	fi
	@echo ""
	@echo "Verifying installation..."
	@ls -l $(INSTALL_BIN)
	@getcap $(INSTALL_BIN) 2>/dev/null || echo "No file capabilities set"
	@echo ""
	@echo "=== Installation complete ==="
	@echo "Usage: runec <executable> [args...]"

# Remove from system (requires root)
uninstall:
	@echo "Removing $(INSTALL_BIN)..."
	@if [ -f "$(INSTALL_BIN)" ]; then \
		sudo rm -f $(INSTALL_BIN); \
		echo "Removed $(INSTALL_BIN)"; \
	else \
		echo "$(INSTALL_BIN) not found - nothing to remove"; \
	fi

# Clean build artifacts
clean:
	rm -f $(OBJ) $(TARGET)
	@echo "Cleaned build artifacts"

# Simple test (requires the binary to be built)
test: $(TARGET)
	@echo "Testing $(TARGET)..."
	@echo ""
	@echo "=== Showing help ==="
	./$(TARGET)
	@echo ""
	@echo "=== Testing with /bin/true ==="
	./$(TARGET) /bin/true && echo "SUCCESS: runec executed /bin/true" || echo "FAILED"
	@echo ""
	@echo "Note: Full functionality requires installation (make install)"

# Show available targets
help:
	@echo "Available targets:"
	@echo "  make              - Build runec with default settings"
	@echo "  make debug        - Build with debug logging enabled"
	@echo "  make install      - Install to $(BINDIR) (requires sudo)"
	@echo "  make uninstall    - Remove from $(BINDIR) (requires sudo)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make test         - Run basic tests"
	@echo ""
	@echo "Build options:"
	@echo "  ENABLE_CAP_NET_RAW=0|1    (default: 1)"
	@echo "  ENABLE_CAP_NET_ADMIN=0|1  (default: 1)"
	@echo "  ENABLE_CAP_SYS_NICE=0|1   (default: 1)"
	@echo "  ENABLE_DEBUG_LOG=0|1      (default: 0)"
	@echo "  PREFIX=/path              (default: /usr/local)"
	@echo ""
	@echo "Examples:"
	@echo "  make debug                          # Build with debug logging"
	@echo "  make ENABLE_CAP_NET_ADMIN=0        # Build with only CAP_NET_RAW"
	@echo "  make PREFIX=/opt/local install     # Install to /opt/local/bin"
	@echo "  make clean && make debug install   # Clean, debug build, and install"
