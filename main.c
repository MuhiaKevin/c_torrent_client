#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena.h"
#include <ctype.h>
#include <ctype.h>


typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef ssize_t  isize;

#define TRUE  1
#define FALSE 0

#define PORT 8080
#define BUFFER_SIZE 500

typedef struct {
	const char *file_name;
	u8 *data;
	size_t len;
} Buffer;



Buffer read_entire_file(const char *path, Arena *arena ) {
	Buffer buf = {0};


	int fd = open(path, O_RDONLY);
	if(fd < 0) {
		perror("open");
		return buf;
	}

	struct stat st;
	if(fstat(fd, &st) < 0) {
		perror("fstat");
		close(fd);
		return buf;
	}


	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr, "Not a regular file\n");
		close(fd);
		return buf;
	}


	// set the length of the buffer equal to the size of the file 
	buf.len = (size_t)st.st_size;
	buf.file_name = path;

	// allocate buffer for the text
	buf.data = arena_alloc(arena, buf.len);


	ssize_t  total = 0;

	while(total < (ssize_t)buf.len) {
		// Read up to the remaining free space in the buffer, and append the data after what I’ve already read.
		
		// read from file descriptor(fd) to buffer whose buffer starts at buffer.data + total number of bytes read and read the next buffer.len - total
		
		// Adding total moves the pointer forward, so the new data is written after the existing data instead of overwriting it.
		
		ssize_t n = read(fd, buf.data + total, buf.len - total);

		if(n <= 0) {
			perror("read");
			close(fd);
			return (Buffer){0};

		}
		// set the number of bytes already read
		total += n;
	}

	return buf;
}

void print_ascii(const u8 *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        u8 c = data[i];
        putchar(isprint(c) ? c : '.');
    }
    putchar('\n');
}


void hexdump_ascii(const u8 *data, size_t len) {
    for (size_t i = 0; i < len; i += 16) {
        printf("%08zx  ", i);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", data[i + j]);
            else
                printf("   ");
        }

        printf(" |");

        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                u8 c = data[i + j];
                putchar(isprint(c) ? c : '.');
            }
        }
        printf("|\n");
    }
}



int main(int argc, char **argv) {
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	Arena arena = arena_create(1 << 20);

	Buffer torrent = read_entire_file(argv[1], &arena);
	if (!torrent.data) {
		fprintf(stderr, "Failed to load torrent\n");
		return 1;
	}

	// bencoded files start with 'd'
	if (torrent.data[0] != 'd') {
		fprintf(stderr, "Invalid torrent file\n");
	}


	 print_ascii(torrent.data, torrent.len);
	//hexdump_ascii(torrent.data, torrent.len);


	arena_destroy(&arena);
}

