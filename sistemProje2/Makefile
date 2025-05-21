# Makefile for Emergency Drone Coordination System
UNAME := $(shell uname -s)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -g -pthread -Iheaders

# Base Linker flags (pthread and json-c are common)
LDFLAGS_BASE = -pthread -ljson-c

# Platform-specific SDL2 flags (only for the main app)
LDFLAGS_APP = $(LDFLAGS_BASE)
ifeq ($(UNAME), Linux)
	LDFLAGS_APP += -lSDL2 -lm
endif
ifeq ($(UNAME), Darwin)
	LDFLAGS_APP += -F/Library/Frameworks -framework SDL2 -lm
endif

# Linker flags for the client (no SDL2 needed)
LDFLAGS_CLIENT = $(LDFLAGS_BASE)

# Source files
APP_SRC = controller.c server.c drone.c list.c map.c survivor.c ai.c view.c globals.c
CLIENT_SRC = drone_client.c
HEADERS = headers/list.h headers/map.h headers/drone.h headers/survivor.h \
          headers/ai.h headers/coord.h headers/globals.h headers/view.h \
          headers/server.h

# Object files
APP_OBJ = $(APP_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Executables
APP_EXE = server
CLIENT_EXE = drone

# Default target
all: $(APP_EXE) $(CLIENT_EXE)

# Main application executable
$(APP_EXE): $(APP_OBJ)
	$(CC) $(APP_OBJ) -o $(APP_EXE) $(LDFLAGS_APP)

# Client executable
$(CLIENT_EXE): $(CLIENT_OBJ)
	$(CC) $(CLIENT_OBJ) -o $(CLIENT_EXE) $(LDFLAGS_CLIENT)

# Compile source files to object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f *.o $(APP_EXE) $(CLIENT_EXE)

# Phony targets
.PHONY: all clean