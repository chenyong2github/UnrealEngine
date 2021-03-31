// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_block_alloc.c                                                *
 * Author : Nikita Smith                                                      *
 * Created: 2021/02/21                                                        *
 ******************************************************************************/

SYMS_API SymsBlockAllocator *
syms_block_allocator_init(u32 push_size, u32 block_size, SymsArena *arena)
{
  SymsBlockAllocator *ba = syms_arena_push_struct(arena, SymsBlockAllocator);
  ba->push_size   = push_size;
  ba->push_count  = 0;
  ba->block_size  = block_size;
  ba->block_count = 0;
  ba->last_block_size = block_size;
  ba->first_block = 0;
  ba->next_block  = &ba->first_block;
  ba->arena       = arena;
  ba->index_table = 0;
  return ba;
}

SYMS_API void *
syms_block_allocator_push(SymsBlockAllocator *a)
{
  SymsBlockNode *b;
  if (a->last_block_size + a->push_size > a->block_size) {
    b = syms_arena_push_struct(a->arena, SymsBlockNode);
    b->base = syms_arena_push_array(a->arena, char, a->block_size);
    b->next = 0;

    *a->next_block = b;
    a->next_block = &b->next;

    a->last_block = b;
    a->last_block_size = 0;

    ++a->block_count;
  } else {
    b = a->last_block;
  }
  void *ptr = (char*)a->last_block->base + a->last_block_size;
  a->last_block_size += a->push_size;
  a->push_count += 1;
  return ptr;
}

SYMS_API void
syms_block_allocator_build_index_table(SymsBlockAllocator *a)
{
  SYMS_ASSERT(a->index_table == 0);
  a->index_table = syms_arena_push_array(a->arena, SymsBlockNode *, a->block_count);
  SymsBlockNode *b = a->first_block; 
  for (u32 i = 0; i < a->block_count; ++i) {
    a->index_table[i] = b;
    b = b->next;
  }
  SYMS_ASSERT(b == 0);
}

SYMS_API void *
syms_block_allocator_find_push(SymsBlockAllocator *a, u32 pi)
{
  SYMS_ASSERT(pi < a->push_count);
  u32 push_offset = pi * a->push_size;
  u32 block_index = push_offset / a->block_size;
  SymsBlockNode *block = a->index_table[block_index];
  u32 base_offset = push_offset - (block_index * a->block_size);
  SYMS_ASSERT(base_offset + a->push_size <= a->block_size);
  void *p = (void*)(block->base + base_offset);
  return p;
}

SYMS_API void
syms_block_allocator_swap(SymsBlockAllocator *a, u32 l, u32 r)
{
  SymsArenaFrame frame = syms_arena_frame_begin(a->arena);
  u8 *t = syms_arena_push_array(frame.arena, u8, a->push_size);
  void *pl = syms_block_allocator_find_push(a, (u32)l);
  void *pr = syms_block_allocator_find_push(a, (u32)r);
  syms_memcpy(t, pl, a->push_size);
  syms_memcpy(pl, pr, a->push_size);
  syms_memcpy(pr, t, a->push_size);
  syms_arena_frame_end(frame);
}

SYMS_INTERNAL void
syms_block_allocator_ins_sort(SymsBlockAllocator *allocator, s32 n, s32 (*compare)(const void *a, const void *b))
{
  SymsArenaFrame frame = syms_arena_frame_begin(allocator->arena);
  u8 *t = syms_arena_push_array(allocator->arena, u8, allocator->push_size);
  for (s32 i = 1; i < n; ++i) {
    s32 j = i;
    
    void *a = syms_block_allocator_find_push(allocator, i);
    syms_memcpy(t, a, allocator->push_size);
    a = t;

    while (j > 0) {
      void *b = syms_block_allocator_find_push(allocator, j-1);
      s32 c = compare(a, b);
      if (c == 0) break;
      syms_block_allocator_swap(allocator, j, j-1);
      --j;
    }
    if (i != j) {
      void *dst = syms_block_allocator_find_push(allocator, j);
      syms_memcpy(dst, t, allocator->push_size);
    }
  }
  syms_arena_frame_end(frame);
}

SYMS_INTERNAL void
syms_block_allocator_quicksort(SymsBlockAllocator *allocator, s32 w, s32 n, s32 (*compare)(const void *a, const void *b))
{
  while (n > 12) {
    s32 m = n >> 1;
    s32 c01 = compare(syms_block_allocator_find_push(allocator, w + 0), syms_block_allocator_find_push(allocator, w + m));
    s32 c12 = compare(syms_block_allocator_find_push(allocator, w + m), syms_block_allocator_find_push(allocator, w + n-1));
    if (c01 != c12) {
      s32 c = compare(syms_block_allocator_find_push(allocator, w + 0), syms_block_allocator_find_push(allocator, w + n-1));
      s32 z = (c == c12) ? 0 : n-1;
      syms_block_allocator_swap(allocator, w + z, w + m);
    }
    syms_block_allocator_swap(allocator, w + 0, w + m);
    s32 i = 1;
    s32 j = n-1;
    for (;;) {
      for (;;++i) {
        void *a = syms_block_allocator_find_push(allocator, w + i);
        void *b = syms_block_allocator_find_push(allocator, w + 0);
        if (!compare(a,b)) break;
      }
      for (;;--j) {
        void *a = syms_block_allocator_find_push(allocator, w + 0);
        void *b = syms_block_allocator_find_push(allocator, w + j);
        if (!compare(a,b)) break;
      }
      if (i >= j) break;
      syms_block_allocator_swap(allocator, w + i, w + j);
      ++i;
      --j;
    }
    if (j < (n-i)) {
      syms_block_allocator_quicksort(allocator, w + 0, j, compare);
      w += i;
      n = n-i;
    } else {
      syms_block_allocator_quicksort(allocator, w + i, n - i, compare);
      n = j;
    }
  }
}

SYMS_API void
syms_block_allocator_sort(SymsBlockAllocator *a, s32 (*compare)(const void *a, const void *b))
{
  if (a->push_count > 1) {
    // NOTE: butchered quicksort from stb.h
    syms_block_allocator_quicksort(a, 0, a->push_count, compare);
    syms_block_allocator_ins_sort(a, a->push_count, compare);
  }
}

