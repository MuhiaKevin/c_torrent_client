#include <openssl/sha.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define close_socket closesocket
#include "arena.h"
#else
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>


typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
#define close_socket close

#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
#ifdef _WIN32
    typedef long long isize;
#else
    typedef ssize_t isize;
#endif

#define TRUE  1
#define FALSE 0

#define PORT 8080
#define BUFFER_SIZE 500

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define close_socket closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
    #define close_socket close
#endif

// Protocol constants
#define PROTOCOL_ID 0x41727101980LL  // Magic constant for UDP trackers
#define ACTION_CONNECT  0
#define ACTION_ANNOUNCE 1
#define ACTION_SCRAPE   2
#define ACTION_ERROR    3

// Connect request/response
typedef struct {
    u64 protocol_id;  // 0x41727101980
    u32 action;       // 0 for connect
    u32 transaction_id;
} __attribute__((packed)) ConnectRequest;

typedef struct {
    u32 action;       // 0 for connect
    u32 transaction_id;
    u64 connection_id;
} __attribute__((packed)) ConnectResponse;

// Announce request/response
typedef struct {
    u64 connection_id;
    u32 action;       // 1 for announce
    u32 transaction_id;
    u8  info_hash[20];
    u8  peer_id[20];
    u64 downloaded;
    u64 left;
    u64 uploaded;
    u32 event;        // 0: none, 1: completed, 2: started, 3: stopped
    u32 ip_address;   // 0 for default
    u32 key;          // random
    i32 num_want;     // -1 for default
    u16 port;         // your listening port
} __attribute__((packed)) AnnounceRequest;

typedef struct {
    u32 action;       // 1 for announce
    u32 transaction_id;
    u32 interval;     // seconds to wait before re-announcing
    u32 leechers;
    u32 seeders;
} __attribute__((packed)) AnnounceResponse;

typedef struct {
    u32 ip;
    u16 port;
} __attribute__((packed)) Peer;


typedef struct {
	const char *file_name;
	u8 *data;
	size_t len;
} Buffer;



typedef struct {
    u8 *buffer;
    size_t capacity;
    size_t pos;
    Arena *arena;
} Encoder;


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


// Torrent struct
typedef struct {
    u8 *announce;
    u8 info_hash[20];
    u8 *name;
    Buffer *pieceHashes;
    i64  piece_length ;
    i64 length;
    BcodeNode  *announce_list;
}  TorrentFile;

// Forward declaration
BcodeNode* parse_value(Parser *p);

