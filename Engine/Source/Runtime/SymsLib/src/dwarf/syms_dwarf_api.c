// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_dwarf.c                                                      *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/20                                                        *
 * Purpose: API wrappers and type conversion for dwarf.h                      *
 ******************************************************************************/

SYMS_INTERNAL const DwTag *
syms_typeid_to_dw(const SymsTypeID *type_id)
{
  SYMS_ASSERT(type_id->kind == SYMS_TYPE_ID_DW);
  return (const DwTag *)type_id->impl;
}

SYMS_INTERNAL SymsTypeID
syms_typeid_for_dw(DwTag tag)
{
  SymsTypeID type_id;
  type_id.kind = SYMS_TYPE_ID_DW;
  *(DwTag *)type_id.impl = tag;
  return type_id;
}

SYMS_INTERNAL syms_int
syms_typeid_cmp_dw(DwTag a, DwTag b)
{
  syms_int r = 0;
  if (a.cu <= b.cu) {
    if (a.info < b.info) {
      r = -1;
    } else if (a.info > b.info) {
      r = +1;
    }
  } else if (a.cu > b.cu) {
    r = +1;
  }
  return r;
}

// ----------------------------------------------------------------------------

SYMS_INTERNAL syms_bool
syms_mod_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsModIterDwarf *iter)
{
  if (dw_cu_iter_init(&iter->impl, &debug_info->context)) {
    return syms_true;
  } else {
    syms_memzero(&iter, sizeof(iter));
    return syms_false;
  }
}

SYMS_INTERNAL syms_bool
syms_mod_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsModIterDwarf *iter, SymsMod *mod_out)
{ 
  DwCompileUnit *cu = (DwCompileUnit *)mod_out->impl;
  syms_bool result = syms_false;

  (void)debug_info;

  if (dw_cu_iter_next(&iter->impl, cu)) {
    mod_out->id = (SymsModID)cu->info_base;
    mod_out->name = syms_string_ref_str(cu->name);
    mod_out->va = cu->rva;
    mod_out->size = cu->len;
    if (cu->len == 0) {
      DwRangeIter range_iter;
      if (dw_range_iter_init(&range_iter, cu, cu->range_off)) {
        SymsAddr lo, hi;
        while (dw_range_iter_next(&range_iter, &lo, &hi)) {
          SYMS_ASSERT(lo <= hi);
          mod_out->size += (hi - lo);
        }
      }
    }
    result = syms_true;
  }
  return result;
}

// ----------------------------------------------------------------------------

SYMS_INTERNAL SymsLineIterDwarf *
syms_line_iter_to_dw(SymsLineIter *iter)
{
  SymsLineIterDwarf *impl = (SymsLineIterDwarf *)iter->impl;
  return impl;
}

SYMS_INTERNAL syms_bool
syms_line_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsLineIterDwarf *iter, DwCompileUnit *cu, syms_bool *has_line_count)
{ (void)debug_info;
  *has_line_count = syms_false;
  iter->prev_file_index = 0;
  iter->cu = cu;
  return dw_line_iter_init(&iter->impl, cu);
}

SYMS_INTERNAL syms_bool
syms_line_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsLineIterDwarf *iter, SymsLine *line_out, syms_bool *switched_file, SymsSourceFile *file_out)
{
  syms_bool keep_fetching = syms_true;
  syms_bool result = syms_false;
(void)debug_info;

  for (; keep_fetching ;) {
    DwLineIter  *impl = &iter->impl;
    DwLineIterOp dw_op;

    keep_fetching = syms_false;

    if (dw_line_iter_next(impl, &dw_op)) {
      switch (dw_op.type) {
      case DW_LINE_ITER_OP_NULL: break;
      case DW_LINE_ITER_OP_LINE: {
        dw_uint ln;
        u16 col;

        if (dw_op.u.line.file_index != iter->prev_file_index) {
          SymsString dir;
          DwLineFile file; 
          dir = syms_string_init(0,0);
          file.file_name = syms_string_init(0,0);
          if (dw_line_iter_get_file(impl, dw_op.u.line.file_index, &file)) {
            if (dw_line_iter_get_dir(impl, file.dir_index, &dir)) {
              file_out->name = syms_string_ref_dw_path(iter->cu->compile_dir, dir, file.file_name);
              file_out->chksum_type = SYMS_CHECKSUM_NULL; // DWARF doesn't export checksums
            } else {
              SYMS_ASSERT_FAILURE("cannot extract file name for line");
            }
            iter->prev_file_index = dw_op.u.line.file_index;
          } else {
            SYMS_ASSERT_FAILURE("cannot extract directory for line");
          }

          *switched_file = syms_true;
        }
        ln = dw_op.u.line.line;
        col = syms_trunc_u16(syms_trunc_u32(dw_op.u.line.column));
        syms_line_init(line_out, (SymsAddr)dw_op.u.line.address, ln, col);
      } break;
      case DW_LINE_ITER_OP_DEFINE_FILE: {
        SYMS_ASSERT_FAILURE("define-file is not implemented");
        keep_fetching = syms_true;
      } break;
      }
      result = syms_true;
    }
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_member_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsMemberIterDwarf *iter, SymsType *type)
{
  const DwTag *type_tag = syms_typeid_to_dw(&type->id);
  return dw_member_iter_init(&iter->impl, &debug_info->context, *type_tag);
}

SYMS_INTERNAL syms_bool
syms_member_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsMemberIterDwarf *iter, SymsMember *member_out)
{
  syms_bool result = syms_false;
  DwMember member;
(void)debug_info;
  if (dw_member_iter_next(&iter->impl, &member)) {
    member_out->type_id  = syms_typeid_for_dw(member.type_tag);
    member_out->name_ref = syms_string_ref_str(member.name);
    member_out->u.data_offset = member.byte_off;
    result = syms_true;
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_global_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsGlobalIterDwarf *iter)
{ (void)debug_info; (void)iter;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_global_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsGlobalIterDwarf *iter, SymsGlobal *gdata_out)
{ (void)debug_info; (void)iter; (void)gdata_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_local_data_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsLocalDataIterDwarf *iter, SymsMod *mod)
{ (void)debug_info; (void)iter; (void)mod;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_local_data_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsLocalDataIterDwarf *iter, SymsLocalData *ldata_out)
{ (void)debug_info; (void)iter; (void)ldata_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
SymsFile_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsFileIterDwarf *iter)
{
  return dw_file_iter_init(&iter->impl, &debug_info->context);
}

SYMS_INTERNAL syms_bool
SymsFile_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsFileIterDwarf *iter, SymsStringRef *ref_out)
{ (void)debug_info; (void)iter; (void)ref_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_proc_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsProcIterDwarf *iter, SymsMod *mod)
{ (void)debug_info;
  return dw_proc_iter_init(&iter->impl, (DwCompileUnit *)mod->impl);
}

