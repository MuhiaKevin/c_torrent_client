#include <stdio.h>
#include "string.h"
#include "arena.h"
#include "string.h"
#include <string.h>


String string_copy(Arena *arena, const char *cstr) {
    size_t len = strlen(cstr);
    char *data = arena_alloc(arena, len);
    memcpy(data, cstr, len);
    return (String){ data, len };
}

String string_concat(Arena *arena, String a, String b) {
    char *data = arena_alloc(arena, a.len + b.len);
    memcpy(data, a.data, a.len);
    memcpy(data + a.len, b.data, b.len);
    return (String){ data, a.len + b.len };
}



int string_eq(String a, String b) {
    if (a.len != b.len) return 0;
    return memcmp(a.data, b.data, a.len) == 0;
}


char *string_to_cstr(Arena *arena, String s) {
    char *cstr = arena_alloc(arena, s.len + 1);
    memcpy(cstr, s.data, s.len);
    cstr[s.len] = '\0';
    return cstr;
}


String string_slice(String s, size_t start, size_t len) {
    return (String){ s.data + start, len };
}

int string_starts_with(String s, String prefix) {
    if (prefix.len > s.len) return 0;
    return memcmp(s.data, prefix.data, prefix.len) == 0;
}

void string_print(String s) {
    printf("%.*s", (int)s.len, s.data);
}
