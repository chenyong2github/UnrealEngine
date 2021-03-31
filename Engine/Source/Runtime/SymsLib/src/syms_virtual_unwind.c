// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_virtual_unwind.c                                             *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/10                                                        *
 * Purpose: Provides a single interface for stack unwinding                   *
 ******************************************************************************/

#include "dwarf/syms_dwarf.h"
#include "dwarf/syms_dwarf_unwind.h"

struct SymsVirtualUnwind 
{
  SymsInstance *instance;
  union {
    struct {
      U32 reserved;
    } win64;
    struct {
      DwVirtualUnwindDataType frame_data_type;
      SymsSection frame_data;
      DwVirtualUnwind context;
    } dwarf;
  } u;
};

SYMS_API SymsVirtualUnwind *
syms_virtual_unwind_init(SymsInstance *instance, SymsArena *arena)
{
  syms_bool is_inited = syms_false;
  SymsImage *image = &instance->img;
  SymsArenaFrame frame = syms_arena_frame_begin(arena);
  SymsVirtualUnwind* unwind = syms_arena_push_struct(arena, SymsVirtualUnwind);
  unwind->instance = instance;
  switch (image->type) {
  case SYMS_IMAGE_NULL: break;
  // TODO(nick): ARM, PPC, IA64
  case SYMS_IMAGE_NT:  is_inited = image->arch == SYMS_ARCH_X64 || image->arch == SYMS_ARCH_X86;       break;
  case SYMS_IMAGE_ELF: {
    if (syms_img_sec_from_name(instance, syms_string_init_lit(".eh_frame"), &unwind->u.dwarf.frame_data)) {
      unwind->u.dwarf.frame_data_type = DW_VIRTUAL_UNWIND_DATA_EH_FRAME;
    } else if (syms_img_sec_from_name(instance, syms_string_init_lit(".debug_frame"), &unwind->u.dwarf.frame_data)) {
      unwind->u.dwarf.frame_data_type = DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME;
    } else {
      // NOTE(nick): This might be a probable case where image doesn't have 
      // section with data to unwind frame. Don't unwind in this case.
      unwind->u.dwarf.frame_data_type = DW_VIRTUAL_UNWIND_DATA_NULL;
      syms_memzero(&unwind->u.dwarf.frame_data, sizeof(unwind->u.dwarf.frame_data));
    }
    is_inited = dw_virtual_unwind_init(image->type, &unwind->u.dwarf.context); 
  } break;
  }
  if (!is_inited) {
    syms_arena_frame_end(frame);
    unwind = 0;
  }
  return unwind;
}

SYMS_API SymsErrorCode
syms_virtual_unwind_frame(SymsVirtualUnwind *context, 
    void *memread_ctx, syms_memread_sig *memread_cb, 
    void *regread_context, syms_regread_sig *regread_cb, 
    void *regwrite_context, syms_regwrite_sig *regwrite_cb)
{
  if (!context || !memread_cb || !regread_cb || !regwrite_cb) {
    return SYMS_ERR_INVAL;
  }

  SymsInstance* instance = context->instance;
  SymsImage* img = &instance->img;

  SymsMemread memread_info;
  memread_info.result   = SYMS_ERR_INREAD;
  memread_info.context  = memread_ctx;
  memread_info.callback = memread_cb;

  SymsRegread regread_info;
  regread_info.arch = syms_get_arch(instance);
  regread_info.result = SYMS_ERR_INREAD;
  regread_info.context = regread_context;
  regread_info.callback = regread_cb;

  SymsRegwrite regwrite_info;
  regwrite_info.arch = syms_get_arch(instance);
  regwrite_info.result = SYMS_ERR_INVAL;
  regwrite_info.context = regwrite_context;
  regwrite_info.callback = regwrite_cb;

  SymsErrorCode result = SYMS_ERR_INVALID_CODE_PATH;

  switch (img->type) {
  case SYMS_IMAGE_NULL: break;

  case SYMS_IMAGE_NT: result = syms_virtual_unwind_nt(instance, &memread_info, regread_context, regread_cb, regwrite_context, regwrite_cb); break;

  case SYMS_IMAGE_ELF: {
    if (dw_virtual_unwind_frame(&context->u.dwarf.context,
                  img->arch,
                  context->u.dwarf.frame_data_type,
                  context->u.dwarf.frame_data.data, context->u.dwarf.frame_data.data_size,
                  syms_get_rebase(instance),
                  context->u.dwarf.frame_data.va,
                  &memread_info, syms_memread_dwarf,
                  &regread_info, syms_regread_dwarf,
                  &regwrite_info, syms_regwrite_dwarf)) {
      result = SYMS_ERR_OK;
      break;
    }

    if (memread_info.result == SYMS_ERR_MAYBE) {
      result = SYMS_ERR_MAYBE;
      break;
    }
  } break;
  }

  return result;
}
