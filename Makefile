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
CFLAGS  = -m64 -std=c99 -Wall -Wextra -Wpedantic

# If building debug:
DEBUG_FLAGS = -DDEBUG -O0 -g -fsanitize=address 
LDLIBS  = -lssl -lcrypto 
TARGET  = /tmp/torrent_client
SRC     = main.c arena.c
RELEASE_FLAGS = -DNDEBUG -O2


config ?= debug

ifeq ($(config), debug)
	CFLAGS += $(DEBUG_FLAGS)
else
	CFLAGS += $(RELEASE_FLAGS)
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(LDLIBS) $(CFLAGS)  $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full --track-origins=yes $(TARGET) 8080

run:
	/tmp/torrent_client ./totk.torrent
