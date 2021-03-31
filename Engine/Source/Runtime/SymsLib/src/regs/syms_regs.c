// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_regs.c                                                       *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/10                                                        *
 * Purpose: Generic register interface that supports x86/x64 and ARM/ARM64    *
 ******************************************************************************/

#include "syms_regs_data.h"

// NOTE(PM): We don't try to match any particular OS/CPU struct layout here, we define our own.
// We store all the bytes individually. bytes[0] is the MSB, bytes[N-1] is the LSB. (always big endian)
//

#if 0
SYMS_INTERNAL const SymsRegDesc *
syms_regs_get_regdesc(SymsArch arch)
{
    SymsRegDesc *result = 0;
    if (arch < SYMS_ARCH_COUNT)
    {
        result = g_syms_reg_desc[arch];
    }
    return result;
}

SYMS_INTERNAL U32
syms_regs_get_desc_size(SymsArch arch)
{
    U32 result = 0;
    if (arch < SYMS_ARCH_COUNT)
    {
        result = g_syms_reg_desc_size[arch];
    }
    return result;
}
#endif

SYMS_API SymsRegID
syms_regs_get_first_regid_ex(SymsArch arch)
{
    syms_uint result = 0;
    if (arch < SYMS_ARCH_COUNT)
    {
        result = 1;
    }
    return result;
}

SYMS_API SymsRegID
syms_regs_get_last_regid_ex(SymsArch arch)
{
    syms_uint result = 0;
    if (arch < SYMS_ARCH_COUNT)
    {
        result = g_syms_reg_desc_count[arch] - 1;
    }
    return result;
}

SYMS_API SymsString
syms_regs_get_name_ex(SymsArch arch, SymsRegID id)
{
  const SymsRegInfo *reg_table = reg_table_from_arch(arch);
  syms_uint reg_count = g_syms_reg_desc_count[arch];
  SymsString name = syms_string_init(0,0);
  if (id < reg_count) {
    name = reg_table[id].name;
  }
  return name;
}

SYMS_API SymsTypeKind
syms_regs_get_type_ex(SymsArch arch, SymsRegID id)
{
  const SymsRegInfo *reg_table = reg_table_from_arch(arch);
  syms_uint reg_count = g_syms_reg_desc_count[arch];
  SymsTypeKind kind = SYMS_TYPE_NULL;
  if (id < reg_count) {
    kind = reg_table[id].kind;
  }
  return kind;
}

SYMS_API SymsRegClass
syms_regs_get_reg_class_ex(SymsArch arch, SymsRegID id)
{
  const SymsRegInfo *reg_table = reg_table_from_arch(arch);
  syms_uint reg_count = g_syms_reg_desc_count[arch];
  SymsRegClass reg_class = SYMS_REG_CLASS_NULL;
  if (id < reg_count) {
    reg_class = reg_table[id].group;
  }
  return reg_class;
}

SYMS_API SymsRegID
syms_regs_get_ip_ex(SymsArch arch)
{
  switch (arch) {
  case SYMS_ARCH_X86: return SYMS_REG_X86_eip;
  case SYMS_ARCH_X64: return SYMS_REG_X64_rip;
  default:            return SYMS_REG_null;
  }
}

SYMS_API SymsRegID
syms_regs_get_sp_ex(SymsArch arch)
{
  switch (arch) {
  case SYMS_ARCH_X86: return SYMS_REG_X86_esp;
  case SYMS_ARCH_X64: return SYMS_REG_X64_rsp;
  default:            return SYMS_REG_null;
  }
}

SYMS_API SymsRegAliasInfo
syms_regs_get_alias_info(SymsArch arch, SymsRegID id)
{
  const SymsRegInfo *reg_table = reg_table_from_arch(arch);
  syms_uint reg_count = g_syms_reg_desc_count[arch];
  SymsRegAliasInfo result;
  if (id < reg_count) {
    result.aliasee = reg_table[id].alias;
    result.bit_shift = reg_table[id].bit_shift;
    result.bit_count = reg_table[id].bit_count;
  } else {
    result.aliasee = 0;
    result.bit_shift = 0;
    result.bit_count = 0;
  }
  return result;
}

