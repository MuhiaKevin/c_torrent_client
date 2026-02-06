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


// Add these to your main file after the existing typedefs

typedef enum {
    BCODE_INT,
    BCODE_STRING,
    BCODE_LIST,
    BCODE_DICT
} BcodeType;

typedef struct BcodeNode BcodeNode;

struct BcodeNode {
    BcodeType type;
    union {
        i64 int_val;
        struct {
            u8 *data;
            size_t len;
        } string_val;
        struct {
            BcodeNode **items;
            size_t count;
        } list_val;
        struct {
            BcodeNode **keys;
            BcodeNode **values;
            size_t count;
        } dict_val;
    };
};

// Parser state
typedef struct {
    const u8 *data;
    size_t pos;
    size_t len;
    Arena *arena;
} Parser;

// Forward declaration
BcodeNode* parse_value(Parser *p);

// Parse integer: i<number>e
BcodeNode* parse_int(Parser *p) {
    p->pos++; // skip 'i'
    
    i64 val = 0;
    int negative = 0;
    
    if (p->data[p->pos] == '-') {
        negative = 1;
        p->pos++;
    }
    
    while (p->pos < p->len && p->data[p->pos] != 'e') {
        if (!isdigit(p->data[p->pos])) {
            fprintf(stderr, "Invalid integer\n");
            return NULL;
        }
        val = val * 10 + (p->data[p->pos] - '0');
        p->pos++;
    }
    
    if (p->pos >= p->len || p->data[p->pos] != 'e') {
        fprintf(stderr, "Integer not terminated\n");
        return NULL;
    }
    
    p->pos++; // skip 'e'
    
    BcodeNode *node = arena_alloc(p->arena, sizeof(BcodeNode));
    node->type = BCODE_INT;
    node->int_val = negative ? -val : val;
    return node;
}

// Parse string: <length>:<data>
BcodeNode* parse_string(Parser *p) {
    size_t len = 0;
    
    while (p->pos < p->len && isdigit(p->data[p->pos])) {
        len = len * 10 + (p->data[p->pos] - '0');
        p->pos++;
    }
    
    if (p->pos >= p->len || p->data[p->pos] != ':') {
        fprintf(stderr, "Invalid string format\n");
        return NULL;
    }
    
    p->pos++; // skip ':'
    
    if (p->pos + len > p->len) {
        fprintf(stderr, "String length exceeds buffer\n");
        return NULL;
    }
    
    BcodeNode *node = arena_alloc(p->arena, sizeof(BcodeNode));
    node->type = BCODE_STRING;
    node->string_val.len = len;
    node->string_val.data = arena_alloc(p->arena, len + 1);
    memcpy(node->string_val.data, p->data + p->pos, len);
    node->string_val.data[len] = '\0';
    
    p->pos += len;
    return node;
}

// Parse list: l<items>e
BcodeNode* parse_list(Parser *p) {
    p->pos++; // skip 'l'
    
    BcodeNode *node = arena_alloc(p->arena, sizeof(BcodeNode));
    node->type = BCODE_LIST;
    node->list_val.count = 0;
    node->list_val.items = NULL;
    
    // Count items first
    size_t capacity = 4;
    node->list_val.items = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
    
    while (p->pos < p->len && p->data[p->pos] != 'e') {
        if (node->list_val.count >= capacity) {
            capacity *= 2;
            BcodeNode **new_items = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
            memcpy(new_items, node->list_val.items, node->list_val.count * sizeof(BcodeNode*));
            node->list_val.items = new_items;
        }
        
        BcodeNode *item = parse_value(p);
        if (!item) return NULL;
        
        node->list_val.items[node->list_val.count++] = item;
    }
    
    if (p->pos >= p->len || p->data[p->pos] != 'e') {
        fprintf(stderr, "List not terminated\n");
        return NULL;
    }
    
    p->pos++; // skip 'e'
    return node;
}

// Parse dictionary: d<key><value>...e
BcodeNode* parse_dict(Parser *p) {
    p->pos++; // skip 'd'
    
    BcodeNode *node = arena_alloc(p->arena, sizeof(BcodeNode));
    node->type = BCODE_DICT;
    node->dict_val.count = 0;
    
    size_t capacity = 4;
    node->dict_val.keys = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
    node->dict_val.values = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
    
    while (p->pos < p->len && p->data[p->pos] != 'e') {
        if (node->dict_val.count >= capacity) {
            capacity *= 2;
            BcodeNode **new_keys = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
            BcodeNode **new_vals = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
            memcpy(new_keys, node->dict_val.keys, node->dict_val.count * sizeof(BcodeNode*));
            memcpy(new_vals, node->dict_val.values, node->dict_val.count * sizeof(BcodeNode*));
            node->dict_val.keys = new_keys;
            node->dict_val.values = new_vals;
        }
        
        BcodeNode *key = parse_value(p);
        if (!key || key->type != BCODE_STRING) {
            fprintf(stderr, "Dictionary key must be string\n");
            return NULL;
        }
        
        BcodeNode *value = parse_value(p);
        if (!value) return NULL;
        
        node->dict_val.keys[node->dict_val.count] = key;
        node->dict_val.values[node->dict_val.count] = value;
        node->dict_val.count++;
    }
    
    if (p->pos >= p->len || p->data[p->pos] != 'e') {
        fprintf(stderr, "Dictionary not terminated\n");
        return NULL;
    }
    
    p->pos++; // skip 'e'
    return node;
}

