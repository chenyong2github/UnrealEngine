// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_REGS_INCLUDE_H
#define SYMS_REGS_INCLUDE_H

/******************************************************************************
 * File   : syms_regs.h                                                       *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/10                                                        *
 * Purpose: Generic register interface that supports x86/x64                  *
 ******************************************************************************/

#ifndef REG_API
#define REG_API SYMS_API
#endif

typedef U32 SymsRegID;

typedef struct SymsRegDesc 
{
    // register name/alias
    char name[8];
    
    // register class
    U16 regclass;
    
    // register contained within a variable at this byte offset into the register file 
    U16 varOffset_le;
    
    // bit offset within the containing variable 
    U16 bitpos_le;
    
    // bit size within the containing variable 
    U16 bitcount_le;
} SymsRegDesc;
SYMS_COMPILER_ASSERT(sizeof(SymsRegDesc) == 16);

typedef struct SymsRegsPtr 
{
    U8 *data;
    U16 bytes;
} SymsRegsPtr;

typedef struct SymsRegs 
{
    U16 endiness;
    
    // SymsArch type
    U16 arch_le;
    
    // Regs version
    U16 version_le;
    
    // Lowest register
    U32 firstreg_le;
    
    // Highest register
    U32 lastreg_le;
    
    /* Total register file size */
    U32 size_le;
    
    SymsRegDesc descs[0];
} SymsRegs;

typedef struct
{
    SymsRegs *regs;
    
    U32 desc_count;
    SymsRegDesc *desc;
    
    U32 values_size;
    U8 *values;
} SymsRegsInfo;

#define SYMS_REG_ID_ARCH_MASK(x) ((U32)(x) >> 12)

syms_bool
syms_get_table_info(SymsArch arch, SymsRegDesc **desc_out, U32 *desc_size_out, U32 *regs_size_out, U32 *ver_out, U32 *regid_min_out, U32 *regid_max_out);

REG_API U32
syms_regs_size_for_arch(SymsArch arch);

REG_API SymsRegs *
syms_regs_init(SymsArch arch, void *bf, U32 bf_max);

REG_API SymsRegs *
syms_regs_clone(const SymsRegs *regs, void *buffer, U32 buffer_max);

// Returns the CPU architecture for a register file.
REG_API SymsArch 
syms_regs_get_arch(const SymsRegs *regs);

// Returns the size of a register file, in bytes.
REG_API U32 
syms_regs_get_size(const SymsRegs *regs);

// Returns the version number of a register file.
REG_API U32 
syms_regs_get_version(const SymsRegs *regs);

// Returns the lowest register ID for a file. 
REG_API SymsRegID 
syms_regs_get_first_regid(const SymsRegs *regs);

// Returns the highest register ID for a file.
REG_API SymsRegID 
syms_regs_get_last_regid(const SymsRegs *regs);

// Returns the register ID for the instruction pointer.
REG_API SymsRegID 
syms_regs_get_ip_regid(const SymsRegs *regs);

// Returns the register ID for the stack pointer.
REG_API SymsRegID
syms_regs_get_sp_regid(const SymsRegs *regs);

// Returns the description for a register file & index.
REG_API const SymsRegDesc *
syms_regs_get_regdesc(const SymsRegs *regs, SymsRegID id);

REG_API SymsRegID
syms_regid_from_name(const SymsRegs *regs, char *name);

REG_API U32 
syms_regs_get_value(const SymsRegs *regs, SymsRegID id, void *dst, U32 dst_size);

REG_API U32 
syms_regs_set_value(SymsRegs *regs, SymsRegID id, const void *src, U32 src_size);

REG_API void 
syms_regs_clear_reg(SymsRegs *regs, SymsRegID id);

REG_API U8  
syms_regs_get8(const SymsRegs *regs, SymsRegID id);

REG_API U16 
syms_regs_get16(const SymsRegs *regs, SymsRegID id);

REG_API U32 
syms_regs_get32(const SymsRegs *regs, SymsRegID id);

REG_API U64 
syms_regs_get64(const SymsRegs *regs, SymsRegID id);

REG_API syms_bool 
syms_regs_get_addr(const SymsRegs *regs, SymsRegID id, SymsAddr *addr_out);

REG_API syms_bool 
syms_regs_set8(SymsRegs *regs, SymsRegID id, U8 value);

REG_API syms_bool 
syms_regs_set16(SymsRegs *regs, SymsRegID id, U16 value);

REG_API syms_bool 
syms_regs_set32(SymsRegs *regs, SymsRegID id, U32 value);

REG_API syms_bool 
syms_regs_set64(SymsRegs *regs, SymsRegID id, U64 value);

REG_API syms_bool 
syms_regs_set_addr(SymsRegs *regs, SymsRegID id, SymsAddr value);

REG_API SymsAddr 
syms_regs_get_ip(const SymsRegs *regs);

REG_API SymsAddr 
syms_regs_get_sp(const SymsRegs *regs);

REG_API syms_bool 
syms_regs_set_ip(SymsRegs *regs, SymsAddr value);

REG_API syms_bool 
syms_regs_set_sp(SymsRegs *regs, SymsAddr value);

REG_API SymsEndian
syms_regs_get_endianess(const SymsRegs *regs);

#endif /* SYMS_REGS_INCLUDE_H */