SYMS_INTERNAL U32
syms_regs_get_regsdata_size(SymsArch arch)
{
    U32 result = 0;
    if (arch < SYMS_ARCH_COUNT)
    {
        result = g_syms_reg_data_size[arch];
    }
    return result;
}

REG_API SymsRegs * 
syms_regs_init(SymsArch arch, void *bf, U32 bf_size)
{
    syms_uint size = syms_regs_size_for_arch(arch);
    SymsRegs *regs = 0;
    if (size > 0 && size <= bf_size) {
        syms_uint descs_size        = reg_table_size_from_arch(arch);
        syms_uint data_size         = syms_regs_get_regsdata_size(arch);
        syms_uint reg_info_count    = g_syms_reg_desc_count[arch];
        const SymsRegInfo *reg_info = reg_table_from_arch(arch);
        syms_uint i;
        
        regs = (SymsRegs *)bf;
        regs->endiness = 0x1FFF;
        regs->arch_le = SYMS_LE16((u16)arch);
        
        regs->version_le    = SYMS_LE16((U16)1);
        regs->firstreg_le   = SYMS_LE32((U32)syms_regs_get_first_regid_ex(arch));
        regs->lastreg_le    = SYMS_LE32((U32)syms_regs_get_last_regid_ex(arch));
        regs->size_le       = SYMS_LE32((U32)size);

        for (i = 0; i < reg_info_count; ++i) {
          const SymsRegInfo *r = reg_info + i;
          SymsRegDesc *r_out   = regs->descs + i;

          SYMS_ASSERT(r->name.len <= sizeof(regs->descs[i].name));
          syms_memcpy(regs->descs[i].name, r->name.data, r->name.len);
          r_out->regclass     = SYMS_LE16((u16)r->group);
          r_out->varOffset_le = SYMS_LE32((u16)r->offset);
          r_out->bitpos_le    = SYMS_LE16((u16)r->bit_shift);
          r_out->bitcount_le  = SYMS_LE16((u16)r->bit_count);
        }

        syms_memset(((U8*)&regs->descs[0]) + descs_size, 0, data_size);
    }
    
    return regs;
}

REG_API U32
syms_regs_size_for_arch(SymsArch arch)
{
    U32 result = 0;
    if (arch < SYMS_ARCH_COUNT) {
        result = sizeof(SymsRegs) + syms_regs_get_regsdata_size(arch) + reg_table_size_from_arch(arch);
    }
    return result;
}

REG_API SymsRegs *
syms_regs_clone(const SymsRegs *regs, void *buffer, U32 buffer_max)
{
    U32 size = syms_regs_get_size(regs);
    SymsRegs *result = 0;
    if (size <= buffer_max) {
        result = (SymsRegs *)syms_memcpy(buffer, regs, size);
    }
    return result;
}

REG_API SymsArch 
syms_regs_get_arch(const SymsRegs *regs)
{
    SymsArch result = SYMS_ARCH_COUNT;
    if (regs) {
        result = (SymsArch)SYMS_LE16(regs->arch_le);
    }
    return result;
}

REG_API const SymsRegDesc *
syms_regs_get_regdesc(const SymsRegs *regs, SymsRegID id)
{
    const SymsRegDesc *result = 0;
    SymsRegID first_reg = syms_regs_get_first_regid(regs);
    SymsRegID last_reg = syms_regs_get_last_regid(regs);
    if (first_reg <= id && id <= last_reg) {
        result = regs->descs + ((U16)id - (U16)first_reg);
    }
    return result;
}

REG_API U32 
syms_regs_get_size(const SymsRegs *regs)
{
    return SYMS_LE32(regs->size_le);
}

REG_API U32 
syms_regs_get_version(const SymsRegs *regs)
{
    return SYMS_LE32(regs->version_le);
}

