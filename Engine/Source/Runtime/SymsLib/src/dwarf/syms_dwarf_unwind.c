// Copyright Epic Games, Inc. All Rights Reserved.
SYMS_INTERNAL const char *
dw_get_reg_name(SymsArch arch, U32 regid)
{
  switch (arch) {
  case SYMS_ARCH_X86: {
#define X(arch, name, value) case value: return #name;
    switch (regid) {
      DW_REG_X86_LIST
    }
#undef X
  } break;

  case SYMS_ARCH_X64: {
#define X(arch, name, value) case value: return #name;
    switch (regid) {
      DW_REG_X64_LIST
    }
#undef X
  } break;

  case SYMS_ARCH_ARM: {
#define X(arch, name, value) case value: return #name;
    switch (regid) {
      DW_REG_ARM_LIST
    }
#undef X
  } break;

  default: SYMS_INVALID_CODE_PATH; break;
  }

  return "<uknown-regid>";
}

SYMS_INTERNAL syms_bool
dw_is_cie_id(DwFrameInfo *frame_info, SymsSWord id)
{
  syms_bool result = syms_false;
  switch (frame_info->source_type) {
  case DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME: result = id == -1 || id == -1; break;
  case DW_VIRTUAL_UNWIND_DATA_EH_FRAME:    result = (id == 0);  break;
  case DW_VIRTUAL_UNWIND_DATA_NULL:        result = syms_false; break;
  }
  return result;
}

SYMS_INTERNAL dw_uint
dw_regread(DwUserCallbacks *user_cbs, dw_uint regid, void *value, dw_uint value_size)
{
  dw_uint read_size = 0;

  if (user_cbs->regread != NULL) {
    read_size = user_cbs->regread(user_cbs->regread_ctx, user_cbs->arch_info.arch, regid, value, value_size);
  } else {
    SYMS_ASSERT_FAILURE("callback for reading registers is not specified");
  }
   
  return read_size;
}

SYMS_INTERNAL dw_uint
dw_regwrite(DwUserCallbacks *user_cbs, dw_uint regid, void *value, dw_uint value_size)
{
  dw_uint write_size = 0;

  if (user_cbs->regwrite != NULL) {
    write_size = user_cbs->regwrite(user_cbs->regwrite_ctx, user_cbs->arch_info.arch, regid, value, value_size);
  } else {
    SYMS_ASSERT_FAILURE("callback for writing registers is not specified");
  }

  return write_size;
}

SYMS_INTERNAL syms_bool
dw_regread_uword(DwUserCallbacks *user_cbs, dw_uint regid, SymsUWord *value_out)
{
  dw_uint read_size;
  syms_bool result;

  SYMS_ASSERT(user_cbs->arch_info.word_size <= sizeof(*value_out));
  *value_out = 0;
  read_size = dw_regread(user_cbs, regid, value_out, user_cbs->arch_info.word_size);
  result = (read_size == user_cbs->arch_info.word_size);

  return result;
}

SYMS_INTERNAL syms_bool
dw_regread_sword(DwUserCallbacks *user_cbs, dw_uint regid, SymsSWord *value_out)
{
  dw_uint read_size;
  syms_bool result;

  SYMS_ASSERT(user_cbs->arch_info.word_size <= sizeof(*value_out));
  *value_out = 0;
  read_size = dw_regread(user_cbs, regid, value_out, user_cbs->arch_info.word_size);
  result = (read_size == user_cbs->arch_info.word_size);

  return result;
}

SYMS_INTERNAL syms_bool
dw_regwrite_uword(DwUserCallbacks *user_cbs, S64 regid, SymsUWord *value)
{
  dw_uint write_size;
  syms_bool result = syms_false;
  if (regid >= 0) {
    SYMS_ASSERT(user_cbs->arch_info.word_size <= sizeof(*value));
    write_size = dw_regwrite(user_cbs, syms_trunc_u32(regid), value, user_cbs->arch_info.word_size);
    result = (write_size == user_cbs->arch_info.word_size);
  }
  return result;
}

SYMS_INTERNAL syms_bool
dw_regwrite_sword(DwUserCallbacks *user_cbs, dw_uint regid, SymsSWord *value)
{
  dw_uint write_size = dw_regwrite(user_cbs, regid, value, user_cbs->arch_info.word_size);
  syms_bool result = (write_size == user_cbs->arch_info.word_size);
  return result;
}

SYMS_INTERNAL syms_bool
dw_memread(DwUserCallbacks *user_cbs, SymsAddr va, void *buffer, dw_uint buffer_size)
{
  /* TODO(nick): replace bool with byte read counter */
  syms_bool result = syms_false;

  if (user_cbs->memread != NULL) {
    result = user_cbs->memread(user_cbs->memread_ctx, va, buffer, buffer_size);
  } else {
    SYMS_ASSERT_FAILURE("callback for reading memory is not specified");
  }

  return result;
}

