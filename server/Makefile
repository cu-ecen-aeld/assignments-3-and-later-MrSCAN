# CC ?= aarch64-linux-gnu-gcc
CC ?= gcc
CFLAGS ?= -Wall -Wextra -DUSE_AESD_CHAR_DEVICE=1
LDFLAGS ?= -lpthread

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket $(LDFLAGS)

clean:
	rm -f aesdsocket

