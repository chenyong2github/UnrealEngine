// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_NT_UNWIND_INCLUDE_H
#define SYMS_NT_UNWIND_INCLUDE_H

/******************************************************************************
 * File   : syms_nt_unwind.h                                                  *
 * Author : Nikita Smith                                                      *
 * Created: 2020/09/11                                                        *
 * Purpose: Stack unwinder for Win32                                          *
 ******************************************************************************/

SYMS_INTERNAL syms_uint
syms_nt_unwind_info_sizeof(SymsNTUnwindInfo *uwinfo);

SYMS_INTERNAL syms_uint
syms_nt_unwind_code_count_nodes(U8 uwcode_flags);

SYMS_INTERNAL SymsRegID
syms_remap_gpr_nt(SymsImage *img, syms_uint nt_regid);

SYMS_INTERNAL SymsRegID
syms_remap_xmm_nt(SymsImage *img, syms_uint nt_regid);

SYMS_INTERNAL SymsErrorCode
syms_memread_pdata(struct SymsInstance *instance, SymsMemread *memread_info, SymsAddr va, SymsNTPdata *pdata_out);

SYMS_INTERNAL SymsErrorCode
syms_virtual_unwind_nt(struct SymsInstance *instance, SymsRegs *regs, SymsMemread *memread_info);

#endif // SYMS_NT_UNWIND_INCLUDE_H