// Main parser
BcodeNode* parse_value(Parser *p) {
    if (p->pos >= p->len) return NULL;
    
    u8 c = p->data[p->pos];
    
    if (c == 'i') return parse_int(p);
    if (c == 'l') return parse_list(p);
    if (c == 'd') return parse_dict(p);
    if (isdigit(c)) return parse_string(p);
    
    fprintf(stderr, "Unknown token: %c\n", c);
    return NULL;
}

// Helper: Find value in dictionary
BcodeNode* dict_get(BcodeNode *dict, const char *key) {
    if (!dict || dict->type != BCODE_DICT) return NULL;
    
    for (size_t i = 0; i < dict->dict_val.count; i++) {
        BcodeNode *k = dict->dict_val.keys[i];
        if (k->type == BCODE_STRING && 
            strcmp((char*)k->string_val.data, key) == 0) {
            return dict->dict_val.values[i];
        }
    }
    return NULL;
}

// Pretty print for debugging
void print_bcode(BcodeNode *node, int indent) {
    if (!node) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (node->type) {
        case BCODE_INT:
            printf("int: %lld\n", (long long)node->int_val);
            break;
        case BCODE_STRING:
            printf("string(%zu): ", node->string_val.len);
            for (size_t i = 0; i < node->string_val.len && i < 50; i++) {
                u8 c = node->string_val.data[i];
                putchar(isprint(c) ? c : '.');
            }
            printf("\n");
            break;
        case BCODE_LIST:
            printf("list[%zu]:\n", node->list_val.count);
            for (size_t i = 0; i < node->list_val.count; i++) {
                print_bcode(node->list_val.items[i], indent + 1);
            }
            break;
        case BCODE_DICT:
            printf("dict[%zu]:\n", node->dict_val.count);
            for (size_t i = 0; i < node->dict_val.count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("key: ");
                if (node->dict_val.keys[i]->type == BCODE_STRING) {
                    printf("%s\n", node->dict_val.keys[i]->string_val.data);
                }
                print_bcode(node->dict_val.values[i], indent + 2);
            }
            break;
    }
}


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
        return 1;
    }

    // Parse the torrent
    Parser parser = {
        .data = torrent.data,
        .pos = 0,
        .len = torrent.len,
        .arena = &arena
    };

    BcodeNode *root = parse_value(&parser);
    if (!root) {
        fprintf(stderr, "Failed to parse torrent\n");
        return 1;
    }

    // Extract some info
    BcodeNode *announce = dict_get(root, "announce");
    if (announce && announce->type == BCODE_STRING) {
        printf("Tracker: %s\n", announce->string_val.data);
    }

    BcodeNode *info = dict_get(root, "info");
    if (info && info->type == BCODE_DICT) {
        BcodeNode *name = dict_get(info, "name");
        if (name && name->type == BCODE_STRING) {
            printf("Name: %s\n", name->string_val.data);
        }
        
        BcodeNode *piece_length = dict_get(info, "piece length");
        if (piece_length && piece_length->type == BCODE_INT) {
            printf("Piece length: %lld\n", (long long)piece_length->int_val);
        }
    }



      // Extract announce-list (list of tracker tiers)
    BcodeNode *announce_list = dict_get(root, "announce-list");
    if (announce_list && announce_list->type == BCODE_LIST) {
        printf("\nTracker tiers:\n");
        for (size_t i = 0; i < announce_list->list_val.count; i++) {
            BcodeNode *tier = announce_list->list_val.items[i];
            if (tier->type == BCODE_LIST) {
                printf("  Tier %zu:\n", i + 1);
                for (size_t j = 0; j < tier->list_val.count; j++) {
                    BcodeNode *tracker = tier->list_val.items[j];
                    if (tracker->type == BCODE_STRING) {
                        printf("    - %s\n", tracker->string_val.data);
                    }
                }
            }
        }
    }

    // Uncomment to see full structure
    // print_bcode(root, 0);

    arena_destroy(&arena);
    return 0;
}