SYMS_INTERNAL syms_bool
dw_parse_cie(DwFrameInfo *frame_info, DwBinRead *secdata, DwCommonInfoEntry *cie_out)
{
  dw_uint i;
  SymsAddr augdata_end;

  cie_out->init_cfi_offset = DW_INVALID_OFFSET;
  cie_out->init_cfi_size = 0;
  cie_out->end_offset = DW_INVALID_OFFSET;

  cie_out->cie_id = 0;
  cie_out->augmentation = syms_string_init(0,0);

  cie_out->is_aug_sized = syms_false;
  cie_out->is_sig_frame = syms_false;
  cie_out->have_abi_maker = syms_false;

  cie_out->lsda_encoding = DW_EH_PE_OMIT;
  cie_out->fde_encoding = DW_EH_PE_OMIT;
  cie_out->handler_encoding = DW_EH_PE_OMIT;

  cie_out->addr_size = frame_info->arch_info.addr_size;
  cie_out->segsel_size = 0;
  cie_out->code_align = 0;
  cie_out->data_align = 0;
  cie_out->ret_addr_reg = 0;

  switch (frame_info->arch_info.addr_size) {
  case 4:     cie_out->fde_encoding = DW_EH_PE_UDATA4; break;
  case 8:     cie_out->fde_encoding = DW_EH_PE_UDATA8; break;
  default:    cie_out->fde_encoding = DW_EH_PE_OMIT;   break;
  }

  {
    U64 cie_size;

    cie_size = dw_bin_read_u32(secdata);
    if (cie_size != 0xFFFFFFFFul) {
      cie_out->end_offset = secdata->off + cie_size;
      cie_out->cie_id = dw_bin_read_u32(secdata);
    } else {
      cie_size = dw_bin_read_u64(secdata);
      cie_out->end_offset = secdata->off + cie_size;
      cie_out->cie_id = dw_bin_read_u64(secdata);
    }

    switch (frame_info->source_type) {
    case DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME: {
      if (cie_out->cie_id != SYMS_UINT32_MAX || cie_out->cie_id != SYMS_UINT64_MAX) {
        return syms_false;
      }
    } break;

    case DW_VIRTUAL_UNWIND_DATA_EH_FRAME: {
      if (cie_out->cie_id != 0) {
        return syms_false;
      }
    } break;

    default: SYMS_INVALID_CODE_PATH; break;
    }
  }

  cie_out->version = dw_bin_read_u08(secdata);
  if (cie_out->version != 1 && (cie_out->version < DW_CIE_VERSION || cie_out->version > DW_CIE_VERSION_MAX)) {
    return syms_false;
  }

  cie_out->augmentation = dw_bin_read_string(secdata);
#if 0
  if (source_type == DW_STACK_UNWIND_DATA_SOURCE_DEBUG_FRAME) {
    cie_out->addr_size = dw_bin_read_sleb128(secdata);
    cie_out->segsel_size = dw_bin_read_sleb128(secdata);
  } else {
    cie_out->addr_size = 0;
    cie_out->segsel_size = 0;
  }
#endif
  cie_out->code_align = dw_bin_read_uleb128(secdata);
  cie_out->data_align = dw_bin_read_sleb128(secdata);

  if (cie_out->version == 1) {
    cie_out->ret_addr_reg = dw_bin_read_u08(secdata);
  } else {
    cie_out->ret_addr_reg = dw_bin_read_uleb128(secdata);
  }

  augdata_end = DW_INVALID_OFFSET;
  for (i = 0; i < cie_out->augmentation.len; ++i) {
    switch (syms_string_peek_byte(cie_out->augmentation, i)) {
    case 'z': {
      U64 augdata_size = dw_bin_read_uleb128(secdata);
      augdata_end = secdata->off + augdata_size;
      cie_out->is_aug_sized = syms_true;
    } break;

    case 'L': {
      cie_out->lsda_encoding = dw_bin_read_u08(secdata);
      SYMS_ASSERT(cie_out->lsda_encoding);
    } break;

    case 'R': {
      cie_out->fde_encoding = dw_bin_read_u08(secdata);
      SYMS_ASSERT(cie_out->fde_encoding);
    } break;

    case 'S': {
      cie_out->is_sig_frame = syms_true;
      cie_out->have_abi_maker = syms_true;
    } break;

    case 'P': {
      cie_out->handler_encoding = dw_bin_read_u08(secdata);
      SYMS_ASSERT(cie_out->handler_encoding);
      cie_out->handler_ip = dw_parse_pointer(frame_info, secdata, cie_out->handler_encoding);
    } break;

    default: {
      if (!dw_bin_seek(secdata, augdata_end)) {
        return syms_false;
      }
    } goto exit_augdata_read;
    }
  }
  exit_augdata_read:;

  SYMS_ASSERT(cie_out->end_offset >= secdata->off);
  cie_out->init_cfi_offset = secdata->off;
  cie_out->init_cfi_size = cie_out->end_offset - secdata->off;

  return syms_true;
}