// Forward declaration
void encode_node(Encoder *enc, BcodeNode *node);

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
    
    // Allocate a dictionary node
    BcodeNode *node = arena_alloc(p->arena, sizeof(BcodeNode));
    node->type = BCODE_DICT;
    node->dict_val.count = 0;
    
    // Allocate initial storage 
    // This creates two arrays
    // keys[4]
    // values[4]
    
    size_t capacity = 4;
    node->dict_val.keys = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
    node->dict_val.values = arena_alloc(p->arena, capacity * sizeof(BcodeNode*));
    
    // Main Loop — Parse Until 'e' 
    // As long as we haven’t reached the terminating e, we keep parsing key-value pairs. 
    while (p->pos < p->len && p->data[p->pos] != 'e') {

        // If dictionary has more than 4 entries:
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

void print_single_hash(u8 *hash_ptr) {  // hash_ptr points to start of one 20-byte hash
    for (int j = 0; j < 20; j++) {
        printf("%02x", hash_ptr[j]);
    }
    printf("\n");
}


Buffer *split_pieces(BcodeNode *root, Arena *arena) {
    // create a buffer for the pieces
    Buffer *pieces_buffer = {0};

    BcodeNode *info = dict_get(root, "info");
    if (info && info->type == BCODE_DICT) {

        BcodeNode *pieces = dict_get(info, "pieces");
        if (pieces && pieces->type == BCODE_STRING) {

            pieces_buffer = arena_alloc(arena, sizeof(Buffer));
            pieces_buffer ->len = pieces->string_val.len;

            size_t hash_len = 20;
            size_t num_hashes = pieces->string_val.len / hash_len;

            // create a array of piecces hash with each array item being 20 bytes
            u8 *hashes = arena_alloc(arena, (sizeof(u8) * 20) * num_hashes);

            // copy each 20 byte piece hash to new array
            for (size_t i = 0; i < num_hashes; i++) {
                memcpy((hashes + i * hash_len), pieces->string_val.data + i * hash_len, hash_len);
            }
            pieces_buffer->data = hashes;

            printf("pieces length %zu\n", pieces_buffer->len);

            /*for (size_t i = 0; i < num_hashes; i++) {*/
            /*    print_single_hash(&pieces_buffer->data[i]);*/
            /*}*/

        }
    }

    return  pieces_buffer;
}


// Usage: print_single_hash((uint8_t*)res.hashes[5]);  // 6th hash

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


#ifdef _WIN32
// Windows version using Win32 API
Buffer read_entire_file(const char *path, Arena *arena) {
    Buffer buf = {0};

    HANDLE hFile = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open file: %s (Error: %lu)\n", path, GetLastError());
        return buf;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        fprintf(stderr, "Failed to get file size (Error: %lu)\n", GetLastError());
        CloseHandle(hFile);
        return buf;
    }

    if (fileSize.QuadPart > SIZE_MAX) {
        fprintf(stderr, "File too large\n");
        CloseHandle(hFile);
        return buf;
    }

    buf.len = (size_t)fileSize.QuadPart;
    buf.file_name = path;
    buf.data = arena_alloc(arena, buf.len);

    DWORD totalRead = 0;
    while (totalRead < buf.len) {
        DWORD toRead = (DWORD)(buf.len - totalRead);
        DWORD bytesRead;

        if (!ReadFile(hFile, buf.data + totalRead, toRead, &bytesRead, NULL)) {
            fprintf(stderr, "Read failed (Error: %lu)\n", GetLastError());
            CloseHandle(hFile);
            return (Buffer){0};
        }

        if (bytesRead == 0) {
            fprintf(stderr, "Unexpected end of file\n");
            CloseHandle(hFile);
            return (Buffer){0};
        }

        totalRead += bytesRead;
    }

    CloseHandle(hFile);
    return buf;
}

#else
// Linux version (your original code)
Buffer read_entire_file(const char *path, Arena *arena) {
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

    buf.len = (size_t)st.st_size;
    buf.file_name = path;
    buf.data = arena_alloc(arena, buf.len);

    ssize_t total = 0;
    while(total < (ssize_t)buf.len) {
        ssize_t n = read(fd, buf.data + total, buf.len - total);

        if(n <= 0) {
            perror("read");
            close(fd);
            return (Buffer){0};
        }
        total += n;
    }

    close(fd);
    return buf;
}
#endif


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



void encode_ensure(Encoder *enc, size_t needed) {
    if (enc->pos + needed > enc->capacity) {
        size_t new_cap = enc->capacity * 2;
        while (new_cap < enc->pos + needed) {
            new_cap *= 2;
        }
        u8 *new_buf = arena_alloc(enc->arena, new_cap);
        memcpy(new_buf, enc->buffer, enc->pos);
        enc->buffer = new_buf;
        enc->capacity = new_cap;
    }
} 

void encode_bytes(Encoder *enc, const u8 *data, size_t len) {
    encode_ensure(enc, len);
    memcpy(enc->buffer + enc->pos, data, len);
    enc->pos += len;
}

void encode_str(Encoder *enc, const char *str) {
    encode_bytes(enc, (const u8*)str, strlen(str));
}

void encode_list(Encoder *enc, BcodeNode *node) {
    encode_str(enc, "l");
    for (size_t i = 0; i < node->list_val.count; i++) {
        encode_node(enc, node->list_val.items[i]);
    }
    encode_str(enc, "e");
} 
void encode_string(Encoder *enc, u8 *data, size_t len) {
    char buf[32];
    int prefix_len = snprintf(buf, sizeof(buf), "%zu:", len);
    encode_bytes(enc, (u8*)buf, prefix_len);
    encode_bytes(enc, data, len);
}

void encode_dict(Encoder *enc, BcodeNode *node) {
    encode_str(enc, "d");
    for (size_t i = 0; i < node->dict_val.count; i++) {
        encode_node(enc, node->dict_val.keys[i]);
        encode_node(enc, node->dict_val.values[i]);
    }
    encode_str(enc, "e");
}

void encode_int(Encoder *enc, i64 val) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "i%llde", (long long)val);
    encode_bytes(enc, (u8*)buf, len);
} 


