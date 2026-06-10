#include <openssl/sha.h>

#ifdef _WIN32 // “If we are compiling on Windows… then use these header files
// Everything inside this block is Windows-specific.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // This tells MSVC (Windows compiler): “Link against the Winsock library automatically.

typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define close_socket closesocket
#include "arena.h"
#else
// If not Windows, this part is used.
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
#include <errno.h>


typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
#define close_socket close

#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usize ;

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
    #include <winerror.h>
    #pragma comment(lib, "ws2_32.lib")
    
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define close_socket closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    
    #define INVALID_SOCKET_VALUE -1
    #define close_socket close
#endif


// Macros for defining size
#define KB(x) ((size_t)(x) * 1024ULL)
#define MB(x) (KB(x) * 1024ULL)
#define GB(x) (MB(x) * 1024ULL) 

// Protocol constants
#define PROTOCOL_ID 0x41727101980LL  // Magic constant for UDP trackers
#define ACTION_CONNECT  0
#define ACTION_ANNOUNCE 1
#define ACTION_SCRAPE   2
#define ACTION_ERROR    3



// Peer protocol constants
#define HANDSHAKE_LENGTH 68
#define BLOCK_SIZE 16384  // 16 KB blocks
#define REQUEST_TIMEOUT 30


typedef struct BcodeNode BcodeNode;

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

// Add these to your main file after the existing typedefs

typedef enum {
    BCODE_INT,
    BCODE_STRING,
    BCODE_LIST,
    BCODE_DICT
} BcodeType;



const char* bcode_type_to_string(BcodeType type) {
    switch (type) {
        case BCODE_INT:
            return "INT";

        case BCODE_STRING:
            return "STRING";

        case BCODE_LIST:
            return "LIST";

        case BCODE_DICT:
            return "DICT";


        default:
            return "UNKNOWN";
    }
}

// *****************
// HELPER FUNCTIONS
// *****************