SYMS_INTERNAL SymsAddr
dw_parse_pointer(DwFrameInfo *frame_info, DwBinRead *secdata, dw_uint encoding)
{
  SymsAddr init_off = secdata->off;
  SymsAddr pointer = 0;

  if (encoding == DW_EH_PE_OMIT) {
    return 0;
  } 
  if ((encoding & DW_EH_PE_APPLY_MASK) == DW_EH_PE_ALIGNED) {
    SymsAddr align;

    SYMS_ASSERT(secdata->addr_size > 0);
    align = (init_off + secdata->addr_size - 1) & (-secdata->addr_size);
    if (!dw_bin_seek(secdata, align)) {
      return 0;
    }
  }

  if ((encoding & DW_EH_PE_FORMAT_MASK) == DW_EH_PE_PTR) {
    encoding &= ~DW_EH_PE_PTR;
    switch (secdata->mode) {
    case DW_MODE_32BIT: encoding |= DW_EH_PE_SDATA4; break;
    case DW_MODE_64BIT: encoding |= DW_EH_PE_SDATA8; break;
    default: SYMS_INVALID_CODE_PATH;
    }
  }

  switch (encoding & DW_EH_PE_FORMAT_MASK) {
  case DW_EH_PE_UDATA2:   pointer = dw_bin_read_u16(secdata);     break;
  case DW_EH_PE_UDATA4:   pointer = dw_bin_read_u32(secdata);     break;
  case DW_EH_PE_UDATA8:   pointer = dw_bin_read_u64(secdata);     break;
  case DW_EH_PE_ULEB128:  pointer = dw_bin_read_uleb128(secdata); break;

  case DW_EH_PE_SDATA2:   pointer = (SymsAddr)dw_bin_read_s16(secdata);     break;
  case DW_EH_PE_SDATA4:   pointer = (SymsAddr)dw_bin_read_s32(secdata);     break;
  case DW_EH_PE_SDATA8:   pointer = (SymsAddr)dw_bin_read_s64(secdata);     break;
  case DW_EH_PE_SLEB128:  pointer = (SymsAddr)dw_bin_read_sleb128(secdata); break;

  default: SYMS_INVALID_CODE_PATH; break;
  }

  switch (encoding & DW_EH_PE_APPLY_MASK) {
  case DW_EH_PE_ALIGNED: {
    /* NOTE(nick): Ignore */
  } break;

  case DW_EH_PE_ABSPTR: {
    /* NOTE(nick): leave pointer as is */
  } break;

  case DW_EH_PE_DATAREL: {
    SYMS_ASSERT_FAILURE("DW_EH_PE_DATAREL not implemented");
  } break;

  case DW_EH_PE_FUNCREL: {
    SYMS_ASSERT_FAILURE("DW_EH_PE_FUNCREL not implemented");
  } break;

  case DW_EH_PE_PCREL: {
    SYMS_ASSERT(frame_info->eh_frame);
    pointer += frame_info->eh_frame + init_off;
  } break;

  case DW_EH_PE_TEXTREL: {
    // TODO(nick): IMPLEMENT
    SYMS_ASSERT_FAILURE("DW_EH_PE_TEXTREL is not implemented");
  }
  default: SYMS_INVALID_CODE_PATH; break;
  }

  SYMS_ASSERT(pointer);
  return pointer;
}

SYMS_INTERNAL syms_bool
dw_cfi_program_init(SymsImageType img_type, DwCfiProgram *program)
{
  dw_uint i;
  syms_bool is_inited;

  switch (img_type) {
  case SYMS_IMAGE_NULL: program->reg_count = 0;                             break;
  case SYMS_IMAGE_NT:   program->reg_count = DW_PRESERVED_REGS_COUNT_NT;    break;
  case SYMS_IMAGE_ELF:  program->reg_count = DW_PRESERVED_REGS_COUNT_UNIX;  break;
  }

  for (i = 0; i < SYMS_ARRAY_SIZE(program->rules.value); ++i) {
    program->rules.type[i] = DW_CFI_REGISTER_TYPE_SAME;
    program->rules.value[i].w = 0;
  }

  program->frame           = &program->stack[DW_CFI_PROGRAM_STACK_MAX - 1];
  program->setup_cfa       = syms_true;
  program->ret_addr_regid  = SYMS_UINT32_MAX;
  program->cfa             = 0;

  is_inited = (program->reg_count > 0);
  return is_inited;
}

