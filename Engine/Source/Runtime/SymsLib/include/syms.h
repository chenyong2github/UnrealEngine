// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_INCLUDE_H
#define SYMS_INCLUDE_H

/******************************************************************************
 * File   : syms.h                                                            *
 * Author : Nikita Smith                                                      *
 * Created: 2019/02/20                                                        *
 * Purpose: Provides an API for accessing debug information                   *
 ******************************************************************************/

typedef struct SymsBorrowedMemory
{
  SymsArena *arena;
  struct SymsBorrowedMemory *next;
} SymsBorrowedMemory;

struct SymsInstance
{
  syms_uint user_id;
  SymsArena *arena;
  SymsImage img;
  SymsDebugInfo debug_info;
  SymsMod null_mod;
  SymsAddr rebase; // base address that is used for calculating addresses of symbols, lines, and etc.
  syms_uint mods_num;
  SymsModInfo *mods;
  SymsBorrowedMemory *borrowed_memory;
  struct 
  {
    SymsErrorCode code;
    SymsString text;
  } error;
};


typedef union SymsProcData
{
  struct 
  {
    pdb_cvdata_token cvdata;
  } pdb;
  struct 
  {
    DwTag type_tag;
    DwAttrib frame_base;
    SymsOffset range_off;
  } dw;
} SymsProcData;
SYMS_COMPILER_ASSERT(sizeof(SymsProcData) <= sizeof(SymsProcImpl));

typedef struct SymsMemread 
{
  SymsErrorCode result;
  void *context;
  syms_memread_sig *callback;
} SymsMemread;

typedef struct
{
  SymsErrorCode result;
  SymsArch arch;
  void *context;
  syms_regread_sig *callback;
} SymsRegread;

typedef struct
{
  SymsErrorCode result;
  SymsArch arch;
  void *context;
  syms_regwrite_sig *callback;
} SymsRegwrite;

SYMS_INTERNAL SymsTypeID
syms_type_id_null(void);

SYMS_INTERNAL void
syms_line_init(SymsLine *line, SymsAddr va, syms_uint ln, syms_uint col);

SYMS_API SymsModInfo *
syms_get_mod(SymsInstance *instance, SymsModID mod_id);

SYMS_API SymsModID
syms_infer_global_data_module(SymsInstance *instance, SymsGlobal *gdata);
// Resolve module that contains global-data.

typedef struct SymsRangeMap
{
  SymsAddr lo;
  SymsAddr hi;
  syms_uint id;
} SymsRangeMap;

SYMS_API SymsRangeMap *
syms_mod_info_find_rangemap(SymsModInfo *m, syms_uint i);

SYMS_API SymsRangeMap *
syms_rangemap_search(SymsBlockAllocator *rangemap, SymsAddr va);

SYMS_INTERNAL SymsErrorCode
syms_memread(SymsMemread* info, SymsAddr va, void* buffer, syms_uint buffer_size);
SYMS_API SymsArenaFrame *
syms_begin_arena_frame(struct SymsArena *arena);

SYMS_API void
syms_end_arena_frame(struct SymsArenaFrame *frame);

#endif // SYMS_INCLUDE_H