void peer_ip_to_str(Peer *peer, char ip_str[20]) {
    u32 ip = ntohl(peer->ip);

    sprintf(ip_str, "%u.%u.%u.%u",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
}

void print_peers(Peer *peers, size_t peer_count) {
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
}

void print_bcode_type(BcodeType type) {
    printf("%s\n", bcode_type_to_string(type));
}

// *****************
// HELPER FUNCTIONS
// *****************


struct BcodeNode {
    BcodeType type;
    size_t start_offset;
    size_t end_offset;

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


typedef struct {
    socket_t sock;
    char ip[16];
    u16 port;

    // Connection state
    u32 am_choking;
    u32 am_interested;
    u32 peer_choking;
    u32 peer_interested;

    u8 *bitfield;
    usize bitfield_len;

    time_t last_message_time;
} PeerConnection;

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
    usize  piece_length ;
    usize length;
    BcodeNode  *announce_list;
    u32 num_pieces;
}  TorrentFile;

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



// Record position in parse_value before dispatching:
BcodeNode* parse_value(Parser *p) {
    if (p->pos >= p->len) return NULL;

    size_t start = p->pos;  // record start

    u8 c = p->data[p->pos];
    BcodeNode *node = NULL;

    if (c == 'i') node = parse_int(p);
    else if (c == 'l') node = parse_list(p);
    else if (c == 'd') node = parse_dict(p);
    else if (isdigit(c)) node = parse_string(p);

    if (node) {
        node->start_offset = start;
        node->end_offset = p->pos;  // parser has advanced past the node
    }

    return node;
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


Buffer *split_pieces(BcodeNode *root, Arena *arena, TorrentFile *torrent_file) {
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
            torrent_file->num_pieces = num_hashes;

            /*printf("pieces length %zu\n", pieces_buffer->len);*/

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


void calculate_info_hash(BcodeNode *info, const u8 *source_bytes, u8 hash[20]) {
    memset(hash, 0, 20);
    size_t len = info->end_offset - info->start_offset;
    SHA1(source_bytes + info->start_offset, len, hash);

    /*printf("info hash: ");*/
    /*for(size_t i = 0; i < 20; i++) {*/
    /*    printf("%02x", hash[i]);*/
    /*}*/
    /*printf("\n");*/
}


TorrentFile  buildTorrentFile(Buffer *torrent, BcodeNode *root, Arena *arena) {
    TorrentFile torrentFile = {0};

    // Extract some info
    BcodeNode *announce = dict_get(root, "announce");
    if (announce && announce->type == BCODE_STRING) {
        /*printf("Tracker: %s\n", announce->string_val.data);*/
        torrentFile.announce = announce->string_val.data;
    }

    BcodeNode *info = dict_get(root, "info");
    if (info && info->type == BCODE_DICT) {

        calculate_info_hash(info, torrent->data, torrentFile.info_hash);


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


    Buffer *pieceHashes = split_pieces(root, arena, &torrentFile);
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

static inline u64 swap64(u64 x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
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



int wait_for_response(socket_t sock, int timeout_seconds) {
    fd_set readfs;
    struct timeval tv;

    FD_ZERO(&readfs);
    FD_SET(sock, &readfs);

    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;

    int ret = select(sock + 1, &readfs, NULL, NULL, &tv);

    if (ret == 0) return 0;   // timeout
    if (ret < 0) return -1;   // error
    return 1;                 // ready
}



int parse_tracker_url(const char *url, TrackerURL *out) {
    // Expected format: udp://hostname:port/announce
    if (strncmp(url, "udp://", 6) != 0) {
        fprintf(stderr, "Not a UDP tracker URL\n");
        return -1;
    }


    // url is a pointer to start of the string 
    // add 6 + start of pointer to the string to get text past udp://
    const char *start = url + 6;

    // get pointer address of the location of the colon character in the tracker url 
    const char *colon = strchr(start, ':');
    if (!colon) {
        fprintf(stderr, "Invalid tracker URL format\n");
        return -1;
    }

    // length of the host name i.e strlen("opentor.net") == 11;
    size_t host_len = colon - start;

    // make host_len is not longer than 256
    if (host_len >= sizeof(out->host)) {
        fprintf(stderr, "Hostname too long\n");
        return -1;
    }

    // copy that string to trakcer_url.host
    memcpy(out->host, start, host_len);

    // add null terminator for the string 
    out->host[host_len] = '\0';

    // convert port to interger and set to the struct
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
    const int max_retries = 5;
    int timeout = 5; 
    socklen_t addr_len = sizeof(*tracker_addr);

    // unlike in TCP where it automatically handles connection retries, in UDP we have to do it on your own

    for(int attempt = 0;  attempt < max_retries; attempt++) {
        // preparet Connect message struct 
        ConnectRequest req = {0};
        req.protocol_id = swap64(PROTOCOL_ID);
        req.action = htonl(ACTION_CONNECT);
        u32 tx_id = random_transaction_id();
        req.transaction_id = htonl(tx_id);

        printf("Sending connect request...\n");
        if (sendto(sock, (char*)&req, sizeof(req), 0, (struct sockaddr*)tracker_addr, sizeof(*tracker_addr)) < 0) {
            perror("sendto failed");
            return -1;
        }

        int wait = wait_for_response(sock, timeout);

        // if wait == 1 then then the request was successful and we can begin to serialize the respnse
        if(wait == 1) { 
            ConnectResponse resp = {0};
            ssize_t n = recvfrom(sock, (char*)&resp, sizeof(resp), 0, (struct sockaddr*)tracker_addr, &addr_len);

            // if packet received is less than than the Connect response size then not valid Connect Response
            if (n < (ssize_t)sizeof(resp)) {
                fprintf(stderr, "Invalid connect response size\n");
                return -1;
            }

            // if greater then get connection ID
            if (n >= (ssize_t)sizeof(resp)) {
                resp.action = ntohl(resp.action);
                resp.transaction_id = ntohl(resp.transaction_id);
                resp.connection_id = be64toh_custom(resp.connection_id);

                if(resp.action == ACTION_CONNECT && resp.transaction_id  == tx_id) {
                    // get connection ID to be used for the next udp communication 
                    *connection_id = resp.connection_id;
                    printf("Connected! Connection ID: %llx\n", (unsigned long long)*connection_id);
                    return 0;
                }
            }
        }

        printf("Connect attempt failed\n");
        timeout *= 2;  // exponential backoff
    }

    fprintf(stderr, "Connect failed after retries\n");
    return -1;
}

// Step 2: Announce to tracker
int tracker_announce(socket_t sock, struct sockaddr_in *tracker_addr, u64 connection_id, const u8 info_hash[20], Peer **peers_out, size_t *peer_count_out, Arena *arena) {
    const int max_retries = 5;
    int timeout = 5; 
    socklen_t addr_len = sizeof(*tracker_addr);

    for(int attempt = 0;  attempt < max_retries; attempt++) {
        AnnounceRequest req = {0};
        /*req.connection_id = htobe64_custom(connection_id);*/
        req.connection_id = swap64(connection_id);
        req.action = htonl(ACTION_ANNOUNCE);

        u32 tx_id = random_transaction_id();
        req.transaction_id = htonl(tx_id);


        memcpy(req.info_hash, info_hash, 20);

        generate_peer_id(req.peer_id);

        req.downloaded = 0;
        /*req.left = htobe64_custom(0);  // Pretend we have everything for now*/
        req.left = swap64(0);  // Pretend we have everything for now
        req.uploaded = 0;
        req.event = htonl(2);  // 2 = started
        req.ip_address = 0;
        req.key = htonl(rand());
        req.num_want = htonl(50);  // Request 50 peers
        req.port = htons(6881);    // Standard BitTorrent port


        printf("Announce attempt %d (timeout=%d sec)\n", attempt + 1, timeout);

        printf("Sending announce request...\n");
        if (sendto(sock, (char*)&req, sizeof(req), 0, (struct sockaddr*)tracker_addr, sizeof(*tracker_addr)) < 0) {
            perror("sendto failed");
            return -1;
        }


        int wait = wait_for_response(sock, timeout);

        if(wait == 1) { 
            // Receive response (header + peers)
            u8 buffer[2048];
            ssize_t n = recvfrom(sock, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)tracker_addr, &addr_len);

            if (n >= (ssize_t)sizeof(AnnounceResponse)) {
                AnnounceResponse resp;
                memcpy(&resp, buffer, sizeof(resp));

                resp.action = ntohl(resp.action);
                resp.transaction_id = ntohl(resp.transaction_id);
                resp.interval = ntohl(resp.interval);
                resp.leechers = ntohl(resp.leechers);
                resp.seeders = ntohl(resp.seeders);

                if (resp.action == ACTION_ANNOUNCE && resp.transaction_id == tx_id) {
                    printf("\nTracker Stats:\n");
                    printf("  Seeders: %u\n", resp.seeders);
                    printf("  Leechers: %u\n", resp.leechers);
                    printf("  Interval: %u seconds\n", resp.interval);

                    // Parse peer list
                    size_t peer_data_size = n - sizeof(AnnounceResponse);

                    if (peer_data_size % sizeof(Peer) != 0) {
                        fprintf(stderr, "Malformed peer list\n");
                        return -1;
                    }


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

                    printf("Announce successful!\n");
                    return 0;
                }
            }
        }

        printf("Announce attempt failed\n");
        timeout *= 2;
    }

    fprintf(stderr, "Announce failed after retries\n");
    return -1;
}



// total size is 68 bytes
typedef struct {
    u8 pstrlen; // 19 length of string "BitTorrent protocol"
    u8 pstr[19]; // the string "BitTorrent protocol" 
    u8 reserved[8]; // Feature flags, all zeros for now
    u8 info_hash[20]; // SHA-1 hash of info dict
    u8 peer_id[20]; // our peer ID
} __attribute__((packed)) Handshake; // not packed bytes


Handshake *build_handshake(Arena *arena, const u8 info_hash[20], const u8 peer_id[20]) {
    Handshake *hs = arena_alloc(arena, sizeof(Handshake));

    hs->pstrlen = 19;
    memcpy(hs->pstr,  "BitTorrent protocol", 19);
    memset(hs->reserved, 0, 8);
    memcpy(hs->info_hash, info_hash, 20);
    memcpy(hs->peer_id, peer_id, 20);

    return hs;
}



PeerConnection *peer_create(char ip[20], u16 port, Arena *arena) {
    PeerConnection *peer = arena_alloc(arena, sizeof(PeerConnection));

    memset(peer, 0, sizeof(PeerConnection));

    // initialize peer conneciton struct
    strncpy(peer->ip, ip, sizeof(peer->ip) - 1);
    peer->port = port;
    peer->am_choking = 1;
    peer->am_interested = 0;
    peer->peer_choking = 1;
    peer->peer_interested = 0;
    peer->last_message_time = time(NULL);

    return peer;
}

// connect to peer
// 1. if peer is connectable then connect to next peer
//    get peer list instead and if timeout lapses then try connecting to next peer;


// Message types
typedef enum {
    MSG_KEEP_ALIVE = 20,
    MSG_CHOKE = 0,
    MSG_UNCHOKE = 1,
    MSG_INTERESTED = 2,
    MSG_NOT_INTERESTED = 3,
    MSG_HAVE = 4,
    MSG_BITFIELD = 5,
    MSG_REQUEST = 6,
    MSG_PIECE = 7,
    MSG_CANCEL = 8,
    MSG_PORT = 9
} MessageType;



// Add this helper function to check if peer has a specific piece
// STEP 1: Divide the piece index by 8 to get the byte index
// STEP 2: GET the bit index of the piece index 
// STEP 3: SHIFT the byte from the bit index to the right using >> (bit index) 
// STEP 4: Mask with & 1; Result will be either 1(peer has a piece) or 0(peer has no piece)
int peer_has_piece(PeerConnection *peer, u32 piece_index) {
    if (!peer->bitfield) return 0;
    
    u32 byte_idx = piece_index / 8;
    u32 bit_idx = 7 - (piece_index % 8);  // BitTorrent uses big-endian bit ordering
    
    if (byte_idx >= peer->bitfield_len) return 0;
    
    return (peer->bitfield[byte_idx] >> bit_idx) & 1;
}


// Print bitfield for debugging
void print_bitfield(PeerConnection *peer, u32 total_pieces) {
    printf("Bitfield visualization (first 80 pieces):\n");
    for (u32 i = 0; i < total_pieces && i < 80; i++) {
        printf("%c", peer_has_piece(peer, i) ? '#' : '.');
        if ((i + 1) % 40 == 0) printf("\n");
    }
    printf("\n");
}

// Receive a message from peer
// Typical Bittorent peer message strucuture: [4 bytes: length][1 byte: type][N bytes: payload]
int peer_recv_message(PeerConnection *peer, u8 *type_out, u8 *payload, u32 *payload_len_out, u32 max_payload) {
    // Read length field (4 bytes)
    u8 len_bytes[4];
    // copy  4 bytes of the bittorent message to the len bytes buffer
    // this moves the kernel pointer up to 4 bytes 
    // subsequent recv will copy from  from message type and payload
    ssize_t n = recv(peer->sock, (char*)len_bytes, 4, 0);
    /*if (n != 4) {*/
    /*    if (n == 0) {*/
    /*        printf("Peer closed connection\n");*/
    /*    } else if (n < 0) {*/
    /*        perror("recv");*/
    /*    } else {*/
    /*        fprintf(stderr, "Incomplete length field\n");*/
    /*    }*/
    /*    return -1;*/
    /*}*/


    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // timeout expired, no data yet — not a real error
            return -2;  // distinct return code for timeout
        }
        perror("recv");
        return -1;  // real error
    }
    if (n == 0) {
        printf("Peer closed connection\n");
        return -1;
    }
    
    u32 msg_len = ntohl(*(u32*)len_bytes);
    
    // Keep-alive message (length = 0)
    // A length of 0 is a special case — it means keep-alive, which has no type byte and no payload
    // the next message might be a keep alive message and not necessarily a bitfield message so make sure you handle that
    if (msg_len == 0) {
        *payload_len_out = 0;
        *type_out = MSG_KEEP_ALIVE;
        peer->last_message_time = time(NULL);
        return 0;
    }
    
    // Read type + payload
    // this is just there to make sure that the bitfield message is not larger than the size we allocated to store the bitfield, the +1 is the byte for the message type
    if (msg_len > max_payload + 1) {
        fprintf(stderr, "Message too large: %u bytes\n", msg_len);
        return -1;
    }
    
    // create space to store the bitfield
    u8 buffer[msg_len];
    // to track the amount we have copied
    size_t total = 0;

    // receive and copy  the message to buffer
    while (total < msg_len) {
        n = recv(peer->sock, (char*)buffer + total, msg_len - total, 0);
        if (n <= 0) {
            fprintf(stderr, "Connection closed or error\n");
            return -1;
        }
        total += n;
    }
    
    // save the message type
    *type_out = buffer[0];


    // for the payload received
    *payload_len_out = msg_len - 1;
    if (*payload_len_out > 0) {
        memcpy(payload, buffer + 1, *payload_len_out);
    }
    
    peer->last_message_time = time(NULL);
    return 0;
}

// Count how many pieces the peer has
u32 peer_count_pieces(PeerConnection *peer) {
    if (!peer->bitfield) return 0;
    
    u32 count = 0;
    for (size_t i = 0; i < peer->bitfield_len * 8; i++) {
        if (peer_has_piece(peer, i)) {
            count++;
        }
    }
    return count;
}

// Receive and parse bitfield message
int peer_receive_bitfield(PeerConnection *peer, u32 expected_pieces, Arena *arena) {
    printf("Waiting for bitfield message...\n");
    
    u8 msg_type;
    u8 payload[8192];  // Bitfield can be large for torrents with many pieces
    u32 payload_len;
    
    // Set a timeout for receiving bitfield
    int timeout_attempts = 0;
    while (timeout_attempts < 10) {
        if (peer_recv_message(peer, &msg_type, payload, &payload_len, sizeof(payload)) < 0) {
            timeout_attempts++;
            continue;
        }
        
        if (msg_type == MSG_BITFIELD) {
            printf("✓ Received bitfield message (%u bytes)\n", payload_len);
            
            // Validate bitfield size
            // Bitfield should be ceil(num_pieces / 8) bytes
            u32 expected_bitfield_len = (expected_pieces + 7) / 8;
            
            if (payload_len < expected_bitfield_len) {
                fprintf(stderr, "Warning: Bitfield smaller than expected (%u < %u)\n", 
                        payload_len, expected_bitfield_len);
            }
            
            if (payload_len > expected_bitfield_len) {
                fprintf(stderr, "Warning: Bitfield larger than expected (%u > %u)\n", 
                        payload_len, expected_bitfield_len);
            }
            
            // Store bitfield
            peer->bitfield = arena_alloc(arena, payload_len);
            memset(peer->bitfield, 0, payload_len);
            memcpy(peer->bitfield, payload, payload_len);
            peer->bitfield_len = payload_len;
            
            printf("Bitfield hexdump");
            hexdump_ascii(peer->bitfield, payload_len);
            // Count pieces
            u32 pieces_count = peer_count_pieces(peer);
            printf("  Peer has %u/%u pieces\n", pieces_count, expected_pieces);
            print_bitfield(peer, expected_pieces);
            
            return 0;
            
        } else if (msg_type == MSG_KEEP_ALIVE) {
            // Ignore keep-alive
            continue;
            
            // MSG_HAVE means the peer just finished downloading a piece and is announcing it. 
        } else if (msg_type == MSG_HAVE) {
            // Some peers may send HAVE messages instead of/before bitfield
            printf("Note: Peer sent HAVE message before bitfield\n");
            timeout_attempts++;
            continue;
        } else {
            printf("Unexpected message before bitfield: type=%u\n", msg_type);
            timeout_attempts++;
            continue;
        }
    }
    
    fprintf(stderr, "Timeout waiting for bitfield\n");
    return -1;
}


// Step 1: Connect to peer and perform handshake
u32 peer_connect(PeerConnection *peer, const u8 info_hash[20], const u8 peer_id[20], Arena *arena, u32 num_pieces) {
    printf("Connecting to peer %s:%u\n", peer->ip, peer->port);
    
    // Resolve IP
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer->port);
    
    if (inet_pton(AF_INET, peer->ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", peer->ip);
        return -1;
    }
    
    // Create TCP socket
    peer->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (peer->sock == INVALID_SOCKET_VALUE) {
        perror("socket");
        return -1;
    }
    
    int flags = fcntl(peer->sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close_socket(peer->sock);
        return -1;
    }
    if (fcntl(peer->sock, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close_socket(peer->sock);
        return -1;
    }
    
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    if (setsockopt(peer->sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval*)&tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
    }
    if (setsockopt(peer->sock, SOL_SOCKET, SO_SNDTIMEO, (struct timeval*)&tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_SNDTIMEO");
    }

    printf("Attempting connection (10 second timeout)...\n\n\n\n");
    

    flags = fcntl(peer->sock, F_GETFL, 0);
    fcntl(peer->sock, F_SETFL, flags | O_NONBLOCK);
    
    int ret = connect(peer->sock, (struct sockaddr*)&addr, sizeof(addr));
    
    int error_code = errno;
    int in_progress = (error_code == EINPROGRESS);
    
    // If connection is in progress or would block, wait with select
    if (ret < 0 && in_progress) {
        fd_set writefds, errorfds;
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        FD_ZERO(&writefds);
        FD_ZERO(&errorfds);
        FD_SET(peer->sock, &writefds);
        FD_SET(peer->sock, &errorfds);
        
        ret = select((int)peer->sock + 1, NULL, &writefds, &errorfds, &tv);
        
        if (ret < 0) {
            perror("select");
            close_socket(peer->sock);
            return -1;
        } else if (ret == 0) {
            fprintf(stderr, "Connection timeout\n");
            close_socket(peer->sock);
            return -1;
        } else if (FD_ISSET(peer->sock, &errorfds)) {
            fprintf(stderr, "Connection refused or failed\n");
            close_socket(peer->sock);
            return -1;
        }
        
        // Connection succeeded, verify it with getsockopt
        int connect_error = 0;
        socklen_t len = sizeof(connect_error);
        
        if (getsockopt(peer->sock, SOL_SOCKET, SO_ERROR, (char*)&connect_error, &len) < 0) {
            perror("getsockopt");
            close_socket(peer->sock);
            return -1;
        }
        
        if (connect_error != 0) {
            fprintf(stderr, "Connection failed: %s\n", strerror(connect_error));
            close_socket(peer->sock);
            return -1;
        }
    } else if (ret < 0) {
        perror("connect");
        close_socket(peer->sock);
        return -1;
    }
    
    flags = fcntl(peer->sock, F_GETFL, 0);
    fcntl(peer->sock, F_SETFL, flags & ~O_NONBLOCK);
    
    printf("Connected! Sending handshake...\n");
    printf("Connected to peer %s:%u\n", peer->ip, peer->port);


      // Send handshake
    // Handshake hs = {0};
    // hs.pstrlen = 19;
    // memcpy(hs.pstr, "BitTorrent protocol", 19);
    // memset(hs.reserved, 0, 8);
    // memcpy(hs.info_hash, info_hash, 20);
    // memcpy(hs.peer_id, peer_id, 20);
    Handshake *hs = build_handshake(arena, info_hash, peer_id);

    // if what is received from peer is not equal to size of handshake or 68 bytes then
    if(send(peer->sock, (char*)hs, sizeof(Handshake), 0)  != sizeof(Handshake)) {
        fprintf(stderr, "Failed to send handshake\n");
        close_socket(peer->sock);
        return -1;
    }

    //  Handshake hs_recv = {0};
    Handshake *hs_recv = arena_alloc(arena, sizeof(Handshake));
    memset(hs_recv, 0, sizeof(Handshake));

    isize n = recv(peer->sock, (char*)hs_recv, sizeof(Handshake), 0);

    if(n != sizeof(Handshake)) {
        fprintf(stderr, "Failed to receive handshake (got %zu bytes)\n", n);
        close_socket(peer->sock);
        return -1;
    }


    if (hs_recv->pstrlen != 19 || memcmp(hs_recv->pstr, "BitTorrent protocol", 19) != 0) {
        fprintf(stderr, "Failed to receive handshake (got %zu bytes)\n", n);
        close_socket(peer->sock);
        return -1;
    }



    if (memcmp(hs_recv->info_hash, info_hash, 20) != 0) {
        fprintf(stderr, "Info hash mismatch!\n");
        close_socket(peer->sock);
        return -1;
    }

    printf("✓ Handshake successful! Connected to peer %s:%u\n", peer->ip, peer->port);


    // Afer receiving a handshake you may receive bitfield message which is an arry of bytes of the pieces the peers have 
    // Each bit in the byte array states whether the peer has a piece or not which we will use to ask the piece from a peer

    peer_receive_bitfield(peer, num_pieces, arena);
    
    peer->last_message_time = time(NULL);
    return 0;
}


// Send a message to peer
int peer_send_message(PeerConnection *peer, u8 type, const u8 *payload, u32 payload_len) {
    // Build message: [length (4 bytes)][type (1 byte)][payload]
    u8 buffer[payload_len + 5];
    u32 message_len = htonl(payload_len + 1);  // +1 for type byte
    
    memcpy(buffer, &message_len, 4);
    buffer[4] = type;
    if (payload_len > 0) {
        memcpy(buffer + 5, payload, payload_len);
    }
    
    if (send(peer->sock, (char*)buffer, sizeof(buffer), 0) != (ssize_t)sizeof(buffer)) {
        perror("send");
        return -1;
    }
    
    peer->last_message_time = time(NULL);
    return 0;
}



typedef struct {
    u32 index;
    u32 begin;
    u32 length;

} __attribute__((packed)) RequestMessage;


typedef struct {
    u32 index;
    u32 begin;
}__attribute__((packed)) PieceMessage;


int peer_request_block(PeerConnection *peer, u32 piece_index, u32 block_offset, u32 block_size) {
    RequestMessage req = {
        .index = htonl(piece_index),
        .begin = htonl(block_offset),
        .length = htonl(block_size)
    };
    
    return peer_send_message(peer, MSG_REQUEST, (u8*)&req, sizeof(req));
}



int peer_download_piece(PeerConnection *peer, u32 piece_index, u32 piece_size, u8 *piece_data) {
    printf("\nDownloading piece %u from %s:%u\n", piece_index, peer->ip, peer->port);
    
    // Send interested message
    if (peer_send_message(peer, MSG_INTERESTED, NULL, 0) < 0) {
        fprintf(stderr, "Failed to send interested\n");
        return -1;
    }
    peer->am_interested = 1;
    
    // Wait for unchoke
    printf("Waiting for peer to unchoke...\n");
    int got_unchoke = 0;
    int timeout_count = 0;
    
    while (!got_unchoke && timeout_count < 30) {
        u8 msg_type;
        u8 payload[1024];
        u32 payload_len;

//        if (peer_recv_message(peer, &msg_type, payload, &payload_len, sizeof(payload)) < 0) {
//            timeout_count++;
//            continue;
//        }

        int result = peer_recv_message(peer, &msg_type, payload, &payload_len, sizeof(payload));


        if (result == -2) {
            // just a timeout, keep waiting, don't increment counter
            continue;
        }
        if (result < 0) {
            // real error, give up on this peer
            return -1;
        }
        
        
        switch (msg_type) {
            case MSG_UNCHOKE:
                printf("Peer unchoked!\n");
                peer->peer_choking = 0;
                got_unchoke = 1;
                break;
            case MSG_CHOKE:
                printf("Peer choked\n");
                peer->peer_choking = 1;
                break;
            case MSG_BITFIELD:
                printf("Received bitfield (%u bytes)\n", payload_len);
                break;
            case MSG_HAVE:
                break;  // Ignore individual have messages
            case MSG_KEEP_ALIVE:
                break;
            default:
                printf("Unexpected message type: %u\n", msg_type);
        }
    }
    
    if (!got_unchoke) {
        fprintf(stderr, "Timeout waiting for unchoke\n");
        return -1;
    }
    
    // Download piece in blocks
    u32 downloaded = 0;
    while (downloaded < piece_size) {
        u32 block_size = (piece_size - downloaded < BLOCK_SIZE) ?  (piece_size - downloaded) : BLOCK_SIZE;
        
        printf("Requesting block at offset %u (size %u)...\n", downloaded, block_size);
        
        if (peer_request_block(peer, piece_index, downloaded, block_size) < 0) {
            fprintf(stderr, "Failed to request block\n");
            return -1;
        }
        
        // Receive piece message
        u8 msg_type;
        u8 payload[BLOCK_SIZE + 8];
        u32 payload_len;
        
        int retries = 0;
        while (retries < 3) {
            if (peer_recv_message(peer, &msg_type, payload, &payload_len, sizeof(payload)) < 0) {
                retries++;
                continue;
            }
            
            if (msg_type == MSG_PIECE) {
                if (payload_len < 8) {
                    fprintf(stderr, "Invalid piece message\n");
                    retries++;
                    continue;
                }
                
                PieceMessage *pm = (PieceMessage*)payload;
                u32 recv_index = ntohl(pm->index);
                u32 recv_begin = ntohl(pm->begin);
                u32 data_len = payload_len - 8;
                
                if (recv_index != piece_index || recv_begin != downloaded) {
                    printf("Unexpected piece data (expected index %u offset %u, got %u offset %u)\n",
                           piece_index, downloaded, recv_index, recv_begin);
                    retries++;
                    continue;
                }
                
                memcpy(piece_data + downloaded, payload + 8, data_len);
                downloaded += data_len;
                printf("Downloaded %u/%u bytes\n", downloaded, piece_size);
                break;
                
            } else if (msg_type == MSG_KEEP_ALIVE) {
                continue;
            } else if (msg_type == MSG_CHOKE) {
                fprintf(stderr, "Peer choked us\n");
                return -1;
            } else if (msg_type == MSG_HAVE) {
                continue;  // ← add this, don't count as retry
            } else {
                printf("Unexpected message type: %u\n", msg_type);
                retries++;
            }
        }
        
        if (retries >= 3) {
            fprintf(stderr, "Failed to receive block\n");
            return -1;
        }
    }
    
    printf("Piece %u downloaded successfully!\n", piece_index);
    return 0;
}

// Main tracker communication function
int communicate_with_tracker(const char *tracker_url, const u8 info_hash[20], Arena *arena, TorrentFile *torrent_file) {
    if (init_networking() < 0) {
        return -1;
    }

    TrackerURL url;
    if (parse_tracker_url(tracker_url, &url) < 0) {
        cleanup_networking();
        return -1;
    }

    printf("Connecting to tracker: %s:%d\n", url.host, url.port);

    // TODO: use getaddrinfo instead of this
    // Resolve hostname: get the ip address of the host name
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

    if (tracker_announce(sock, &tracker_addr, connection_id, info_hash, &peers, &peer_count, arena) < 0) {
        close_socket(sock);
        cleanup_networking();
        return -1;
    }


    if(peers && peer_count > 0) {
        print_peers(peers, peer_count);

        /*char *peer_ip[16];*/
        /*peer_ip_to_str(&peers[0], peer_ip);*/
        u8 peer_id[20];
        generate_peer_id(peer_id);

        PeerConnection **peer_cons = arena_alloc(arena, sizeof(PeerConnection *) * peer_count); // array of pointers

        for(usize i = 0; i < peer_count; i++) {
            char *peer_ip = arena_alloc(arena, 16);
            peer_ip_to_str(&peers[i], peer_ip);

            peer_cons[i] = peer_create(peer_ip, ntohs(peers[i].port), arena); // initialize
        }


        for(usize i = 0; i < peer_count; i++) {
            PeerConnection *peer_con = peer_cons[i];

            if (peer_connect(peer_con, info_hash, peer_id, arena, torrent_file->num_pieces) == 0) {
                u8 *piece_data = arena_alloc(arena, torrent_file->piece_length);

                peer_download_piece(peer_con, 0, torrent_file->piece_length, piece_data);
 
                // TODO: REMOVE:
                exit(1);
            }
        }

        
        /*PeerConnection *peer = peer_create(peer_ip, ntohs(peers[0].port), arena);*/
        /**/
        /*if (peer_connect(peer, info_hash, peer_id) == 0) {*/
        /*}*/
    }




    close_socket(sock);
    cleanup_networking();
    return 0;
}




void get_filenames(BcodeNode *info_dict) {
    BcodeNode *files = dict_get(info_dict, "files");
    if (files && files->type == BCODE_LIST) {
        for(size_t i = 0; i < files->list_val.count; i++) {
            BcodeNode *file_path = dict_get(files->list_val.items[i], "path");
            if (file_path && file_path->type == BCODE_LIST) {
                for(size_t i = 0; i < file_path->list_val.count; i++) {
                    BcodeNode *real_path = file_path->list_val.items[i];
                    if (real_path && real_path->type == BCODE_STRING) {
                        printf("%s: ", real_path->string_val.data);
                    }
                }
            }


            BcodeNode *file_length = dict_get(files->list_val.items[i], "length");
            if (file_length  && file_length->type == BCODE_INT) {
                printf("%zu\n", file_length->int_val);
            }

        }

    }

}

int main(int argc, char **argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }


    // << is the bitwise left shift operator. 
    // it means : 1 × 2^20  = 1 << 20 = 1,048,576 
    // That’s 1 megabyte (1 MB) in bytes. 
    // adding ULL makes it explicitly 64-bit and avoids overflow if you later change it to something 
    Arena arena = arena_create(MB(40)); 

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


    TorrentFile torrentFile = buildTorrentFile(&torrent, root, &arena);
    /*printf("%s\n", torrentFile.name);*/
    /*print_single_hash(torrentFile.info_hash);*/

    // Uncomment to see full structure
    // print_bcode(root, 0);
    /*split_pieces(root, &arena);*/



    BcodeNode *info_dict = dict_get(root, "info");
    if (info_dict && info_dict->type == BCODE_DICT) {
        // get file name
        BcodeNode *file_name = dict_get(info_dict, "name");
        if (file_name && file_name->type == BCODE_STRING) {
            printf("File Name by: %s\n\n", file_name->string_val.data);
        }
        /*get_filenames(info_dict);*/
    }



    // Get tracker URL
    BcodeNode *announce = dict_get(root, "announce");
    if (announce && announce->type == BCODE_STRING) {
        printf("Tracker: %s\n\n", announce->string_val.data);

        // Communicate with tracker
        communicate_with_tracker((char*)announce->string_val.data, torrentFile.info_hash, &arena, &torrentFile);
    }


    arena_destroy(&arena);
    return 0;
}
