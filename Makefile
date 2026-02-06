# CC      = clang
# CFLAGS  = -g -O0 -Wall -Wextra -Wpedantic
# LDLIBS  = -lcrypto
#
# TARGET  = /tmp/torrent_client
# # SRC     = arena.c bencode.c dict.c main.c
# SRC     = arena.c main.c
# OBJ     = $(SRC:.c=.o)
#
# all: $(TARGET)
#
# $(TARGET): $(OBJ)
# 	$(CC) $(CFLAGS) $(OBJ) $(LDLIBS) -o $(TARGET)
#
# %.o: %.c
# 	$(CC) $(CFLAGS) -c $< -o $@
#
# clean:
# 	rm -f $(OBJ) $(TARGET)
#
# valgrind: $(TARGET)
# 	valgrind --leak-check=full --track-origins=yes $(TARGET) 8080
#
# run: $(TARGET)
# 	$(TARGET) ./totk.torrent




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