void encode_node(Encoder *enc, BcodeNode *node) {
    switch (node->type) {
        case BCODE_INT:
            encode_int(enc, node->int_val);
            break;
        case BCODE_STRING:
            encode_string(enc, node->string_val.data, node->string_val.len);
            break;
        case BCODE_LIST:
            encode_list(enc, node);
            break;
        case BCODE_DICT:
            encode_dict(enc, node);
            break;
    }

}

// Calculate SHA-1 hash of the info dictionary
void calculate_info_hash(BcodeNode *info, u8 hash[20], Arena *arena) {
    Encoder enc;

    enc.capacity = 4096;
    enc.buffer = arena_alloc(arena, enc.capacity);
    enc.pos = 0;
    enc.arena = arena;

    encode_node(&enc, info);
    
    SHA1(enc.buffer, enc.pos, hash);
}


TorrentFile  buildTorrentFile(BcodeNode *root, Arena *arena) {
    TorrentFile torrentFile = {0};

    // Extract some info
    BcodeNode *announce = dict_get(root, "announce");
    if (announce && announce->type == BCODE_STRING) {
        /*printf("Tracker: %s\n", announce->string_val.data);*/
        torrentFile.announce = announce->string_val.data;
    }

    BcodeNode *info = dict_get(root, "info");
    if (info && info->type == BCODE_DICT) {

        u8 info_hash[20];
        calculate_info_hash(info, info_hash, arena);
        memcpy(torrentFile.info_hash, info_hash, 20);

        /*printf("info_hash: ");*/
        /*print_single_hash((u8 *)&info_hash);*/

        BcodeNode *name = dict_get(info, "name");
        if (name && name->type == BCODE_STRING) {
            /*printf("Name: %s\n", name->string_val.data);*/
            torrentFile.name = name->string_val.data;
        }

        BcodeNode *piece_length = dict_get(info, "piece length");
        if (piece_length && piece_length->type == BCODE_INT) {
            /*printf("Piece length: %lld\n", (long long)piece_length->int_val);*/
            torrentFile.piece_length = piece_length->int_val;
        }
    }

    // Extract announce-list (list of tracker tiers)
    BcodeNode *announce_list = dict_get(root, "announce-list");
    if (announce_list && announce_list->type == BCODE_LIST) {
        /*printf("\nTracker tiers:\n");*/
        for (size_t i = 0; i < announce_list->list_val.count; i++) {
            BcodeNode *tier = announce_list->list_val.items[i];
            if (tier->type == BCODE_LIST) {
                /*printf("  Tier %zu:\n", i + 1);*/
                for (size_t j = 0; j < tier->list_val.count; j++) {
                    BcodeNode *tracker = tier->list_val.items[j];
                    if (tracker->type == BCODE_STRING) {
                        /*printf("    - %s\n", tracker->string_val.data);*/
                    }
                }
            }
        }
    }


    Buffer *pieceHashes = split_pieces(root, arena);
    torrentFile.pieceHashes = pieceHashes;
    torrentFile.announce_list = announce_list;

    return torrentFile ;
}


// Helper: Convert big-endian to host byte order for 64-bit
u64 be64toh_custom(u64 val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((val & 0xFF00000000000000ULL) >> 56) |
    ((val & 0x00FF000000000000ULL) >> 40) |
    ((val & 0x0000FF0000000000ULL) >> 24) |
    ((val & 0x000000FF00000000ULL) >> 8)  |
    ((val & 0x00000000FF000000ULL) << 8)  |
    ((val & 0x0000000000FF0000ULL) << 24) |
    ((val & 0x000000000000FF00ULL) << 40) |
    ((val & 0x00000000000000FFULL) << 56);
#else
    return val;
#endif
}

u64 htobe64_custom(u64 val) {
    return be64toh_custom(val);
}

// Initialize networking (Windows needs this)
int init_networking(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return -1;
    }
#endif
    return 0;
}