SYMS_INTERNAL syms_bool
syms_proc_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsProcIterDwarf *iter, SymsProc *proc_out)
{
  syms_bool result = syms_false;
  DwProc proc;
  if (dw_proc_iter_next(&iter->impl, &proc)) {
    result = syms_proc_from_dw_proc(debug_info, iter->impl.attribs.cu, &proc, proc_out);
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_arg_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsArgIterDwarf *iter, SymsTypeID *id)
{ (void)debug_info; (void)iter; (void)id;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_arg_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsArgIterDwarf *iter, SymsTypeID *arg_out)
{ (void)debug_info; (void)iter; (void)arg_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_local_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsLocalIterDwarf *iter, SymsProc *proc)
{
  SymsProcData *impl = (SymsProcData *)proc->impl; 
  return dw_local_iter_init(&iter->impl, &debug_info->context, impl->dw.type_tag);
}

SYMS_INTERNAL syms_bool
syms_local_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsLocalIterDwarf *iter, SymsVar *lvar_out)
{
  syms_bool result = syms_false;
  DwLocal lvar;
(void)debug_info;
  if (dw_local_iter_next(&iter->impl, &lvar)) {
    lvar_out->type_id = syms_typeid_for_dw(lvar.type_tag);
    lvar_out->flags = 0;
    if (lvar.flags & DW_VAR_ARGUMENT) {
      lvar_out->flags |= SYMS_VAR_FLAG_PARAM;
    }
    lvar_out->name_ref = syms_string_ref_str(lvar.name);
    *(DwEncodedLocation *)lvar_out->encoded_va.impl = lvar.encoded_va;
    result = syms_true;
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_inline_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsInlineIterDwarf *iter, DwCompileUnit *cu, DwTag inlined_proc_tag, SymsAddr rva)
{ 
  SymsAddr info_off;
  syms_bool is_inited;
(void)debug_info;
  iter->rva = rva;
  iter->cu = cu;
  info_off = DW_PTR_DIFF_BYTES(inlined_proc_tag.info, cu->info_data_start);
  is_inited = dw_attrib_iter_init(&iter->attribs, cu, info_off);
  is_inited = is_inited && iter->attribs.has_children;
  return is_inited;
}

SYMS_INTERNAL syms_bool
syms_inline_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsInlineIterDwarf *iter, SymsInlineSite *site_out)
{
  syms_bool is_next_valid = syms_false;
  DwCompileUnit *cu = iter->cu;
(void)debug_info;

  do {
    if (iter->attribs.has_children) {
      iter->depth += 1;
    }
    if (iter->attribs.tag_type == DW_TAG_INLINED_SUBROUTINE) {
      DwProc dw_proc;
      if (dw_proc_init(&iter->attribs, &dw_proc)) {
        SymsAddr site_lo = 0;
        SymsAddr site_hi = 0;
        if (!dw_range_check(iter->cu, dw_proc.range_off, iter->rva, &site_lo, &site_hi)) {
          DwLocation loc;
          if (dw_decode_location_rva(&dw_proc.encoded_va.u.rva, 0, &loc)) {
            if (loc.type == DW_LOCATION_ADDR) {
              site_lo = loc.u.addr;
              site_hi = site_lo + dw_proc.len;
            } else {
#if defined(SYMS_PARANOID)
              SYMS_ASSERT_FAILURE("expected an address for inline site");
#endif
            }
          }
        }

        is_next_valid = iter->rva >= site_lo && iter->rva < site_hi;
        if (is_next_valid) {
          DwLineIter line_iter;

          site_out->name = syms_string_ref_str(dw_proc.name);
          site_out->type_id = syms_typeid_for_dw(dw_proc.type_tag);

          site_out->call_file.name = syms_string_ref_str(syms_string_init(0,0));
          site_out->call_file.chksum_type = SYMS_CHECKSUM_NULL;

          site_out->decl_file.name = syms_string_ref_str(syms_string_init(0,0));
          site_out->decl_file.chksum_type = SYMS_CHECKSUM_NULL;

          site_out->call_ln = dw_proc.call_ln;
          site_out->decl_ln = dw_proc.decl_ln;

          site_out->range_lo = site_lo;
          site_out->range_hi = site_hi;

          site_out->sort_index = (cu->info_base + cu->info_len) - iter->attribs.info_off;

          if (dw_line_iter_init(&line_iter, iter->cu)) {
            DwLineFile file;

            // resolve call file and line number
            if (dw_line_iter_get_file(&line_iter, dw_proc.call_file, &file)) {
              SymsString dir;
              if (dw_line_iter_get_dir(&line_iter, file.dir_index, &dir)) {
                site_out->call_file.name = syms_string_ref_dw_path(cu->compile_dir, dir, file.file_name);
              } else {
                site_out->call_file.name = syms_string_ref_str(file.file_name);
              }
            }

            // resolve decl file and line number
            if (dw_line_iter_get_file(&line_iter, dw_proc.decl_file, &file)) {
              SymsString dir;
              if (dw_line_iter_get_dir(&line_iter, file.dir_index, &dir)) {
                site_out->decl_file.name = syms_string_ref_dw_path(cu->compile_dir, dir, file.file_name);
              } else {
                site_out->decl_file.name = syms_string_ref_str(file.file_name);
              }
            }
          }
          break;
        }
      }
    } else if (iter->attribs.tag_type == DW_TAG_NULL) {
      if (iter->depth == 0) {
        break;
      }
      iter->depth -= 1;
    }
  } while (dw_attrib_iter_next_tag(&iter->attribs));

  dw_attrib_iter_next_tag(&iter->attribs);

  return is_next_valid;
}

