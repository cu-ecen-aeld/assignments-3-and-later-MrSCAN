# Compiler and flags
CC = gcc
CROSS_COMPILE ?= 
CFLAGS = -Wall -Wextra 

# Source and object files
SRC = writer.c
OBJ = $(SRC:.c=.o)
TARGET = writer

# Default target
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $<

# Clean target
clean:
	rm -f $(OBJ) $(TARGET)

# Support for cross-compilation
ifeq ($(CROSS_COMPILE),)
$(info Cross-compilation not specified. Compiling for the native build platform.)
else
$(info Cross-compilation specified with CROSS_COMPILE=$(CROSS_COMPILE))
endif