REG_API SymsRegID 
syms_regs_get_first_regid(const SymsRegs *regs)
{
    return (SymsRegID)SYMS_LE32(regs->firstreg_le);
}

REG_API SymsRegID 
syms_regs_get_last_regid(const SymsRegs *regs)
{
    return (SymsRegID)SYMS_LE32(regs->lastreg_le);
}

REG_API SymsRegID 
syms_regs_get_ip_regid(const SymsRegs *regs)
{
    switch (syms_regs_get_arch(regs)) {
        case SYMS_ARCH_X86: return SYMS_REG_X86_eip;
        case SYMS_ARCH_X64: return SYMS_REG_X64_rip;
        default:            return 0;
    }
}

REG_API SymsRegID 
syms_regs_get_sp_regid(const SymsRegs *regs)
{
    switch (syms_regs_get_arch(regs)) {
        case SYMS_ARCH_X86: return SYMS_REG_X86_esp;
        case SYMS_ARCH_X64: return SYMS_REG_X64_rsp;
        default:            return 0;
    }
}

SYMS_INTERNAL SymsRegsPtr 
syms_regs_query(const SymsRegs *regs, SymsRegID id)
{
    SymsRegsPtr result;
    
    // HACK: The X87 FPU handles ST/MMX registers in a really annoying way.
    // They're overlaid, but it's not a static mapping; it changes
    // based on the stack TOP pointer. Note that lldb gets this wrong, while
    // gdb gets it right. We hack it by never using the st0 data directly,
    // we redirect it into the appropriate fpr register.
    {
        SymsArch arch = syms_regs_get_arch(regs);
        
        SymsRegID reg_st0 = 0;
        SymsRegID reg_fsw = 0;
        SymsRegID reg_fpr0 = 0;
        switch (arch) {
            default: break;
            
            case SYMS_ARCH_X64: {
                reg_st0 = SYMS_REG_X64_st0;
                if (id >= reg_st0 && id <= SYMS_REG_X64_st7) {
                    reg_fsw = SYMS_REG_X64_fsw;
                    reg_fpr0 = SYMS_REG_X64_fpr0;
                    goto X86_64;
                }
            } break;
            
            case SYMS_ARCH_X86: {
                reg_st0 = SYMS_REG_X86_st0;
                if (id >= reg_st0 && id <= SYMS_REG_X86_st7) {
                    reg_fsw = SYMS_REG_X86_fsw;
                    reg_fpr0 = SYMS_REG_X86_fpr0;
                    goto X86_64;
                }
            } break;
            
            X86_64:;
            {
                U16 fsw = syms_regs_get16(regs, reg_fsw);
                U16 st_index = (U16)id - (U16)reg_st0;
                U32 fpr_index = (st_index + (fsw >> 11)) & 7;
                id = (SymsRegID)(reg_fpr0 + fpr_index);
            }break;
        }
    }
    
    {
        const SymsRegDesc *regdesc = syms_regs_get_regdesc(regs, id);
        if (regdesc != NULL) {
            SymsRegID first_reg = syms_regs_get_first_regid(regs);
            SymsRegID last_reg = syms_regs_get_last_regid(regs);
            U32 regdesc_count = (last_reg - first_reg) + 1;
            U32 regdesc_size = sizeof(SymsRegDesc) * regdesc_count;
            U8 *ptr;
            
            SYMS_ASSERT((regdesc->bitpos_le & 7) == 0); // we don't support non-8bit fields
            SYMS_ASSERT((regdesc->bitcount_le & 7) == 0);
            
            ptr = (U8 *)regs->descs + regdesc_size;
            result.data = ptr + regdesc->varOffset_le + regdesc->bitpos_le / 8;
            result.bytes = regdesc->bitcount_le / 8;
        } else {
            result.data = 0;
            result.bytes = 0;
        }
    }
    
    return result;
}