void cleanup_networking(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Parse tracker URL
typedef struct {
    char host[256];
    int port;
} TrackerURL;

int parse_tracker_url(const char *url, TrackerURL *out) {
    // Expected format: udp://hostname:port/announce
    if (strncmp(url, "udp://", 6) != 0) {
        fprintf(stderr, "Not a UDP tracker URL\n");
        return -1;
    }

    const char *start = url + 6;
    const char *colon = strchr(start, ':');
    if (!colon) {
        fprintf(stderr, "Invalid tracker URL format\n");
        return -1;
    }

    size_t host_len = colon - start;
    if (host_len >= sizeof(out->host)) {
        fprintf(stderr, "Hostname too long\n");
        return -1;
    }

    memcpy(out->host, start, host_len);
    out->host[host_len] = '\0';

    out->port = atoi(colon + 1);
    if (out->port <= 0 || out->port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return -1;
    }

    return 0;
}


// Generate random transaction ID
u32 random_transaction_id(void) {
    return (u32)time(NULL) ^ (u32)getpid();
}

// Generate random peer ID
void generate_peer_id(u8 peer_id[20]) {
    memcpy(peer_id, "-UT2026-", 8);  // Client ID: uTorrent 2026
    for (int i = 8; i < 20; i++) {
        peer_id[i] = '0' + (rand() % 10);
    }
}

// Step 1: Connect to tracker
int tracker_connect(socket_t sock, struct sockaddr_in *tracker_addr, u64 *connection_id) {
    ConnectRequest req = {0};
    req.protocol_id = htobe64_custom(PROTOCOL_ID);
    req.action = htonl(ACTION_CONNECT);
    req.transaction_id = htonl(random_transaction_id());

    printf("Sending connect request...\n");
    if (sendto(sock, (char*)&req, sizeof(req), 0, 
               (struct sockaddr*)tracker_addr, sizeof(*tracker_addr)) < 0) {
        perror("sendto failed");
        return -1;
    }

    // Wait for response (with timeout)
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int ret = select(sock + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        fprintf(stderr, "Connect timeout or error\n");
        return -1;
    }

    ConnectResponse resp = {0};
    socklen_t addr_len = sizeof(*tracker_addr);
    ssize_t n = recvfrom(sock, (char*)&resp, sizeof(resp), 0, 
                         (struct sockaddr*)tracker_addr, &addr_len);

    if (n < (ssize_t)sizeof(resp)) {
        fprintf(stderr, "Invalid connect response size\n");
        return -1;
    }

    resp.action = ntohl(resp.action);
    resp.transaction_id = ntohl(resp.transaction_id);
    resp.connection_id = be64toh_custom(resp.connection_id);

    if (resp.action != ACTION_CONNECT) {
        fprintf(stderr, "Invalid action in response\n");
        return -1;
    }

    if (resp.transaction_id != ntohl(req.transaction_id)) {
        fprintf(stderr, "Transaction ID mismatch\n");
        return -1;
    }

    *connection_id = resp.connection_id;
    printf("Connected! Connection ID: %llx\n", (unsigned long long)*connection_id);
    return 0;
}

// Step 2: Announce to tracker
int tracker_announce(socket_t sock, struct sockaddr_in *tracker_addr, 
                     u64 connection_id, const u8 info_hash[20], 
                     Peer **peers_out, size_t *peer_count_out, Arena *arena) {
    AnnounceRequest req = {0};
    req.connection_id = htobe64_custom(connection_id);
    req.action = htonl(ACTION_ANNOUNCE);
    req.transaction_id = htonl(random_transaction_id());
    memcpy(req.info_hash, info_hash, 20);

    generate_peer_id(req.peer_id);

    req.downloaded = 0;
    req.left = htobe64_custom(0);  // Pretend we have everything for now
    req.uploaded = 0;
    req.event = htonl(2);  // 2 = started
    req.ip_address = 0;
    req.key = htonl(rand());
    req.num_want = htonl(50);  // Request 50 peers
    req.port = htons(6881);    // Standard BitTorrent port

    printf("Sending announce request...\n");
    if (sendto(sock, (char*)&req, sizeof(req), 0,
               (struct sockaddr*)tracker_addr, sizeof(*tracker_addr)) < 0) {
        perror("sendto failed");
        return -1;
    }

    // Wait for response
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int ret = select(sock + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        fprintf(stderr, "Announce timeout or error\n");
        return -1;
    }

    // Receive response (header + peers)
    u8 buffer[2048];
    socklen_t addr_len = sizeof(*tracker_addr);
    ssize_t n = recvfrom(sock, (char*)buffer, sizeof(buffer), 0,
                         (struct sockaddr*)tracker_addr, &addr_len);

    if (n < (ssize_t)sizeof(AnnounceResponse)) {
        fprintf(stderr, "Invalid announce response size\n");
        return -1;
    }

    AnnounceResponse *resp = (AnnounceResponse*)buffer;
    resp->action = ntohl(resp->action);
    resp->transaction_id = ntohl(resp->transaction_id);
    resp->interval = ntohl(resp->interval);
    resp->leechers = ntohl(resp->leechers);
    resp->seeders = ntohl(resp->seeders);

    if (resp->action == ACTION_ERROR) {
        fprintf(stderr, "Tracker error: %.*s\n", (int)(n - 8), buffer + 8);
        return -1;
    }

    if (resp->action != ACTION_ANNOUNCE) {
        fprintf(stderr, "Invalid action in announce response\n");
        return -1;
    }

    printf("\nTracker Stats:\n");
    printf("  Seeders: %u\n", resp->seeders);
    printf("  Leechers: %u\n", resp->leechers);
    printf("  Interval: %u seconds\n", resp->interval);

    // Parse peer list
    size_t peer_data_size = n - sizeof(AnnounceResponse);
    size_t num_peers = peer_data_size / sizeof(Peer);

    if (num_peers > 0) {
        Peer *peer_list = (Peer*)(buffer + sizeof(AnnounceResponse));
        Peer *peers = arena_alloc(arena, num_peers * sizeof(Peer));
        memcpy(peers, peer_list, num_peers * sizeof(Peer));

        *peers_out = peers;
        *peer_count_out = num_peers;
    } else {
        *peers_out = NULL;
        *peer_count_out = 0;
    }

    return 0;
}

// Main tracker communication function
int communicate_with_tracker(const char *tracker_url, const u8 info_hash[20], Arena *arena) {
    if (init_networking() < 0) {
        return -1;
    }

    TrackerURL url;
    if (parse_tracker_url(tracker_url, &url) < 0) {
        cleanup_networking();
        return -1;
    }

    printf("Connecting to tracker: %s:%d\n", url.host, url.port);

    // Resolve hostname
    struct hostent *he = gethostbyname(url.host);
    if (!he) {
        fprintf(stderr, "Failed to resolve hostname\n");
        cleanup_networking();
        return -1;
    }

    // Create UDP socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VALUE) {
        perror("socket failed");
        cleanup_networking();
        return -1;
    }

    // Set up tracker address
    struct sockaddr_in tracker_addr = {0};
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_port = htons(url.port);
    memcpy(&tracker_addr.sin_addr, he->h_addr_list[0], he->h_length);

    // Step 1: Connect
    u64 connection_id;
    if (tracker_connect(sock, &tracker_addr, &connection_id) < 0) {
        close_socket(sock);
        cleanup_networking();
        return -1;
    }

    // Step 2: Announce
    Peer *peers = NULL;
    size_t peer_count = 0;
    if (tracker_announce(sock, &tracker_addr, connection_id, info_hash, 
                         &peers, &peer_count, arena) < 0) {
        close_socket(sock);
        cleanup_networking();
        return -1;
    }

    // Print peer list
    printf("\nReceived %zu peers:\n", peer_count);
    for (size_t i = 0; i < peer_count; i++) {  // Print first 10
        u32 ip = ntohl(peers[i].ip);
        u16 port = ntohs(peers[i].port);
        printf("  %u.%u.%u.%u:%u\n",
               (ip >> 24) & 0xFF,
               (ip >> 16) & 0xFF,
               (ip >> 8) & 0xFF,
               ip & 0xFF,
               port);
    }

    close_socket(sock);
    cleanup_networking();
    return 0;
}


int main(int argc, char **argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }


    // << is the bitwise left shift operator. 
    // it means : 1 × 2^20  = 1 << 20 = 1,048,576 
    // That’s 1 megabyte (1 MB) in bytes. 
    Arena arena = arena_create(40ULL * 1024 * 1024); 

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


    TorrentFile torrentFile = buildTorrentFile(root, &arena);
    printf("%s\n", torrentFile.name);
    print_single_hash(torrentFile.info_hash);

    // Uncomment to see full structure
    // print_bcode(root, 0);
    /*split_pieces(root, &arena);*/


    // Get tracker URL
    BcodeNode *announce = dict_get(root, "announce");
    if (announce && announce->type == BCODE_STRING) {
        printf("Tracker: %s\n\n", announce->string_val.data);

        // Communicate with tracker
        communicate_with_tracker((char*)announce->string_val.data, torrentFile.info_hash, &arena);
    }


    arena_destroy(&arena);
    return 0;
}
