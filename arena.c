#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>


Arena  arena_create(size_t capacity){
	Arena arena;

	arena.buffer = malloc(capacity);
	assert(arena.buffer && "Arena allocation failed");
	arena.capacity = capacity;
	arena.offset = 0;

	return arena;
}

void *arena_alloc(Arena *arena, size_t size) {
	assert(arena->offset + size <= arena->capacity &&  "Arena out of memory");

	void *ptr = arena->buffer + arena->offset;
	arena->offset += size;

	return ptr;
}


void  arena_reset(Arena *arena) {
	arena->offset = 0;
}

void  arena_destroy(Arena *arena) {
	free(arena->buffer);
	arena->buffer = NULL;
	arena->capacity = 0;
	arena->offset = 0;
}

static size_t align_forward(size_t ptr, size_t align) {
	size_t mod = ptr & (align - 1);
	if (mod) {
		ptr += align - mod;
	}

	return ptr;
}

void  *arena_alloc_aligned(Arena *arena, size_t size, size_t align) {
	size_t current = (size_t)(arena->buffer + arena->offset);
	size_t aligned = align_forward(current, align);
	size_t new_offset = (aligned - (size_t)arena->buffer) + size;

	assert(new_offset <= arena->capacity);
	arena->offset = new_offset;
	return (void *)aligned;
}
