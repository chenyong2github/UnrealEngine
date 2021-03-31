// Copyright Epic Games, Inc. All Rights Reserved.
#if !defined(SYMS_PLATFORM_INCLUDE_H)
#define SYMS_PLATFORM_INCLUDE_H

SYMS_API void
syms_init_os(void);

SYMS_API SymsUMM
syms_get_pagesize(void);

SYMS_API void *
syms_reserve_virtual_memory(SymsUMM size);

SYMS_API syms_bool
syms_commit_virtual_memory(void *base, SymsUMM size);

SYMS_API void
syms_free_virtual_memory(void *base, SymsUMM size);

SYMS_INLINE void *
syms_virtual_alloc(SymsUMM size)
{
  void *base = syms_reserve_virtual_memory(size); 
  if (!syms_commit_virtual_memory(base, size)) {
    SYMS_ASSERT_FAILURE_PARANOID("unable to commit virtual memory");
    base = 0;
  }
  return base;
}

typedef struct
{
  void *base; // pointer to first byte in the file
  SymsUMM size; 
} SymsEntireFile;

SYMS_API SymsEntireFile 
syms_read_entire_file(const char *file_name);

SYMS_API void
syms_free_entire_file(SymsEntireFile *file);

#endif // SYMS_PLATFORM_INCLUDE_H