SYMS_INTERNAL syms_bool
syms_const_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsConstIterDwarf *iter)
{ (void)debug_info; (void)iter;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_const_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsConstIterDwarf *iter, SymsConst *const_out)
{ (void)debug_info; (void)iter; (void)const_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_type_iter_init_dwarf(SymsDebugInfoDwarf *debug_info, SymsTypeIterDwarf *iter)
{ (void)debug_info; (void)iter;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_type_iter_next_dwarf(SymsDebugInfoDwarf *debug_info, SymsTypeIterDwarf *iter, SymsTypeID *typeid_out)
{ (void)debug_info; (void)iter; (void)typeid_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_type_from_name_dwarf(SymsDebugInfoDwarf *debug_info, SymsString name, SymsType *type_out)
{
  syms_bool result = syms_false;
  DwTag type_tag;
  if (dw_type_from_name(&debug_info->context, name.data, name.len, 1, &type_tag)) {
    DwType type;
    if (dw_infer_type(&debug_info->context, type_tag, &type)) {
      result = syms_dw_type_to_syms_type(&type, type_out);
    }
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_global_from_name_dwarf(SymsDebugInfoDwarf *debug_info, SymsString name, SymsGlobal *gvar_out)
{
  syms_bool result = syms_false;
  DwVar gvar;
  if (dw_global_from_name(&debug_info->context, name.data, name.len, &gvar)) {
    gvar_out->type_id = syms_typeid_for_dw(gvar.type_tag);
    *(DwEncodedLocation *)gvar_out->encoded_va.impl = gvar.encoded_va;
    result = syms_true;
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_const_from_name_dwarf(SymsDebugInfoDwarf *debug_info, SymsString name, SymsConst *const_out)
{ (void)debug_info; (void)name; (void)const_out;
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_infer_type_dwarf(SymsDebugInfoDwarf *debug_info, SymsTypeID type_id, SymsType *type_out)
{
  DwType type;
  const DwTag *type_tag = syms_typeid_to_dw(&type_id);
  syms_bool result = syms_false;
  if (dw_infer_type(&debug_info->context, *type_tag, &type)) {
    result = syms_dw_type_to_syms_type(&type, type_out);
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_trampoline_from_ip_dwarf(SymsDebugInfoDwarf *debug_info, SymsAddr ip, SymsAddr *ip_out)
{ (void)debug_info; (void)ip; (void)ip_out;
  return syms_false;
}

SYMS_INTERNAL SymsErrorCode
syms_decode_location_dwarf(
               SymsAddr orig_rebase,
               SymsAddr rebase,
               DwEncodedLocation *impl, 
               SymsRegread *regread,
               SymsMemread *memread_context,
               SymsLocation *loc_out)
{
  SymsErrorCode result = SYMS_ERR_INVALID_CODE_PATH;
  DwLocation loc;
  if (dw_decode_location(impl, rebase, memread_context, syms_memread_dwarf, regread, syms_regread_dwarf, &loc)) {
    if (SYMS_RESULT_OK(memread_context->result)) {
      switch (loc.type) {
      case DW_LOCATION_ADDR: {
        SYMS_ASSERT(loc.u.addr >= orig_rebase);
        loc_out->kind = SYMS_LOCATION_VA;
        loc_out->u.va = (loc.u.addr - orig_rebase) + rebase;
        result = SYMS_ERR_OK;
      } break;
      case DW_LOCATION_IMPLICIT: {
        loc_out->kind = SYMS_LOCATION_INDIRECT;
        loc_out->u.indirect.len = loc.u.implicit.len;
        loc_out->u.indirect.data = loc.u.implicit.data;
        result = SYMS_ERR_OK;
      } break;
      case DW_LOCATION_NULL: {
        loc_out->kind = SYMS_LOCATION_NULL;
        result = SYMS_ERR_OK;
      } break;
      }
    } else {
      result = memread_context->result;
    }
  }
  return result;
}

SYMS_INTERNAL syms_bool
syms_proc_from_dw_proc(SymsDebugInfoDwarf *debug_info, DwCompileUnit *cu, DwProc *proc, SymsProc *proc_out)
{
  DwLocation location;
(void)debug_info;
  if (proc_out && proc) {
    SymsAddr range_lo, range_hi;

    proc_out->type_id               = syms_typeid_for_dw(proc->type_tag);
    proc_out->len                   = proc->len;
    proc_out->name_ref              = syms_string_ref_str(proc->name);
    {
      SymsProcData *impl = (SymsProcData *)proc_out->impl;
      impl->dw.type_tag      = proc->type_tag;
      impl->dw.frame_base    = proc->frame_base;
    }

    {
      SymsRange *range = &proc_out->range;
      SymsRangeDwarf *impl = (SymsRangeDwarf *)range->u.impl;
      range->type = SYMS_RANGE_IMPL;
      impl->cu_index = cu->index;
      impl->range_off = proc->range_off;
    }

    if (dw_get_range_bounds(cu, proc->range_off, &range_lo, &range_hi)) {
      proc_out->va = range_lo;
      proc_out->len = syms_trunc_u32(range_hi - range_lo);
    }

    if (dw_decode_location(&proc->encoded_va, 0, NULL, NULL, NULL, NULL, &location)) {
      if (location.type == DW_LOCATION_ADDR) {
        proc_out->va            = location.u.addr;
        proc_out->dbg_start_va  = proc_out->va;
        proc_out->dbg_end_va    = proc_out->va + proc_out->len;
      } else {
        proc_out->va            = 0;
        proc_out->dbg_start_va  = 0;
        proc_out->dbg_end_va    = 0;
      }
    }
  }
  return syms_true;
}

SYMS_INTERNAL syms_bool
syms_dw_type_to_syms_type(DwType *dw_type, SymsType *syms_type)
{
  syms_type->id = syms_typeid_for_dw(dw_type->type_tag);
  syms_type->next_id = syms_typeid_for_dw(dw_type->next_type_tag);
  syms_type->is_fwdref = syms_false;

  syms_type->modifier = 0;
  if (dw_type->modifier & DW_TYPE_MDFR_ATOMIC) {
    syms_type->modifier |= SYMS_TYPE_MDFR_ATOMIC;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_CONST) {
    syms_type->modifier |= SYMS_TYPE_MDFR_CONST;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_IMMUTABLE) {
    syms_type->modifier |= SYMS_TYPE_MDFR_IMMUTABLE;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_PACKED) {
    syms_type->modifier |= SYMS_TYPE_MDFR_PACKED;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_REF) {
    syms_type->modifier |= SYMS_TYPE_MDFR_REF;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_RESTRICT) {
    syms_type->modifier |= SYMS_TYPE_MDFR_RESTRICT;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_RVALUE_REF) {
    syms_type->modifier |= SYMS_TYPE_MDFR_REF;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_SHARED) {
    syms_type->modifier |= SYMS_TYPE_MDFR_SHARED;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_VOLATILE) {
    syms_type->modifier |= SYMS_TYPE_MDFR_VOLATILE;
  }
  if (dw_type->modifier & DW_TYPE_MDFR_CHAR) {
    syms_type->modifier |= SYMS_TYPE_MDFR_CHAR;
  }
  
  switch (dw_type->kind) {
  case DW_TYPE_NULL:      syms_type->kind = SYMS_TYPE_NULL; break;

  case DW_TYPE_FLOAT16:   syms_type->kind = SYMS_TYPE_FLOAT16;    break;
  case DW_TYPE_FLOAT32:   syms_type->kind = SYMS_TYPE_FLOAT32;    break;
  case DW_TYPE_FLOAT48:   syms_type->kind = SYMS_TYPE_FLOAT48;    break;
  case DW_TYPE_FLOAT64:   syms_type->kind = SYMS_TYPE_FLOAT64;    break;
  case DW_TYPE_FLOAT80:   syms_type->kind = SYMS_TYPE_FLOAT80;    break;
  case DW_TYPE_FLOAT128:  syms_type->kind = SYMS_TYPE_FLOAT128;   break;

  case DW_TYPE_INT8:      syms_type->kind = SYMS_TYPE_INT8;   break;
  case DW_TYPE_INT16:     syms_type->kind = SYMS_TYPE_INT16;  break;
  case DW_TYPE_INT32:     syms_type->kind = SYMS_TYPE_INT32;  break;
  case DW_TYPE_INT64:     syms_type->kind = SYMS_TYPE_INT64;  break;
  case DW_TYPE_INT128:    syms_type->kind = SYMS_TYPE_INT128; break;

  case DW_TYPE_UINT8:      syms_type->kind = SYMS_TYPE_UINT8;   break;
  case DW_TYPE_UINT16:     syms_type->kind = SYMS_TYPE_UINT16;  break;
  case DW_TYPE_UINT32:     syms_type->kind = SYMS_TYPE_UINT32;  break;
  case DW_TYPE_UINT64:     syms_type->kind = SYMS_TYPE_UINT64;  break;
  case DW_TYPE_UINT128:    syms_type->kind = SYMS_TYPE_UINT128; break;

  case DW_TYPE_STRUCT:     syms_type->kind = SYMS_TYPE_STRUCT;     break;
  case DW_TYPE_UNION:      syms_type->kind = SYMS_TYPE_UNION;      break;
  case DW_TYPE_CLASS:      syms_type->kind = SYMS_TYPE_CLASS;      break;
  case DW_TYPE_TYPEDEF:    syms_type->kind = SYMS_TYPE_TYPEDEF;    break;
  case DW_TYPE_ENUM:       syms_type->kind = SYMS_TYPE_ENUM;       break;
  case DW_TYPE_PROC:       syms_type->kind = SYMS_TYPE_PROC;       break;
  case DW_TYPE_PROC_PARAM: syms_type->kind = SYMS_TYPE_PROC_PARAM; break;
  case DW_TYPE_VOID:       syms_type->kind = SYMS_TYPE_VOID;       break;
  case DW_TYPE_BOOL:       syms_type->kind = SYMS_TYPE_BOOL;       break;
  case DW_TYPE_PTR:        syms_type->kind = SYMS_TYPE_PTR;        break;
  case DW_TYPE_ARR:        syms_type->kind = SYMS_TYPE_ARR;        break;

#if 0
  case DW_TYPE_CHAR:       syms_type->kind = SYMS_TYPE_CHAR;       break;
  case DW_TYPE_METHOD:     syms_type->kind = SYMS_TYPE_METHOD;     break;
  case DW_TYPE_COMPLEX32:  syms_type->kind = SYMS_TYPE_COMPLEX32;  break;
  case DW_TYPE_COMPLEX64:  syms_type->kind = SYMS_TYPE_COMPLEX64;  break;
  case DW_TYPE_COMPLEX80:  syms_type->kind = SYMS_TYPE_COMPLEX80;  break;
  case DW_TYPE_COMPLEX128: syms_type->kind = SYMS_TYPE_COMPLEX128; break;
#endif
  }
  
  if (dw_type->kind == DW_TYPE_ARR) {
    syms_type->size = (syms_uint)dw_type->u.arr_count;
  } else {
    syms_type->size = (syms_uint)dw_type->size;
  }
  syms_type->name_ref = syms_string_ref_str(dw_type->name);

  return syms_true;
}

SYMS_INTERNAL SymsStringRef
syms_string_ref_dw_path(SymsString compile_dir, SymsString dir, SymsString file)
{
  SymsStringRef ref;
  DwFilePath *path;
  ref.type = SYMS_STRING_REF_TYPE_DW_PATH;
  path = (DwFilePath *)ref.impl;
  path->compile_dir = compile_dir;
  path->dir = dir;
  path->file = file;
  return ref;
}

SYMS_INTERNAL syms_bool
syms_dw_regid_to_regid(SymsArch arch, dw_uint regid, SymsRegID *regid_out)
{
  syms_bool is_mapped = syms_true;

  switch (arch) {
  case SYMS_ARCH_X64: {
    switch (regid) {
    case DW_REG_X64_RAX:        *regid_out = SYMS_REG_X64_rax;     break;
    case DW_REG_X64_RDX:        *regid_out = SYMS_REG_X64_rdx;     break;
    case DW_REG_X64_RCX:        *regid_out = SYMS_REG_X64_rcx;     break;
    case DW_REG_X64_RBX:        *regid_out = SYMS_REG_X64_rbx;     break;
    case DW_REG_X64_RSI:        *regid_out = SYMS_REG_X64_rsi;     break;
    case DW_REG_X64_RDI:        *regid_out = SYMS_REG_X64_rdi;     break;
    case DW_REG_X64_RBP:        *regid_out = SYMS_REG_X64_rbp;     break;
    case DW_REG_X64_RSP:        *regid_out = SYMS_REG_X64_rsp;     break;
    case DW_REG_X64_R8:         *regid_out = SYMS_REG_X64_r8;      break;
    case DW_REG_X64_R9:         *regid_out = SYMS_REG_X64_r9;      break;
    case DW_REG_X64_R10:        *regid_out = SYMS_REG_X64_r10;     break;
    case DW_REG_X64_R11:        *regid_out = SYMS_REG_X64_r11;     break;
    case DW_REG_X64_R12:        *regid_out = SYMS_REG_X64_r12;     break;
    case DW_REG_X64_R13:        *regid_out = SYMS_REG_X64_r13;     break;
    case DW_REG_X64_R14:        *regid_out = SYMS_REG_X64_r14;     break;
    case DW_REG_X64_R15:        *regid_out = SYMS_REG_X64_r15;     break;
    case DW_REG_X64_RIP:        *regid_out = SYMS_REG_X64_rip;     break;
    case DW_REG_X64_XMM0:       *regid_out = SYMS_REG_X64_xmm0;    break;
    case DW_REG_X64_XMM1:       *regid_out = SYMS_REG_X64_xmm1;    break;
    case DW_REG_X64_XMM2:       *regid_out = SYMS_REG_X64_xmm2;    break;
    case DW_REG_X64_XMM3:       *regid_out = SYMS_REG_X64_xmm3;    break;
    case DW_REG_X64_XMM4:       *regid_out = SYMS_REG_X64_xmm4;    break;
    case DW_REG_X64_XMM5:       *regid_out = SYMS_REG_X64_xmm5;    break;
    case DW_REG_X64_XMM6:       *regid_out = SYMS_REG_X64_xmm6;    break;
    case DW_REG_X64_XMM7:       *regid_out = SYMS_REG_X64_xmm7;    break;
    case DW_REG_X64_XMM8:       *regid_out = SYMS_REG_X64_xmm8;    break;
    case DW_REG_X64_XMM9:       *regid_out = SYMS_REG_X64_xmm9;    break;
    case DW_REG_X64_XMM10:      *regid_out = SYMS_REG_X64_xmm10;   break;
    case DW_REG_X64_XMM11:      *regid_out = SYMS_REG_X64_xmm11;   break;
    case DW_REG_X64_XMM12:      *regid_out = SYMS_REG_X64_xmm12;   break;
    case DW_REG_X64_XMM13:      *regid_out = SYMS_REG_X64_xmm13;   break;
    case DW_REG_X64_XMM14:      *regid_out = SYMS_REG_X64_xmm14;   break;
    case DW_REG_X64_XMM15:      *regid_out = SYMS_REG_X64_xmm15;   break;
    case DW_REG_X64_ST0:        *regid_out = SYMS_REG_X64_st0;     break;
    case DW_REG_X64_ST1:        *regid_out = SYMS_REG_X64_st1;     break;
    case DW_REG_X64_ST2:        *regid_out = SYMS_REG_X64_st2;     break;
    case DW_REG_X64_ST3:        *regid_out = SYMS_REG_X64_st3;     break;
    case DW_REG_X64_ST4:        *regid_out = SYMS_REG_X64_st4;     break;
    case DW_REG_X64_ST5:        *regid_out = SYMS_REG_X64_st5;     break;
    case DW_REG_X64_ST6:        *regid_out = SYMS_REG_X64_st6;     break;
    case DW_REG_X64_ST7:        *regid_out = SYMS_REG_X64_st7;     break;
    case DW_REG_X64_MM0:        *regid_out = SYMS_REG_X64_mm0;     break;
    case DW_REG_X64_MM1:        *regid_out = SYMS_REG_X64_mm1;     break;
    case DW_REG_X64_MM2:        *regid_out = SYMS_REG_X64_mm2;     break;
    case DW_REG_X64_MM3:        *regid_out = SYMS_REG_X64_mm3;     break;
    case DW_REG_X64_MM4:        *regid_out = SYMS_REG_X64_mm4;     break;
    case DW_REG_X64_MM5:        *regid_out = SYMS_REG_X64_mm5;     break;
    case DW_REG_X64_MM6:        *regid_out = SYMS_REG_X64_mm6;     break;
    case DW_REG_X64_MM7:        *regid_out = SYMS_REG_X64_mm7;     break;
    case DW_REG_X64_RFLAGS:     *regid_out = SYMS_REG_X64_rflags;  break;
    case DW_REG_X64_ES:         *regid_out = SYMS_REG_X64_es;      break;
    case DW_REG_X64_CS:         *regid_out = SYMS_REG_X64_cs;      break;
    case DW_REG_X64_SS:         *regid_out = SYMS_REG_X64_ss;      break;
    case DW_REG_X64_DS:         *regid_out = SYMS_REG_X64_ds;      break;
    case DW_REG_X64_FS:         *regid_out = SYMS_REG_X64_fs;      break;
    case DW_REG_X64_GS:         *regid_out = SYMS_REG_X64_gs;      break;
    case DW_REG_X64_FS_BASE:    *regid_out = SYMS_REG_X64_fsbase;  break;
    case DW_REG_X64_GS_BASE:    *regid_out = SYMS_REG_X64_gsbase;  break;

    case DW_REG_X64_TR:
    case DW_REG_X64_LDTR:
    default: is_mapped = syms_false; break;
    }
  } break;

  case SYMS_ARCH_X86: {
    switch (regid) {
    case DW_REG_X86_EAX:     *regid_out = SYMS_REG_X86_eax;    break;
    case DW_REG_X86_ECX:     *regid_out = SYMS_REG_X86_ecx;    break;
    case DW_REG_X86_EDX:     *regid_out = SYMS_REG_X86_edx;    break;
    case DW_REG_X86_EBX:     *regid_out = SYMS_REG_X86_ebx;    break;
    case DW_REG_X86_ESP:     *regid_out = SYMS_REG_X86_esp;    break;
    case DW_REG_X86_EBP:     *regid_out = SYMS_REG_X86_ebp;    break;
    case DW_REG_X86_ESI:     *regid_out = SYMS_REG_X86_esi;    break;
    case DW_REG_X86_EDI:     *regid_out = SYMS_REG_X86_edi;    break;
    case DW_REG_X86_EIP:     *regid_out = SYMS_REG_X86_eip;    break;
    case DW_REG_X86_EFLAGS:  *regid_out = SYMS_REG_X86_eflags; break;
    case DW_REG_X86_ST0:     *regid_out = SYMS_REG_X86_st0;    break;
    case DW_REG_X86_ST1:     *regid_out = SYMS_REG_X86_st1;    break;
    case DW_REG_X86_ST2:     *regid_out = SYMS_REG_X86_st2;    break;
    case DW_REG_X86_ST3:     *regid_out = SYMS_REG_X86_st3;    break;
    case DW_REG_X86_ST4:     *regid_out = SYMS_REG_X86_st4;    break;
    case DW_REG_X86_ST5:     *regid_out = SYMS_REG_X86_st5;    break;
    case DW_REG_X86_ST6:     *regid_out = SYMS_REG_X86_st6;    break;
    case DW_REG_X86_ST7:     *regid_out = SYMS_REG_X86_st7;    break;
    case DW_REG_X86_XMM0:    *regid_out = SYMS_REG_X86_xmm0;   break;
    case DW_REG_X86_XMM1:    *regid_out = SYMS_REG_X86_xmm1;   break;
    case DW_REG_X86_XMM2:    *regid_out = SYMS_REG_X86_xmm2;   break;
    case DW_REG_X86_XMM3:    *regid_out = SYMS_REG_X86_xmm3;   break;
    case DW_REG_X86_XMM4:    *regid_out = SYMS_REG_X86_xmm4;   break;
    case DW_REG_X86_XMM5:    *regid_out = SYMS_REG_X86_xmm5;   break;
    case DW_REG_X86_XMM6:    *regid_out = SYMS_REG_X86_xmm6;   break;
    case DW_REG_X86_XMM7:    *regid_out = SYMS_REG_X86_xmm7;   break;
    case DW_REG_X86_MM0:     *regid_out = SYMS_REG_X86_mm0;    break;
    case DW_REG_X86_MM1:     *regid_out = SYMS_REG_X86_mm1;    break;
    case DW_REG_X86_MM2:     *regid_out = SYMS_REG_X86_mm2;    break;
    case DW_REG_X86_MM3:     *regid_out = SYMS_REG_X86_mm3;    break;
    case DW_REG_X86_MM4:     *regid_out = SYMS_REG_X86_mm4;    break;
    case DW_REG_X86_MM5:     *regid_out = SYMS_REG_X86_mm5;    break;
    case DW_REG_X86_MM6:     *regid_out = SYMS_REG_X86_mm6;    break;
    case DW_REG_X86_MM7:     *regid_out = SYMS_REG_X86_mm7;    break;
    case DW_REG_X86_FCW:     *regid_out = SYMS_REG_X86_fcw;    break;
    case DW_REG_X86_FSW:     *regid_out = SYMS_REG_X86_fsw;    break;
    case DW_REG_X86_MXCSR:   *regid_out = SYMS_REG_X86_mxcsr;  break;
    case DW_REG_X86_ES:      *regid_out = SYMS_REG_X86_es;     break;
    case DW_REG_X86_CS:      *regid_out = SYMS_REG_X86_cs;     break;
    case DW_REG_X86_SS:      *regid_out = SYMS_REG_X86_ss;     break;
    case DW_REG_X86_DS:      *regid_out = SYMS_REG_X86_ds;     break;
    case DW_REG_X86_FS:      *regid_out = SYMS_REG_X86_fs;     break;
    case DW_REG_X86_GS:      *regid_out = SYMS_REG_X86_gs;     break;

    case DW_REG_X86_TRAPNO:
    case DW_REG_X86_TR:
    case DW_REG_X86_LDTR:
    default: {
      is_mapped = syms_false;
    } break;
    }
  } break;

  case SYMS_ARCH_ARM: {
    // TODO(nick): I haven't thought too hard about the ARM support,
    // so this is just a stub code.
#if 0
    switch (regid) {
    case DW_REG_ARM_S0:       *regid_out = SYMS_REG_ARM_s0;       break;
    case DW_REG_ARM_S1:       *regid_out = SYMS_REG_ARM_s1;       break;
    case DW_REG_ARM_S2:       *regid_out = SYMS_REG_ARM_s2;       break;
    case DW_REG_ARM_S3:       *regid_out = SYMS_REG_ARM_s3;       break;
    case DW_REG_ARM_S4:       *regid_out = SYMS_REG_ARM_s4;       break;
    case DW_REG_ARM_S5:       *regid_out = SYMS_REG_ARM_s5;       break;
    case DW_REG_ARM_S6:       *regid_out = SYMS_REG_ARM_s6;       break;
    case DW_REG_ARM_S7:       *regid_out = SYMS_REG_ARM_s7;       break;
    case DW_REG_ARM_S8:       *regid_out = SYMS_REG_ARM_s8;       break;
    case DW_REG_ARM_S9:       *regid_out = SYMS_REG_ARM_s9;       break;
    case DW_REG_ARM_S10:      *regid_out = SYMS_REG_ARM_s10;      break;
    case DW_REG_ARM_S11:      *regid_out = SYMS_REG_ARM_s11;      break;
    case DW_REG_ARM_S12:      *regid_out = SYMS_REG_ARM_s12;      break;
    case DW_REG_ARM_S13:      *regid_out = SYMS_REG_ARM_s13;      break;
    case DW_REG_ARM_S14:      *regid_out = SYMS_REG_ARM_s14;      break;
    case DW_REG_ARM_S15:      *regid_out = SYMS_REG_ARM_s15;      break;
    case DW_REG_ARM_S16:      *regid_out = SYMS_REG_ARM_s16;      break;
    case DW_REG_ARM_S17:      *regid_out = SYMS_REG_ARM_s17;      break;
    case DW_REG_ARM_S18:      *regid_out = SYMS_REG_ARM_s18;      break;
    case DW_REG_ARM_S19:      *regid_out = SYMS_REG_ARM_s19;      break;
    case DW_REG_ARM_S20:      *regid_out = SYMS_REG_ARM_s20;      break;
    case DW_REG_ARM_S21:      *regid_out = SYMS_REG_ARM_s21;      break;
    case DW_REG_ARM_S22:      *regid_out = SYMS_REG_ARM_s22;      break;
    case DW_REG_ARM_S23:      *regid_out = SYMS_REG_ARM_s23;      break;
    case DW_REG_ARM_S24:      *regid_out = SYMS_REG_ARM_s24;      break;
    case DW_REG_ARM_S25:      *regid_out = SYMS_REG_ARM_s25;      break;
    case DW_REG_ARM_S26:      *regid_out = SYMS_REG_ARM_s26;      break;
    case DW_REG_ARM_S27:      *regid_out = SYMS_REG_ARM_s27;      break;
    case DW_REG_ARM_S28:      *regid_out = SYMS_REG_ARM_s28;      break;
    case DW_REG_ARM_S29:      *regid_out = SYMS_REG_ARM_s29;      break;
    case DW_REG_ARM_S30:      *regid_out = SYMS_REG_ARM_s30;      break;
    case DW_REG_ARM_S31:      *regid_out = SYMS_REG_ARM_s31;      break;
    case DW_REG_ARM_F0:       *regid_out = SYMS_REG_ARM_f0;       break;
    case DW_REG_ARM_F1:       *regid_out = SYMS_REG_ARM_f1;       break;
    case DW_REG_ARM_F2:       *regid_out = SYMS_REG_ARM_f2;       break;
    case DW_REG_ARM_F3:       *regid_out = SYMS_REG_ARM_f3;       break;
    case DW_REG_ARM_F4:       *regid_out = SYMS_REG_ARM_f4;       break;
    case DW_REG_ARM_F5:       *regid_out = SYMS_REG_ARM_f5;       break;
    case DW_REG_ARM_F6:       *regid_out = SYMS_REG_ARM_f6;       break;
    case DW_REG_ARM_F7:       *regid_out = SYMS_REG_ARM_f7;       break;
    case DW_REG_ARM_WCGR0:    *regid_out = SYMS_REG_ARM_wcgr0;    break;
    case DW_REG_ARM_WCGR1:    *regid_out = SYMS_REG_ARM_wcgr1;    break;
    case DW_REG_ARM_WCGR2:    *regid_out = SYMS_REG_ARM_wcgr2;    break;
    case DW_REG_ARM_WCGR3:    *regid_out = SYMS_REG_ARM_wcgr3;    break;
    case DW_REG_ARM_WCGR4:    *regid_out = SYMS_REG_ARM_wcgr4;    break;
    case DW_REG_ARM_WCGR5:    *regid_out = SYMS_REG_ARM_wcgr5;    break;
    case DW_REG_ARM_WCGR6:    *regid_out = SYMS_REG_ARM_wcgr6;    break;
    case DW_REG_ARM_WCGR7:    *regid_out = SYMS_REG_ARM_wcgr7;    break;
    case DW_REG_ARM_WR0:      *regid_out = SYMS_REG_ARM_wr0;      break;
    case DW_REG_ARM_WR1:      *regid_out = SYMS_REG_ARM_wr1;      break;
    case DW_REG_ARM_WR2:      *regid_out = SYMS_REG_ARM_wr2;      break;
    case DW_REG_ARM_WR3:      *regid_out = SYMS_REG_ARM_wr3;      break;
    case DW_REG_ARM_WR4:      *regid_out = SYMS_REG_ARM_wr4;      break;
    case DW_REG_ARM_WR5:      *regid_out = SYMS_REG_ARM_wr5;      break;
    case DW_REG_ARM_WR6:      *regid_out = SYMS_REG_ARM_wr6;      break;
    case DW_REG_ARM_WR7:      *regid_out = SYMS_REG_ARM_wr7;      break;
    case DW_REG_ARM_WR8:      *regid_out = SYMS_REG_ARM_wr8;      break;
    case DW_REG_ARM_WR9:      *regid_out = SYMS_REG_ARM_wr9;      break;
    case DW_REG_ARM_WR10:     *regid_out = SYMS_REG_ARM_wr10;     break;
    case DW_REG_ARM_WR11:     *regid_out = SYMS_REG_ARM_wr11;     break;
    case DW_REG_ARM_WR12:     *regid_out = SYMS_REG_ARM_wr12;     break;
    case DW_REG_ARM_WR13:     *regid_out = SYMS_REG_ARM_wr13;     break;
    case DW_REG_ARM_WR14:     *regid_out = SYMS_REG_ARM_wr14;     break;
    case DW_REG_ARM_WR15:     *regid_out = SYMS_REG_ARM_wr15;     break;
    case DW_REG_ARM_SPSR:     *regid_out = SYMS_REG_ARM_spsr;     break;
    case DW_REG_ARM_spsr_FIQ: *regid_out = SYMS_REG_ARM_spsr_fiq; break;
    case DW_REG_ARM_spsr_IRQ: *regid_out = SYMS_REG_ARM_spsr_irq; break;
    case DW_REG_ARM_spsr_ABT: *regid_out = SYMS_REG_ARM_spsr_abt; break;
    case DW_REG_ARM_spsr_UND: *regid_out = SYMS_REG_ARM_spsr_und; break;
    case DW_REG_ARM_spsr_SVC: *regid_out = SYMS_REG_ARM_spsr_svc; break;
    case DW_REG_ARM_r8_USR:   *regid_out = SYMS_REG_ARM_r8_usr;   break;
    case DW_REG_ARM_r9_USR:   *regid_out = SYMS_REG_ARM_r9_usr;   break;
    case DW_REG_ARM_r10_USR:  *regid_out = SYMS_REG_ARM_r10_usr;  break;
    case DW_REG_ARM_r11_USR:  *regid_out = SYMS_REG_ARM_r11_usr;  break;
    case DW_REG_ARM_r12_USR:  *regid_out = SYMS_REG_ARM_r12_usr;  break;
    case DW_REG_ARM_r13_USR:  *regid_out = SYMS_REG_ARM_r13_usr;  break;
    case DW_REG_ARM_r14_USR:  *regid_out = SYMS_REG_ARM_r14_usr;  break;
    case DW_REG_ARM_r8_FIQ:   *regid_out = SYMS_REG_ARM_r8_fiq;   break;
    case DW_REG_ARM_r9_FIQ:   *regid_out = SYMS_REG_ARM_r9_fiq;   break;
    case DW_REG_ARM_r10_FIQ:  *regid_out = SYMS_REG_ARM_r10_fiq;  break;
    case DW_REG_ARM_r11_FIQ:  *regid_out = SYMS_REG_ARM_r11_fiq;  break;
    case DW_REG_ARM_r12_FIQ:  *regid_out = SYMS_REG_ARM_r12_fiq;  break;
    case DW_REG_ARM_r13_FIQ:  *regid_out = SYMS_REG_ARM_r13_fiq;  break;
    case DW_REG_ARM_r14_FIQ:  *regid_out = SYMS_REG_ARM_r14_fiq;  break;
    case DW_REG_ARM_r13_RIQ:  *regid_out = SYMS_REG_ARM_r13_riq;  break;
    case DW_REG_ARM_r14_RIQ:  *regid_out = SYMS_REG_ARM_r14_riq;  break;
    case DW_REG_ARM_r14_ABT:  *regid_out = SYMS_REG_ARM_r14_abt;  break;
    case DW_REG_ARM_r13_ABT:  *regid_out = SYMS_REG_ARM_r13_abt;  break;
    case DW_REG_ARM_r14_UND:  *regid_out = SYMS_REG_ARM_r14_und;  break;
    case DW_REG_ARM_r13_UND:  *regid_out = SYMS_REG_ARM_r13_und;  break;
    case DW_REG_ARM_r14_SVC:  *regid_out = SYMS_REG_ARM_r14_svc;  break;
    case DW_REG_ARM_r13_SVC:  *regid_out = SYMS_REG_ARM_r13_svc;  break;
    case DW_REG_ARM_WC0:      *regid_out = SYMS_REG_ARM_wc0;      break;
    case DW_REG_ARM_WC1:      *regid_out = SYMS_REG_ARM_wc1;      break;
    case DW_REG_ARM_WC2:      *regid_out = SYMS_REG_ARM_wc2;      break;
    case DW_REG_ARM_WC3:      *regid_out = SYMS_REG_ARM_wc3;      break;
    case DW_REG_ARM_WC4:      *regid_out = SYMS_REG_ARM_wc4;      break;
    case DW_REG_ARM_WC5:      *regid_out = SYMS_REG_ARM_wc5;      break;
    case DW_REG_ARM_WC6:      *regid_out = SYMS_REG_ARM_wc6;      break;
    case DW_REG_ARM_WC7:      *regid_out = SYMS_REG_ARM_wc7;      break;
    case DW_REG_ARM_D0:       *regid_out = SYMS_REG_ARM_d0;       break;
    case DW_REG_ARM_D1:       *regid_out = SYMS_REG_ARM_d1;       break;
    case DW_REG_ARM_D2:       *regid_out = SYMS_REG_ARM_d2;       break;
    case DW_REG_ARM_D3:       *regid_out = SYMS_REG_ARM_d3;       break;
    case DW_REG_ARM_D4:       *regid_out = SYMS_REG_ARM_d4;       break;
    case DW_REG_ARM_D5:       *regid_out = SYMS_REG_ARM_d5;       break;
    case DW_REG_ARM_D6:       *regid_out = SYMS_REG_ARM_d6;       break;
    case DW_REG_ARM_D7:       *regid_out = SYMS_REG_ARM_d7;       break;
    case DW_REG_ARM_D8:       *regid_out = SYMS_REG_ARM_d8;       break;
    case DW_REG_ARM_D9:       *regid_out = SYMS_REG_ARM_d9;       break;
    case DW_REG_ARM_D10:      *regid_out = SYMS_REG_ARM_d10;      break;
    case DW_REG_ARM_D11:      *regid_out = SYMS_REG_ARM_d11;      break;
    case DW_REG_ARM_D12:      *regid_out = SYMS_REG_ARM_d12;      break;
    case DW_REG_ARM_D13:      *regid_out = SYMS_REG_ARM_d13;      break;
    case DW_REG_ARM_D14:      *regid_out = SYMS_REG_ARM_d14;      break;
    case DW_REG_ARM_D15:      *regid_out = SYMS_REG_ARM_d15;      break;
    case DW_REG_ARM_D16:      *regid_out = SYMS_REG_ARM_d16;      break;
    case DW_REG_ARM_D17:      *regid_out = SYMS_REG_ARM_d17;      break;
    case DW_REG_ARM_D18:      *regid_out = SYMS_REG_ARM_d18;      break;
    case DW_REG_ARM_D19:      *regid_out = SYMS_REG_ARM_d19;      break;
    case DW_REG_ARM_D20:      *regid_out = SYMS_REG_ARM_d20;      break;
    case DW_REG_ARM_D21:      *regid_out = SYMS_REG_ARM_d21;      break;
    case DW_REG_ARM_D22:      *regid_out = SYMS_REG_ARM_d22;      break;
    case DW_REG_ARM_D23:      *regid_out = SYMS_REG_ARM_d23;      break;
    case DW_REG_ARM_D24:      *regid_out = SYMS_REG_ARM_d24;      break;
    case DW_REG_ARM_D25:      *regid_out = SYMS_REG_ARM_d25;      break;
    case DW_REG_ARM_D26:      *regid_out = SYMS_REG_ARM_d26;      break;
    case DW_REG_ARM_D27:      *regid_out = SYMS_REG_ARM_d27;      break;
    case DW_REG_ARM_D28:      *regid_out = SYMS_REG_ARM_d28;      break;
    case DW_REG_ARM_D29:      *regid_out = SYMS_REG_ARM_d29;      break;
    case DW_REG_ARM_D30:      *regid_out = SYMS_REG_ARM_d30;      break;
    case DW_REG_ARM_D31:      *regid_out = SYMS_REG_ARM_d31;      break;
    default: is_mapped = syms_false; break;
    }
#endif
  } break;

  default: is_mapped = syms_false; break;
  }

  return is_mapped;
}

SYMS_INTERNAL
DW_REGWRITE_SIG(syms_regwrite_dwarf)
{
  SymsRegwrite *info = (SymsRegwrite *)context;
  SymsRegID regid = SYMS_REG_null;
  syms_uint write_size = 0;
  if (syms_dw_regid_to_regid(arch, reg_index, &regid)) {
    write_size = info->callback(info->context, arch, regid, value, syms_trunc_u32(value_size));
  }
  return (dw_uint)write_size;
}

SYMS_INTERNAL
DW_REGREAD_SIG(syms_regread_dwarf)
{
  SymsRegread *info = (SymsRegread *)context;
  SymsRegID regid = SYMS_REG_null;
  syms_uint read_size = 0;
  if (syms_dw_regid_to_regid(arch, reg_index, &regid)) {
    read_size = info->callback(info->context, arch, regid, read_buffer, syms_trunc_u32(read_buffer_max));
  }
  return (dw_uint)read_size;
}

SYMS_INTERNAL
DW_MEMREAD_SIG(syms_memread_dwarf)
{
  SymsMemread *memread = (SymsMemread *)context;
  memread->result = memread->callback(memread->context, va, read_buffer, num_read);
  return SYMS_RESULT_OK(memread->result) ? syms_true : syms_false;
}

SYMS_INTERNAL syms_bool
syms_debug_file_iter_init_dwarf(SymsInstance *instance, SymsDebugFileIterDwarf *iter)
{
  SymsSecIter sec_iter;
  if (syms_sec_iter_init(instance, &sec_iter)) {
    SymsSection sec;
    SymsSection debug_info;
    SymsSection debug_abbrev;
    SymsSection debug_str;
    syms_memzero(&debug_info, sizeof(debug_info));
    syms_memzero(&debug_abbrev, sizeof(debug_abbrev));
    syms_memzero(&debug_str, sizeof(debug_str));
    while (syms_sec_iter_next(&sec_iter, &sec)) {
      if (syms_string_cmp_lit(sec.name, ".debug_info")) {
        debug_info = sec;
      } else if (syms_string_cmp_lit(sec.name, ".debug_abbrev")) {
        debug_abbrev = sec;
      } else if (syms_string_cmp_lit(sec.name, ".debug_str")) {
        debug_str = sec;
      }
    }

    DwInitdata init_data;
    init_data.secs[DW_SEC_INFO].data_len = debug_info.data_size;
    init_data.secs[DW_SEC_INFO].data = debug_info.data;
    init_data.secs[DW_SEC_ABBREV].data_len = debug_abbrev.data_size;
    init_data.secs[DW_SEC_ABBREV].data = debug_abbrev.data;
    init_data.secs[DW_SEC_STR].data_len = debug_str.data_size;
    init_data.secs[DW_SEC_STR].data = debug_str.data;

    if (dw_init(&iter->context, syms_get_arch(instance), &init_data)) {
      return dw_cu_iter_init(&iter->cu_iter, &iter->context);
    }
  }
  return syms_false;
}

SYMS_INTERNAL syms_bool
syms_debug_file_iter_next_dwarf(SymsDebugFileIterDwarf *iter, SymsString *dwo_path_out)
{
  DwCompileUnit cu;
  syms_bool is_valid = syms_false;
  while (dw_cu_iter_next(&iter->cu_iter, &cu)) {
    *dwo_path_out = cu.dwo_name;
    //*dwo_id_out = cu.dwo_id;
    is_valid = !syms_string_is_null(*dwo_path_out);
    if (is_valid) {
      break;
    }
  }
  return is_valid;
}
