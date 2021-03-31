// Copyright Epic Games, Inc. All Rights Reserved.
//#define  _WIN32_WINNT 0x0500
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

SYMS_API void
syms_init_os(void)
{
  return;
}

SYMS_API SymsUMM
syms_get_pagesize(void)
{
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwPageSize;
}
SYMS_API void *
syms_reserve_virtual_memory(SymsUMM size)
{
  void *p = VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
  return p;
}
SYMS_API syms_bool
syms_commit_virtual_memory(void *base, SymsUMM size)
{
  void *p = VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE);
  return (p != 0);
}
SYMS_API void
syms_free_virtual_memory(void *base, SymsUMM size)
{
  VirtualFree(base, size, MEM_RELEASE);
}