SYMS_INTERNAL syms_bool
dw_compile_cfi_table_row(DwCfiProgram *program, DwFrameInfo *frame_info, DwCommonInfoEntry *cie, DwFrameDescEntry *fde, DwBinRead *cfi_data, SymsAddr ip)
{
  SymsAddr decode_ip;
  DwCfiRow *rules_frame = &program->stack[DW_CFI_PROGRAM_STACK_MAX - 1];
   
  if (fde) {
    decode_ip = fde->start_ip;
    if (ip > frame_info->image_base) {
      decode_ip += frame_info->image_base;
    }
  } else {
    decode_ip = 0;
  }

  while (cfi_data->off < cfi_data->max) {
    U8 opcode = dw_bin_read_u08(cfi_data);
    U8 operand = 0;

    if ((opcode & DW_CFA_OPCODE_MASK) != 0) {
      operand = opcode & DW_CFA_OPER_MASK;
      opcode = opcode & DW_CFA_OPCODE_MASK;
    }

    if (decode_ip >= ip) {
      /* NOTE(nick): Instruction Pointer might be at any point in procedure body:
       * prologue, body, or epilogue. And instructions that manage
       * stack pointer might not be executed yet and we have to take this into account
       * by comparing virtual-instruction-pointer 'decode_ip' with instruction pointer 'ip',
       * which is actual location in procedures body. */
      break;
    }

    switch (opcode) {
    case DW_CFA_NOP: {
    } break;

    case DW_CFA_SAME_VALUE: {
      SymsUWord reg = dw_bin_read_uleb128(cfi_data);
      if (reg < program->reg_count) {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_SAME;
        program->rules.value[reg].w = 0;
      }
    } break;

    case DW_CFA_UNDEFINED: {
      SymsUWord reg = dw_bin_read_uleb128(cfi_data);
      if (reg < program->reg_count) {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_UNDEF;
        program->rules.value[reg].w = 0;
      }
    } break;

    case DW_CFA_ADVANCE_LOC: {
      decode_ip += operand * cie->code_align;
    } break;

    case DW_CFA_ADVANCE_LOC1: {
      U8 value = dw_bin_read_u08(cfi_data);
      decode_ip += value * cie->code_align;
    } break;

    case DW_CFA_ADVANCE_LOC2: {
      U16 value = dw_bin_read_u16(cfi_data);
      decode_ip += value * cie->code_align;
    } break;

    case DW_CFA_ADVANCE_LOC4: {
      dw_uint value = dw_bin_read_u32(cfi_data);
      decode_ip += value * cie->code_align;
    } break;

    case DW_CFA_OFFSET: {
      DwOffset reg = operand;
      if (reg < program->reg_count) {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_CFAREL;
        program->rules.value[reg].w = (S64)dw_bin_read_uleb128(cfi_data) * cie->data_align;
      } else {
        // TODO(nick): handle invalid register
      }
    } break;

    case DW_CFA_VAL_OFFSET: {
      DwOffset reg = dw_bin_read_sleb128(cfi_data);
      DwOffset value = dw_bin_read_sleb128(cfi_data);

      if ((dw_uint)reg < program->reg_count) {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_CFAREL;
        program->rules.value[reg].w = value * cie->data_align;
      } else {
        // TODO(nick): handle invalid register
      }
    } break;

    case DW_CFA_SET_LOC: {
      decode_ip = dw_parse_pointer(frame_info, cfi_data, cie->fde_encoding);
    } break;

    case DW_CFA_DEF_CFA_OFFSET: {
      program->rules.type[DW_CFA_COLUMN_OFF] = DW_CFI_REGISTER_TYPE_UNDEF;
      program->rules.value[DW_CFA_COLUMN_OFF].w = (S64)dw_bin_read_uleb128(cfi_data);
    } break;

    case DW_CFA_DEF_CFA_REGISTER: {
      SymsUWord reg = dw_bin_read_uleb128(cfi_data);

      if (reg < program->reg_count) {
        program->rules.type[DW_CFA_COLUMN_REG] = DW_CFI_REGISTER_TYPE_REG;
        program->rules.value[DW_CFA_COLUMN_REG].w = (SymsSWord)reg;
      } else {
        // TODO(nick): handle invalid register
      }
    } break;

    case DW_CFA_EXPR: {
      dw_uint reg = dw_bin_read_uleb128(cfi_data);
      dw_uint expr_size = dw_bin_read_uleb128(cfi_data);

      if (expr_size <= SYMS_UINT32_MAX) {
        if (reg < program->reg_count) {
          if (dw_bin_skip(cfi_data, expr_size)) {
            program->rules.type[reg] = DW_CFI_REGISTER_TYPE_EXPR;
            program->rules.value[reg].e.ops = dw_bin_at(cfi_data);
            program->rules.value[reg].e.ops_size = expr_size;
            program->rules.value[reg].e.frame_base = 0;
            program->rules.value[reg].e.member_location = 0;
            program->rules.value[reg].e.cfa = program->cfa;
          } else {
            // TODO(nick): handle invalid expression size
          }
        } else {
          // TODO(nick): handle invalid register
        }
      } else {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_INVALID;
        program->rules.value[reg].e.ops = 0;
        program->rules.value[reg].e.ops_size = 0;
        program->rules.value[reg].e.frame_base = 0;
        program->rules.value[reg].e.member_location = 0;
        program->rules.value[reg].e.cfa = 0;
      }
    } break;

    case DW_CFA_DEF_CFA: {
      /* NOTE(nick): Defining Canonical Frame Address as 'register + offset' */

      SymsUWord reg = dw_bin_read_uleb128(cfi_data);
      SymsUWord off = dw_bin_read_uleb128(cfi_data);

      if (reg < program->reg_count) {
        program->rules.type[DW_CFA_COLUMN_REG] = DW_CFI_REGISTER_TYPE_REG;
        program->rules.value[DW_CFA_COLUMN_REG].w = (SymsSWord)reg;

        program->rules.type[DW_CFA_COLUMN_OFF] = DW_CFI_REGISTER_TYPE_UNDEF;
        program->rules.value[DW_CFA_COLUMN_OFF].w = (SymsSWord)off;
      } else {
        // TODO(nick): handle invalid register
      }
    } break;

    case DW_CFA_DEF_CFA_EXPR: {
      /* NOTE(nick): Defining Canonical Frame Address as a dwarf expression */

      U64 expr_size = dw_bin_read_uleb128(cfi_data);
      if (expr_size < SYMS_UINT32_MAX) {
        void *expr = dw_bin_at(cfi_data);
        if (dw_bin_skip(cfi_data, expr_size)) {
          program->rules.type[DW_CFA_COLUMN_REG] = DW_CFI_REGISTER_TYPE_EXPR;
          program->rules.value[DW_CFA_COLUMN_REG].e.ops = expr;
          program->rules.value[DW_CFA_COLUMN_REG].e.ops_size = (dw_uint)expr_size;
          program->rules.value[DW_CFA_COLUMN_REG].e.frame_base = 0;
          program->rules.value[DW_CFA_COLUMN_REG].e.member_location = 0;
          program->rules.value[DW_CFA_COLUMN_REG].e.cfa = program->cfa;
        }
      } else {
        program->rules.type[DW_CFA_COLUMN_REG] = DW_CFI_REGISTER_TYPE_INVALID;
        program->rules.value[DW_CFA_COLUMN_REG].e.ops = 0;
        program->rules.value[DW_CFA_COLUMN_REG].e.ops_size = 0;
        program->rules.value[DW_CFA_COLUMN_REG].e.frame_base = 0;
        program->rules.value[DW_CFA_COLUMN_REG].e.member_location = 0;
        program->rules.value[DW_CFA_COLUMN_REG].e.cfa = 0;
      }
    } break;

    case DW_CFA_REGISTER: {
      SymsUWord reg = dw_bin_read_uleb128(cfi_data);
      SymsUWord value = dw_bin_read_uleb128(cfi_data);

      if (reg < program->reg_count) {
        program->rules.type[reg] = DW_CFI_REGISTER_TYPE_REG;
        program->rules.value[reg].w = (SymsSWord)value;
      } else {
        // TODO(nick): handle invalid register
      }
    } break;

    case DW_CFA_REMEMBER_STATE: {
      if (rules_frame < &program->stack[0]) {
        SYMS_ASSERT_FAILURE("stack overflow");
        return syms_false;
      }

      *rules_frame = program->rules;
      --rules_frame;
    } break;

    case DW_CFA_RESTORE_STATE: {
      if (rules_frame >= &program->stack[DW_CFI_PROGRAM_STACK_MAX]) {
        SYMS_ASSERT_FAILURE("stack underflow");
        return syms_false;
      }

      ++rules_frame;
      program->rules = *rules_frame;
    } break;

    case DW_CFA_VAL_EXPR: {
      SYMS_ASSERT_FAILURE("DW_CFA_VAL_EXPR");
    } break;

    case DW_CFA_RESTORE: {
      SYMS_ASSERT_FAILURE("DW_CFA_RESTORE");
    } break;

    case DW_CFA_OFFSET_EXT: {
      SYMS_ASSERT_FAILURE("DW_CFA_OFFSET_EXT");
    } break;

    case DW_CFA_RESTORE_EXT: {
      SYMS_ASSERT_FAILURE("DW_CFA_RESTORE_EXT");
    } break;

    case DW_CFA_OFFSET_EXT_SF: {
      SYMS_ASSERT_FAILURE("DW_CFA_OFFSET_EXT_SF");
    } break;

    case DW_CFA_DEF_CFA_SF: {
      SYMS_ASSERT_FAILURE("DW_CFA_DEF_CFA_SF");
    } break;

    case DW_CFA_DEF_CFA_OFFSET_SF: {
      SYMS_ASSERT_FAILURE("DW_CFA_DEF_CFA_OFFSET_SF");
    } break;

    default: {
      if (opcode >= DW_CFA_USER_LO && opcode <= DW_CFA_USER_HI) {
        // NOTE(nick): User implemented CFA opcode
      } else {
        SYMS_INVALID_CODE_PATH;
      }
    } break;
    }
  }

  return syms_true;
}

