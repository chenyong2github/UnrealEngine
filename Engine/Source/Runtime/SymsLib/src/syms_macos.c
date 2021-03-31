// Copyright Epic Games, Inc. All Rights Reserved.
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

SYMS_API void
syms_init_os()
{
}
SYMS_API SymsUMM
syms_get_pagesize(void)
{
  int size = getpagesize();
  return (SymsUMM)size;
}
SYMS_API void *
syms_reserve_virtual_memory(SymsUMM size)
{
  void *base = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  return base;
}
SYMS_API syms_bool
syms_commit_virtual_memory(void *base, SymsUMM size)
{
  int result = mprotect(base, size, PROT_READ|PROT_WRITE);
  if (result != 0) {
    switch (errno) {
    case EACCES:  SYMS_ASSERT_FAILURE("unable to change page flags"); break;
    case EINVAL:  SYMS_ASSERT_FAILURE("addres is not page-aligned"); break;
    case ENOTSUP: SYMS_ASSERT_FAILURE("invalid combination of protection flags"); break;
    default:      SYMS_ASSERT_FAILURE("mprotect");
    }
  }
  return (result == 0);
}
SYMS_API void
syms_free_virtual_memory(void *base, SymsUMM size)
{
  if (munmap(base, size) != 0)
    SYMS_ASSERT_FAILURE("unable to unmap memory");
}

