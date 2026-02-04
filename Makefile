# CC      = gcc
CC      = clang
CFLAGS  = -g -O0 -Wall -Wextra -Wpedantic
TARGET  = /tmp/torrent_client
SRC     = main.c arena.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full --track-origins=yes $(TARGET) 8080

run:
	/tmp/torrent_client ./totk.torrent
