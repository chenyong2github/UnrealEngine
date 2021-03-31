// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_DWARF_API_INCLUDE_H
#define SYMS_DWARF_API_INCLUDE_H

/******************************************************************************
 * File   : syms_dwarf.h                                                      *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/20                                                        *
 * Purpose: Declarations for dwarf.h interface                                *
 ******************************************************************************/

typedef struct SymsDebugInfoDwarf 
{
  DwContext context;
} SymsDebugInfoDwarf;

typedef struct 
{
   SymsString compile_dir;
   SymsString dir;
   SymsString file;
} DwFilePath;

typedef struct SymsModIterDwarf 
{
  DwCuIter impl;
} SymsModIterDwarf;

typedef struct SymsLineIterDwarf 
{
  DwLineIter impl;
  DwDirIndex prev_file_index;
  DwCompileUnit *cu;
} SymsLineIterDwarf;

typedef struct SymsMemberIterDwarf 
{
  DwMemberIter impl;
} SymsMemberIterDwarf;

typedef struct SymsGlobalIterDwarf 
{
  U32 reserved;
} SymsGlobalIterDwarf;

typedef struct SymsLocalDataIterDwarf 
{
  U32 reserved;
} SymsLocalDataIterDwarf;

typedef struct SymsFileIterDwarf 
{
  DwFileIter impl;
} SymsFileIterDwarf;

typedef struct SymsProcIterDwarf 
{
  DwProcIter impl;
} SymsProcIterDwarf;

typedef struct SymsArgIterDwarf 
{
  U32 reserved;
} SymsArgIterDwarf;

typedef struct SymsLocalIterDwarf 
{
  DwLocalIter impl;
} SymsLocalIterDwarf;

typedef struct SymsInlineIterDwarf 
{
  SymsAddr rva;
  DwCompileUnit *cu;
  DwAttribIter attribs;
  U32 depth;
} SymsInlineIterDwarf;

typedef struct SymsConstIterDwarf
{
  U32 reserved;
} SymsConstIterDwarf;

typedef struct SymsTypeIterDwarf 
{
  U32 reserved;
} SymsTypeIterDwarf;

typedef struct SymsRangeDwarf
{
  syms_uint cu_index;
  SymsAddr range_off;
} SymsRangeDwarf;

typedef struct
{
  DwContext context;
  DwCuIter cu_iter;
  DwAttribIter att_iter;
} SymsDebugFileIterDwarf;

SYMS_COMPILER_ASSERT(sizeof(SymsUWord) >= 8);
SYMS_COMPILER_ASSERT(sizeof(SymsUWord) >= 4);
SYMS_COMPILER_ASSERT(SYMS_GET_MEMBER_SIZE(DwContext, secs) == SYMS_GET_MEMBER_SIZE(DwInitdata, secs));
SYMS_COMPILER_ASSERT(sizeof(SymsRangeDwarf) <= sizeof(SymsRangeImpl));
SYMS_COMPILER_ASSERT(sizeof(DwTag) <= sizeof(SymsTypeIDImpl));
SYMS_COMPILER_ASSERT(sizeof(DwType) <= sizeof(SymsTypeImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsDebugInfoDwarf) <= sizeof(SymsDebugInfoImpl));
SYMS_COMPILER_ASSERT(sizeof(DwFilePath) <= sizeof(SymsStringRefImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsModIterDwarf) <= sizeof(SymsModIterImpl));
SYMS_COMPILER_ASSERT(sizeof(DwCompileUnit) <= sizeof(SymsModImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLineIterDwarf) <= sizeof(SymsLineIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsMemberIterDwarf) <= sizeof(SymsMemberIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsGlobalIterDwarf) <= sizeof(SymsGlobalIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLocalDataIterDwarf) <= sizeof(SymsLocalDataIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsFileIterDwarf) <= sizeof(SymsFileIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsArgIterDwarf) <= sizeof(SymsArgIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLocalIterDwarf) <= sizeof(SymsLocalIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsInlineIterDwarf) <= sizeof(SymsInlineIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsDebugFileIterDwarf) <= sizeof(SymsDebugFileIterImpl));
typedef DW_MEMREAD_SIG(SymsDwMemreadSig);
typedef DW_REGREAD_SIG(SymsDwRegreadSig);
typedef DW_REGWRITE_SIG(SymsDwRegwriteSig);

// -----------------------------------------------------------------------------

SYMS_INTERNAL
DW_REGREAD_SIG(syms_regread_dwarf);

SYMS_INTERNAL
DW_MEMREAD_SIG(syms_memread_dwarf);

SYMS_INTERNAL syms_bool
syms_dw_type_to_syms_type(DwType *dw_type, SymsType *syms_type);

SYMS_INTERNAL syms_bool
syms_proc_from_dw_proc(SymsDebugInfoDwarf *debug_info, DwCompileUnit *cu, DwProc *proc, SymsProc *proc_out);

SYMS_INTERNAL SymsStringRef
syms_string_ref_dw_path(SymsString compile_dir, SymsString dir, SymsString file);

#endif /* SYMS_DWARF_API_INCLUDE_H */