SYMS_INTERNAL syms_bool
dw_apply_cfi_table_row(DwCfiProgram *program, DwFrameInfo *frame, DwUserCallbacks *user_cbs)
{
  SymsUWord cfa = 0;
  dw_uint regid;
  
  if (program->rules.type[DW_CFA_COLUMN_REG] == DW_CFI_REGISTER_TYPE_REG) {
    SYMS_ASSERT(sizeof(cfa) >= frame->arch_info.word_size);
    if (program->rules.value[DW_CFA_COLUMN_REG].w == DW_REG_X64_RSP && 
        program->reg_count > DW_REG_X64_RSP && 
        program->rules.type[DW_REG_X64_RSP] == DW_CFI_REGISTER_TYPE_SAME) {
      cfa = program->cfa;
    } else {
      if (program->rules.value[DW_CFA_COLUMN_REG].w < 0) {
        return syms_false;
      }
      regid = (dw_uint)program->rules.value[DW_CFA_COLUMN_REG].w;
      if (!dw_regread_uword(user_cbs, regid, &cfa)) {
        return syms_false;
      }
    }
    if (program->rules.value[DW_CFA_COLUMN_OFF].w < 0 && program->rules.value[DW_CFA_COLUMN_OFF].w < (SymsSWord)cfa) {
      return syms_false;
    }
    cfa += (dw_uint)program->rules.value[DW_CFA_COLUMN_OFF].w;
  } else {
    DwLocation loc;
    syms_bool is_decoded;

    SYMS_ASSERT(program->rules.type[DW_CFA_COLUMN_REG] == DW_CFI_REGISTER_TYPE_EXPR);
    is_decoded = dw_decode_location_expr(
        &program->rules.value[DW_CFA_COLUMN_REG].e, 
        frame->arch_info.mode, frame->arch_info.arch,
        user_cbs->memread_ctx, user_cbs->memread,
        user_cbs->regread_ctx, user_cbs->regread,
        &loc);

    if (is_decoded && loc.type == DW_LOCATION_ADDR) {
      cfa = loc.u.addr;
    } else {
      return syms_false;
    }
  }

  for (regid = 0; regid < program->reg_count; ++regid) {
    DwCfiRegisterType rule_type = program->rules.type[regid];
    DwCfiRegValue rule_value = program->rules.value[regid];

    switch (rule_type) {
    case DW_CFI_REGISTER_TYPE_UNDEF: {
      /* NOTE(nick): Undefined register value */

      if (program->ret_addr_regid == regid) {
        SymsUWord dummy_value = 0;
        if (!dw_regwrite_uword(user_cbs, program->ret_addr_regid, &dummy_value)) {
          return syms_false;
        }
      }
    } break;

    case DW_CFI_REGISTER_TYPE_SAME: {
      /* NOTE(nick): Register value is not changed */
    } break;

    case DW_CFI_REGISTER_TYPE_CFAREL: {
      SymsAddr value_va;
      SymsUWord value;

      SymsSWord temp = (SymsSWord)cfa + rule_value.w;
      if (temp < 0) {
        return syms_false;
      }
      value_va = (SymsAddr)temp;
      value = 0;
      if (!dw_memread(user_cbs, value_va, &value, frame->arch_info.word_size)) {
        return syms_false;
      }

      if (!dw_regwrite_uword(user_cbs, regid, &value)) {
        return syms_false;
      }
    } break;

    case DW_CFI_REGISTER_TYPE_REG: {
      SymsSWord value;

      if (rule_value.w < 0) {
        return syms_false;
      }
      value = 0;
      if (!dw_regread_sword(user_cbs, (dw_uint)rule_value.w, &value)) {
        return syms_false;
      }
      value += program->rules.value[rule_value.w].w;
      if (!dw_regwrite_sword(user_cbs, regid, &value)) {
        return syms_false;
      }
    } break;

    case DW_CFI_REGISTER_TYPE_EXPR: {
      SYMS_ASSERT_FAILURE("IMPLEMENT::CFI_REGISTER_TYPE_EXPR");
    } break;

    case DW_CFI_REGISTER_TYPE_VAL_EXPR: {
      SYMS_ASSERT_FAILURE("IMPLEMENT::CFI_REGISTER_TYPE_VAL_EXPR");
    } break;

    default: SYMS_ASSERT_FAILURE("IMPLEMENT::INVALID_DEFAULT_CASE");
    }
  }

  program->cfa = cfa;

  return syms_true;
}