REG_API SymsRegID
syms_regid_from_name(const SymsRegs *regs, char *name)
{
    SymsArch arch = syms_regs_get_arch(regs);
    if (arch < SYMS_ARCH_COUNT) {
        // TODO(nick): A hash table to speed this up?
        const SymsRegInfo *info_table = reg_table_from_arch(arch);
        SymsRegID regid_first = syms_regs_get_first_regid(regs);
        SymsRegID regid_last = syms_regs_get_last_regid(regs);
        U32 count = (U32)regid_last - (U32)regid_first + 1;
        U32 i;
        for (i = 0; i < count; ++i) {
          if (syms_strcmp(info_table[i].name.data, name) == 0) {
            return (SymsRegID)((U32)regid_first + i);
          }
        }
    }
    return 0;
}

SYMS_INTERNAL void 
syms_regs_memcpy_swap(U8 *dst, const U8 *src, U32 n)
{
    while (n--) {
        *dst++ = src[n];
    }
}

REG_API SymsEndian
syms_regs_get_endianess(const SymsRegs *regs)
{
    if (((U8 *)&regs->endiness)[0] == 0xFF) {
        return SYMS_ENDIAN_LITTLE;
    } else {
        return SYMS_ENDIAN_BIG;
    }
}

REG_API void 
syms_regs_clear_reg(SymsRegs *regs, SymsRegID id)
{
    SymsRegsPtr dst = syms_regs_query(regs, id);
    syms_memset((void *)dst.data, 0, dst.bytes);   // Zero the register out.
}

REG_API U32 
syms_regs_get_value(const SymsRegs *regs, SymsRegID id, void *dst, U32 dst_size)
{
    U32 written = 0;
    SymsRegsPtr src = syms_regs_query(regs, id);
    if (src.bytes <= dst_size) {
        if (syms_regs_get_endianess(regs) == SYMS_ENDIAN_HOST) {
            syms_memcpy(dst, src.data, src.bytes);
        } else {
            syms_regs_memcpy_swap((U8 *)dst, (const U8 *)src.data, src.bytes);
        }
        written = src.bytes;
    }
    return written;
}

REG_API U8 
syms_regs_get8(const SymsRegs *regs, SymsRegID id)
{
    U8 n = 0;
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 1);
    syms_regs_get_value(regs, id, &n, sizeof(n));
    return n;
}

REG_API U16 
syms_regs_get16(const SymsRegs *regs, SymsRegID id)
{
    U16 n = 0;
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 2);
    syms_regs_get_value(regs, id, &n, sizeof(n));
    return n;
}

REG_API U32 
syms_regs_get32(const SymsRegs *regs, SymsRegID id)
{
    U32 n = 0;
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 4);
    syms_regs_get_value(regs, id, &n, sizeof(n));
    return n;
}

REG_API U64 
syms_regs_get64(const SymsRegs *regs, SymsRegID id)
{
    U64 n = 0;
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 8);
    syms_regs_get_value(regs, id, &n, sizeof(n));
    return n;
}

REG_API syms_bool 
syms_regs_get_addr(const SymsRegs *regs, SymsRegID id, SymsAddr *addr_out)
{
    syms_bool is_result_valid = syms_false;
    U32 bytes = syms_regs_query(regs, id).bytes;
    is_result_valid = syms_true;
    switch (bytes) {
        case 1: *addr_out = syms_regs_get8(regs, id);  break;
        case 2: *addr_out = syms_regs_get16(regs, id); break;
        case 4: *addr_out = syms_regs_get32(regs, id); break;
        case 8: *addr_out = syms_regs_get64(regs, id); break;
        default: is_result_valid = syms_false;              break;
    }
    return is_result_valid;
}

