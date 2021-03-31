// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_BLOCK_ALLOC_H
#define SYMS_BLOCK_ALLOC_H

typedef struct SymsBlockNode
{
  char *base;
  struct SymsBlockNode *next;
} SymsBlockNode;

struct SymsBlockAllocator
{
  syms_uint push_size;
  syms_uint push_count;
  syms_uint block_size;
  syms_uint block_count;
  syms_uint last_block_size;
  SymsBlockNode *first_block;
  SymsBlockNode **next_block;
  SymsBlockNode *last_block;
  SymsArena *arena;
  SymsBlockNode **index_table;
};

SYMS_API SymsBlockAllocator *
syms_block_allocator_init(syms_uint push_size, syms_uint block_size, SymsArena *arena);

SYMS_API void *
syms_block_allocator_push(SymsBlockAllocator *a);

SYMS_API void
syms_block_allocator_build_index_table(SymsBlockAllocator *a);

SYMS_API void *
syms_block_allocator_find_push(SymsBlockAllocator *a, syms_uint pi);

SYMS_API void
syms_block_allocator_sort(SymsBlockAllocator *a, syms_int (*compare)(const void *a, const void *b));

#endif // SYMS_BLOCK_ALLOC_H
