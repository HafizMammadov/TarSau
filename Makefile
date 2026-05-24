CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 -D_POSIX_C_SOURCE=200809L
TARGET  = tarsau
SRC     = tarsau.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