SYMS_INTERNAL syms_bool
dw_fde_iter_init(DwFrameInfo *frame_info, DwBinRead frame_data, DwFrameDescEntryIter *it_out)
{
  syms_bool result = syms_false;

  it_out->frame_info = frame_info;
  it_out->secdata = frame_data;
  it_out->cie_offset = 0;

  if (dw_bin_seek(&it_out->secdata, it_out->cie_offset)) {
    if (dw_parse_cie(frame_info, &it_out->secdata, &it_out->cie)) {
      if (dw_bin_seek(&it_out->secdata, it_out->cie.end_offset)) {
        result = syms_true;
      }
    }
  }

  return result;
}

SYMS_INTERNAL syms_bool
dw_fde_iter_next(DwFrameDescEntryIter *it, DwFrameDescEntry *fde_out)
{
  DwBinRead *secdata = &it->secdata;
  SymsAddr fde_end_offset;
  SymsAddr cie_offset;
  U64 fde_size;
  SymsAddr base_offset;
  DwCommonInfoEntry *cie;

reparse:;

  fde_end_offset = 0;
  cie_offset = 0;
  base_offset = secdata->off;
  cie = NULL;

  fde_size = dw_bin_read_u32(secdata);
  if (fde_size != 0xFFFFFFFF) {
    S32 cie_id;

    if (fde_size == 0) {
      return syms_false;
    }

    fde_end_offset = secdata->off + fde_size;

    cie_id = dw_bin_read_s32(secdata);

    if (dw_is_cie_id(it->frame_info, cie_id)) {
      SYMS_ASSERT(fde_size);

      if (!dw_bin_seek(secdata, base_offset + fde_size + 4)) {
        // TODO(nick): handle corrupted data
        return syms_false;
      }

      goto reparse;
    }
    if (cie_id < 0) {
      return syms_false;
    }
    cie_offset = (SymsAddr)cie_id;

    switch (it->frame_info->source_type) {
    case DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME: {
      cie_offset += base_offset;
    } break;

    case DW_VIRTUAL_UNWIND_DATA_EH_FRAME: {
      if (cie_offset <= base_offset + 4) {
        return syms_false;
      }
      cie_offset = (base_offset + 4) - cie_offset;
    } break;

    default: break;
    }
  } else {
    SYMS_ASSERT_FAILURE("64bit version not implemented yet");
  }

  fde_out->data_off = base_offset;
  fde_out->data_size = fde_size;

  if (it->cie_offset != cie_offset ) {
    SymsAddr pushed_offset = secdata->off;
    dw_bin_seek(secdata, cie_offset);
    if (!dw_parse_cie(it->frame_info, secdata, &it->cie)) {
      SYMS_INVALID_CODE_PATH; // TODO(nick): error handling
    }
    dw_bin_seek(secdata, pushed_offset);

    it->cie_offset = cie_offset;
  }
  cie = &it->cie;

  fde_out->start_ip = 0;
  fde_out->range_ip = 0;
  fde_out->lsda_ip = 0;

  switch (it->frame_info->source_type) {
  case DW_VIRTUAL_UNWIND_DATA_EH_FRAME: {
    fde_out->start_ip = dw_parse_pointer(it->frame_info, secdata, cie->fde_encoding);
    fde_out->range_ip = dw_parse_pointer(it->frame_info, secdata, cie->fde_encoding & DW_EH_PE_FORMAT_MASK);

    if (cie->is_aug_sized) {
      SymsAddr aug_size = dw_bin_read_uleb128(secdata);
      SymsAddr aug_end_addr = secdata->off + aug_size;

      if (!dw_bin_skip(secdata, aug_size)) {
        return syms_false;
      }

      if (aug_end_addr != secdata->off) {
        // TODO(nick): handle corrupted data
        return syms_false;
      }
    }
  } break;

  case DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME: {
    fde_out->start_ip = dw_bin_read_addr(secdata);
    fde_out->range_ip = dw_bin_read_addr(secdata);
  } break;

  default: SYMS_INVALID_CODE_PATH;
  }
  if (fde_out->start_ip == 0 || fde_out->range_ip == 0) {
    return syms_false;
  }

  if (cie->have_abi_maker) {
    U16 abi = dw_bin_read_u16(secdata);
    U16 tag = dw_bin_read_u16(secdata);
    (void)abi; (void)tag;
  }

  fde_out->cfi_offset = secdata->off;
  fde_out->cfi_size = fde_end_offset - secdata->off;

  if (!dw_bin_seek(secdata, fde_end_offset)) {
    return syms_false;
  }

  return syms_true;
}

