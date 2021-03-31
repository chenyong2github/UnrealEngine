// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_VIRTUAL_UNWIND_INCLUDE_H
#define SYMS_VIRTUAL_UNWIND_INCLUDE_H

/******************************************************************************
 * File   : syms_virtual_unwind.h                                             *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/10                                                        *
 * Purpose: User-level API for stack unwinding                                *
 ******************************************************************************/

typedef struct SymsVirtualUnwind SymsVirtualUnwind;

SYMS_API SymsVirtualUnwind *
syms_virtual_unwind_init(SymsInstance *instance, SymsArena *arena);

SYMS_API SymsErrorCode
syms_virtual_unwind_frame(SymsVirtualUnwind *context, 
    void *memread_ctx, syms_memread_sig *memread_cb, 
    void *regread_context, syms_regread_sig *regread_cb, 
    void *regwrite_context, syms_regwrite_sig *regwrite_cb);

#endif /* SYMS_VIRTUAL_UNWIND_INCLUDE_H */
