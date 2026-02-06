#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include "arena.h"

typedef struct {
    char   *data;
    size_t  len;
} String;

// macro #define STR_LIT(s) (String){ (char *)(s), sizeof(s) - 1 } creates a
// String struct from a string literal s by providing its pointer and length
// without the null terminator.
#define STR_LIT(s) (String){ (char *)(s), sizeof(s) - 1 }


void string_print(String s); 
String string_copy(Arena *arena, const char *cstr);
String string_concat(Arena *arena, String a, String b);
int string_eq(String a, String b);
char *string_to_cstr(Arena *arena, String s);

#endif
