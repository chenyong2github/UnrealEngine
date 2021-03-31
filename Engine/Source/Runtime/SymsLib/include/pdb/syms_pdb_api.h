// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_SYMS_API_INCLUDE_H
#define SYMS_SYMS_API_INCLUDE_H

/******************************************************************************
 * File   : syms_pdb.h                                                        *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/20                                                        *
 * Purpose: Declarations for pdb.h interface                                  *
 ******************************************************************************/

typedef struct SymsDebugInfoPdb 
{
  pdb_context context;
} SymsDebugInfoPdb;

typedef struct SymsModIterPdb 
{
  pdb_mod_it impl;
} SymsModIterPdb;

typedef struct SymsLineIterPdb 
{
  pdb_line_it impl;
} SymsLineIterPdb;

typedef struct SymsMemberIterPdb 
{
  pdb_member_it impl;
  pdb_pointer methodlist_name;
  pdb_uint methodlist_index;
  pdb_uint methodlist_count;
  pdb_pointer methodlist_block;
} SymsMemberIterPdb;

typedef struct SymsGlobalIterPdb 
{
  pdb_global_it impl;
} SymsGlobalIterPdb;

typedef struct SymsLocalDataIterPdb 
{
  pdb_sym_it impl;
} SymsLocalDataIterPdb;

typedef struct SymsFileIterPdb 
{
  pdb_file_it impl;
} SymsFileIterPdb;

typedef struct SymsProcIterPdb 
{
  pdb_proc_it impl;
} SymsProcIterPdb;

typedef struct SymsArgIterPdb 
{
  pdb_arg_it impl;
} SymsArgIterPdb;

typedef struct SymsLocalIterPdb 
{
  pdb_local_it impl;
  SymsScope *scope_stack;
  syms_uint scope_count;
  syms_uint scope_max;
} SymsLocalIterPdb;

typedef struct SymsInlineIterPdb 
{
  pdb_inline_it impl;
} SymsInlineIterPdb;

typedef struct SymsConstIterPdb 
{
  pdb_const_it impl;
} SymsConstIterPdb;

typedef struct SymsTypeIterPdb 
{
  pdb_type_it impl;
} SymsTypeIterPdb;

typedef struct SymsRangePdb
{
  SymsAddr lo;
  SymsAddr hi;
  pdb_pointer gaps;
} SymsRangePdb;

typedef struct SymsRangeIterPdb
{
  pdb_context *context;
  pdb_pointer gaps;
  SymsAddr lo;
  SymsAddr hi;
  syms_uint read_offset;
  syms_bool last_range_emitted;
} SymsRangeIterPdb;

SYMS_COMPILER_ASSERT(sizeof(SymsRangePdb) <= sizeof(SymsRangeImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsRangeIterPdb) <= sizeof(SymsRangeIterImpl));
SYMS_COMPILER_ASSERT(sizeof(pdb_type) <= sizeof(SymsTypeImpl));
SYMS_COMPILER_ASSERT(sizeof(pdb_mod) <= sizeof(SymsModImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsDebugInfoPdb) <= sizeof(SymsDebugInfoImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsModIterPdb) <= sizeof(SymsModIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLineIterPdb) <= sizeof(SymsLineIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsMemberIterPdb) <= sizeof(SymsMemberIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsGlobalIterPdb) <= sizeof(SymsGlobalIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLocalDataIterPdb) <= sizeof(SymsLocalDataIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsFileIterPdb) <= sizeof(SymsFileIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsProcIterPdb) <= sizeof(SymsProcIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsArgIterPdb) <= sizeof(SymsArgIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsLocalIterPdb) <= sizeof(SymsLocalIterImpl));
SYMS_COMPILER_ASSERT(sizeof(SymsInlineIterPdb) <= sizeof(SymsInlineIterImpl));
SYMS_COMPILER_ASSERT(SYMS_LOCATION_IMPLICIT_VALUE_MAX >= PDB_LOCATION_IMPLICIT_VALUE_MAX);
SYMS_COMPILER_ASSERT(SYMS_MEMBER_ENUM_MAX >= PDB_NUMERIC_MAX);
SYMS_COMPILER_ASSERT(SYMS_CONST_VALUE_MAX >= PDB_NUMERIC_MAX);
SYMS_COMPILER_ASSERT(sizeof(SymsStringRefImpl) >= sizeof(pdb_pointer));

typedef PDB_MEMREAD_SIG(SymsPdbMemreadSig);
typedef PDB_REGREAD_SIG(SymsPdbRegreadSig);
typedef PDB_REGWRITE_SIG(SymsPdbRegwriteSig);

// ----------------------------------------------------------------------------

SYMS_INTERNAL pdb_cv_itype
syms_typeid_to_pdb(const SymsTypeID *type_id);

SYMS_INTERNAL SymsTypeID
syms_typeid_for_pdb(pdb_cv_itype itype);

SYMS_INTERNAL syms_int
syms_typeid_cmp_pdb(pdb_cv_itype l, pdb_cv_itype r);

// ----------------------------------------------------------------------------

SYMS_INTERNAL SymsStringRef
syms_string_ref_pdb(pdb_pointer *pointer);

SYMS_INTERNAL syms_bool
syms_proc_from_pdb_proc(SymsDebugInfoPdb *debug_info, pdb_proc *proc, SymsProc *proc_out);

SYMS_INTERNAL void
syms_const_convert_from_pdb(pdb_const_value *pdb_const, SymsConst *const_out);

SYMS_INTERNAL syms_bool
syms_pdb_type_to_syms_type(pdb_context *pdb, pdb_type *type, SymsType *type_out);

SYMS_INTERNAL
PDB_REGREAD_SIG(syms_regread_pdb);

#endif /* SYMS_SYMS_API_INCLUDE_H */