SYMS_INTERNAL syms_bool
dw_find_fde(DwFrameInfo *frame_info, DwBinRead frame_data, SymsAddr ip, DwCommonInfoEntry *cie_out, DwFrameDescEntry *fde_out)
{
  DwFrameDescEntryIter it;
  syms_bool result = syms_false;

  // TODO(nick): Replace this linear search with binary!
  if (dw_fde_iter_init(frame_info, frame_data, &it)) {
    DwFrameDescEntry fde;

    if (ip >= frame_info->image_base)
      ip -= frame_info->image_base;

    while (dw_fde_iter_next(&it, &fde)) {
      if (ip >= fde.start_ip && ip < (fde.start_ip + fde.range_ip)) {
        *cie_out = it.cie;
        *fde_out = fde;

        result = syms_true;
        break;
      }
    }
  }

  return result;
}

DW_API syms_bool
dw_virtual_unwind_init(SymsImageType image_type, DwVirtualUnwind *context_out)
{
  return dw_cfi_program_init(image_type, &context_out->program);
}

DW_API syms_bool
dw_virtual_unwind_frame(DwVirtualUnwind *context,
            SymsArch arch,
            DwVirtualUnwindDataType source_type,
            void *sec_bytes, SymsUMM sec_bytes_size,
            SymsAddr image_base,
            SymsAddr sec_bytes_base,
            void *memread_ctx, dw_memread_sig *memread,
            void *regread_ctx, dw_regread_sig *regread,
            void *regwrite_ctx, dw_regwrite_sig *regwrite)
{
  SymsUWord ip;
  SymsUWord sp;
  U32 i;

  DwUserCallbacks user_cbs;
  DwCommonInfoEntry cie;
  DwFrameDescEntry fde;
  DwBinRead cfi_data;
  DwBinRead frame_data;
  DwFrameInfo frame;

  frame.source_type = source_type;
  frame.eh_frame = sec_bytes_base;
  frame.image_base = image_base;
  switch (arch) {
  case SYMS_ARCH_X86: {
    frame.arch_info.mode      = DW_MODE_64BIT;
    frame.arch_info.arch      = arch;
    frame.arch_info.ip_regid  = DW_REG_X86_EIP; 
    frame.arch_info.sp_regid  = DW_REG_X86_ESP; 
    frame.arch_info.word_size = 4;
    frame.arch_info.word_size = 4;
  } break;

  case SYMS_ARCH_X64: {
    frame.arch_info.mode      = DW_MODE_32BIT;
    frame.arch_info.arch      = arch;
    frame.arch_info.ip_regid  = DW_REG_X64_RIP; 
    frame.arch_info.sp_regid  = DW_REG_X64_RSP; 
    frame.arch_info.word_size = 8;
    frame.arch_info.addr_size = 8;
  } break;

  default: {
    frame.arch_info.mode      = DW_MODE_NULL;
    frame.arch_info.arch      = SYMS_ARCH_NULL;
    frame.arch_info.ip_regid  = SYMS_UINT32_MAX;
    frame.arch_info.sp_regid  = SYMS_UINT32_MAX;
    frame.arch_info.word_size = 0;
  } break;
  }

  user_cbs.arch_info    = frame.arch_info;
  user_cbs.memread_ctx  = memread_ctx;
  user_cbs.memread      = memread;
  user_cbs.regread_ctx  = regread_ctx;
  user_cbs.regread      = regread;
  user_cbs.regwrite_ctx = regwrite_ctx;
  user_cbs.regwrite     = regwrite;
  
  ip = 0;
  if (!dw_regread_uword(&user_cbs, frame.arch_info.ip_regid, &ip)) {
    return syms_false;
  }

  sp = 0;
  if (!dw_regread_uword(&user_cbs, frame.arch_info.sp_regid, &sp)) {
    return syms_false;
  }

  frame_data = dw_bin_read_init(DW_MODE_32BIT, frame.arch_info.word_size, sec_bytes, sec_bytes_size);
  if (dw_bin_peek_u32(&frame_data) == 0xFFFFFFFF) {
    frame_data = dw_bin_read_init(DW_MODE_64BIT, frame.arch_info.word_size, sec_bytes, sec_bytes_size);
  }

  if (!dw_find_fde(&frame, frame_data, ip, &cie, &fde)) {
    return syms_false;
  }

  for (i = 0; i < SYMS_ARRAY_SIZE(context->program.rules.value); ++i) {
    context->program.rules.type[i] = DW_CFI_REGISTER_TYPE_SAME;
    context->program.rules.value[i].w = 0;
  }

  /* NOTE(nick): According to DWARF5.pdf instructions from CIE must be
   * executed first to init "state", and should be stored away for later 
   * use by opcodes like DW_CFA_REMEMBER_STATE and DW_CFA_RESTORE_STATE. */
  cfi_data = dw_bin_read_init(frame.arch_info.mode, frame.arch_info.addr_size, sec_bytes, cie.init_cfi_offset + cie.init_cfi_size);
  if (!dw_bin_seek(&cfi_data, cie.init_cfi_offset)) {
    // TODO(nick): handle corrupted data
    return syms_false;
  }
  if (!dw_compile_cfi_table_row(&context->program, &frame, &cie, NULL, &cfi_data, (SymsAddr)~0)) {
    return syms_false;
  }
  context->program.ret_addr_regid = cie.ret_addr_reg;


  if (context->program.setup_cfa) {
    context->program.setup_cfa = syms_false;
    context->program.cfa = sp;
  }

  /* NOTE(nick): Parse FDE instructions, these tell us how to unwind registers. */
  cfi_data = dw_bin_read_init(frame.arch_info.mode, frame.arch_info.addr_size, sec_bytes, fde.cfi_offset + fde.cfi_size);
  if (!dw_bin_seek(&cfi_data, fde.cfi_offset)) {
    SYMS_ASSERT_FAILURE("IMPLEMENT::ERROR");
    return syms_false;
  }
  if (!dw_compile_cfi_table_row(&context->program, &frame, &cie, &fde, &cfi_data, ip)) {
    SYMS_ASSERT_FAILURE("IMPLEMENT::ERROR");
    return syms_false;
  }

  /* NOTE(nick): Applying rules for unwinding to registers. */
  if (!dw_apply_cfi_table_row(&context->program, &frame, &user_cbs)) {
    /* SYMS_ASSERT_FAILURE("IMPLEMENT::ERROR"); */
    return syms_false;
  }

  return syms_true;
}