REG_API SymsAddr 
syms_regs_get_ip(const SymsRegs *regs)
{
    switch (syms_regs_get_arch(regs)) {
        // TODO(allen): This doesn't make sense. Some OSes can't deal with changes
        // to the CS register, so we're going to be broken for all OSes instead of
        // just the ones that have to be??? We're changing this.
        
        // TODO: EIP/RIP isn't actually the instruction pointer. (really!)
        //       It's an *offset* into the code segment.
        //       The actual address of the running code would be CS:EIP.
        //       (i.e. csbase+eip)
        //       We don't handle this though, as it's *incredibly* rare (and
        //       not even possible on some OSes) to change the CS segment base
        //       to something non-zero. Many OSes can't even return the csbase
        //       via their debugger API.
        case SYMS_ARCH_X86: return syms_regs_get32(regs, SYMS_REG_X86_eip);
        case SYMS_ARCH_X64: return syms_regs_get64(regs, SYMS_REG_X64_rip);
        default:            return 0;
    }
}

REG_API SymsAddr 
syms_regs_get_sp(const SymsRegs *regs)
{
    switch (syms_regs_get_arch(regs)) {
        // TODO: should be SS:EIP (ssbase+eip), same as above
        case SYMS_ARCH_X86: return syms_regs_get32(regs, SYMS_REG_X86_esp);
        case SYMS_ARCH_X64: return syms_regs_get64(regs, SYMS_REG_X64_rsp);
        default:        return 0;
    }
}

REG_API U32 
syms_regs_set_value(SymsRegs *regs, SymsRegID id, const void *src, U32 src_size)
{
    U32 written = 0;
    SymsRegsPtr dst = syms_regs_query(regs, id);
    if (src_size > 0 && src_size <= dst.bytes) {
        if (syms_regs_get_endianess(regs) == SYMS_ENDIAN_HOST) {
            syms_memcpy(dst.data, src, src_size);
        } else {
            syms_regs_memcpy_swap((U8 *)dst.data, (const U8 *)src, src_size);
        }
        written = dst.bytes;
    }
    return written;
}

REG_API syms_bool
syms_regs_set8(SymsRegs *regs, SymsRegID id, U8 value)
{
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 1);
    return syms_regs_set_value(regs, id, &value, sizeof(value));
}

REG_API syms_bool 
syms_regs_set16(SymsRegs *regs, SymsRegID id, U16 value)
{
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 2);
    return syms_regs_set_value(regs, id, &value, sizeof(value));
}

REG_API syms_bool 
syms_regs_set32(SymsRegs *regs, SymsRegID id, U32 value)
{
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 4);
    return syms_regs_set_value(regs, id, &value, sizeof(value));
}

REG_API syms_bool 
syms_regs_set64(SymsRegs *regs, SymsRegID id, U64 value)
{
    SYMS_ASSERT(syms_regs_query(regs, id).bytes == 8);
    return syms_regs_set_value(regs, id, &value, sizeof(value));
}

REG_API syms_bool 
syms_regs_set_addr(SymsRegs *regs, SymsRegID id, SymsAddr value)
{
    U32 bytes = syms_regs_query(regs, id).bytes;
    switch (bytes) {
        case 1:     return syms_regs_set8(regs, id, (U8)value);
        case 2:     return syms_regs_set16(regs, id, (U16)value);
        case 4:     return syms_regs_set32(regs, id, (U32)value);
        case 8:     return syms_regs_set64(regs, id, (U64)value);
        default:    return syms_false;
    }
}

REG_API syms_bool
syms_regs_set_ip(SymsRegs *regs, SymsAddr value)
{
    switch (syms_regs_get_arch(regs)) {
        case SYMS_ARCH_X86: return syms_regs_set32(regs, SYMS_REG_X86_eip, (U32)value);
        case SYMS_ARCH_X64: return syms_regs_set64(regs, SYMS_REG_X64_rip, (U64)value);
        default:            return syms_false;
    }
}

REG_API syms_bool
syms_regs_set_sp(SymsRegs *regs, SymsAddr value)
{
    switch (syms_regs_get_arch(regs)) {
        case SYMS_ARCH_X86: return syms_regs_set32(regs, SYMS_REG_X86_esp, (U32)value);
        case SYMS_ARCH_X64: return syms_regs_set64(regs, SYMS_REG_X64_rsp, (U64)value);
        default:        return syms_false;
    }
}
