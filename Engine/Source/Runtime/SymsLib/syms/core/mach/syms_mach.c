// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_MACH_C
#define SYMS_MACH_C

////////////////////////////////
//~ NOTE(allen): Generated

#include "syms/core/generated/syms_meta_mach.c"

////////////////////////////////
//~ NOTE(allen): MACH Format Functions

SYMS_API void
syms_mach_fat_header_endian_swap_in_place(SYMS_MachFatHeader *x){
  x->magic     = syms_bswap_u32(x->magic);
  x->nfat_arch = syms_bswap_u32(x->nfat_arch);
}

SYMS_API void
syms_mach_fat_arch_endian_swap_in_place(SYMS_MachFatArch *x){
  x->cputype    = syms_bswap_u32(x->cputype);
  x->cpusubtype = syms_bswap_u32(x->cpusubtype);
  x->offset     = syms_bswap_u32(x->offset);
  x->size       = syms_bswap_u32(x->size);
  x->align      = syms_bswap_u32(x->align);
}

SYMS_API void
syms_mach_header32_endian_swap_in_place(SYMS_MachHeader32 *x){
  x->magic      = syms_bswap_u32(x->magic);
  x->cputype    = syms_bswap_u32(x->cputype);
  x->cpusubtype = syms_bswap_u32(x->cpusubtype);
  x->filetype   = syms_bswap_u32(x->filetype);
  x->ncmds      = syms_bswap_u32(x->ncmds);
  x->sizeofcmds = syms_bswap_u32(x->sizeofcmds);
  x->flags      = syms_bswap_u32(x->flags);
}

SYMS_API void
syms_mach_header64_endian_swap_in_place(SYMS_MachHeader64 *x){
  x->magic      = syms_bswap_u32(x->magic);
  x->cputype    = syms_bswap_u32(x->cputype);
  x->cpusubtype = syms_bswap_u32(x->cpusubtype);
  x->filetype   = syms_bswap_u32(x->filetype);
  x->ncmds      = syms_bswap_u32(x->ncmds);
  x->sizeofcmds = syms_bswap_u32(x->sizeofcmds);
  x->flags      = syms_bswap_u32(x->flags);
  x->reserved   = syms_bswap_u32(x->reserved);
}

SYMS_API void
syms_mach_segment_command32_endian_swap_in_place(SYMS_MachSegmentCommand32 *x){
  x->cmd.type = syms_bswap_u32(x->cmd.type);
  x->cmd.size = syms_bswap_u32(x->cmd.size);
  x->vmaddr   = syms_bswap_u32(x->vmaddr);
  x->vmsize   = syms_bswap_u32(x->vmsize);
  x->fileoff  = syms_bswap_u32(x->fileoff);
  x->filesize = syms_bswap_u32(x->filesize);
  x->maxprot  = syms_bswap_u32(x->maxprot);
  x->initprot = syms_bswap_u32(x->initprot);
  x->nsects   = syms_bswap_u32(x->nsects);
  x->flags    = syms_bswap_u32(x->flags);
}

SYMS_API void
syms_mach_segment_command64_endian_swap_in_place(SYMS_MachSegmentCommand64 *x){
  x->cmd.type = syms_bswap_u32(x->cmd.type);
  x->cmd.size = syms_bswap_u32(x->cmd.size);
  x->vmaddr   = syms_bswap_u64(x->vmaddr);
  x->vmsize   = syms_bswap_u64(x->vmsize);
  x->fileoff  = syms_bswap_u64(x->fileoff);
  x->filesize = syms_bswap_u64(x->filesize);
  x->maxprot  = syms_bswap_u32(x->maxprot);
  x->initprot = syms_bswap_u32(x->initprot);
  x->nsects   = syms_bswap_u32(x->nsects);
  x->flags    = syms_bswap_u32(x->flags);
}

SYMS_API void
syms_mach_section32_endian_swap_in_place(SYMS_MachSection32 *x){
  x->addr         = syms_bswap_u32(x->addr);
  x->size         = syms_bswap_u32(x->size);
  x->offset       = syms_bswap_u32(x->offset);
  x->align        = syms_bswap_u32(x->align);
  x->relocoff     = syms_bswap_u32(x->relocoff);
  x->nreloc       = syms_bswap_u32(x->nreloc);
  x->flags        = syms_bswap_u32(x->flags);
  x->reserved1    = syms_bswap_u32(x->reserved1);
  x->reserved2    = syms_bswap_u32(x->reserved2);
}

SYMS_API void
syms_mach_section64_endian_swap_in_place(SYMS_MachSection64 *x){
  x->addr         = syms_bswap_u64(x->addr);
  x->size         = syms_bswap_u64(x->size);
  x->offset       = syms_bswap_u32(x->offset);
  x->align        = syms_bswap_u32(x->align);
  x->relocoff     = syms_bswap_u32(x->relocoff);
  x->nreloc       = syms_bswap_u32(x->nreloc);
  x->flags        = syms_bswap_u32(x->flags);
  x->reserved1    = syms_bswap_u32(x->reserved1);
  x->reserved2    = syms_bswap_u32(x->reserved2);
}

SYMS_API void
syms_mach_header64_from_header32(SYMS_MachHeader64 *dst, SYMS_MachHeader32 *header32){
  dst->magic = header32->magic;
  dst->cputype = header32->cputype;
  dst->cpusubtype = header32->cpusubtype;
  dst->filetype = header32->filetype;
  dst->ncmds = header32->ncmds;
  dst->sizeofcmds = header32->sizeofcmds;
  dst->flags = header32->flags;
  dst->reserved = 0;
}

SYMS_API void
syms_mach_nlist64_from_nlist32(SYMS_MachNList64 *dst, SYMS_MachNList32 *nlist32){
  dst->n_strx = nlist32->n_strx;
  dst->n_type = nlist32->n_type;
  dst->n_sect = nlist32->n_sect;
  dst->n_desc = nlist32->n_desc;
  dst->n_value = nlist32->n_value;
}

SYMS_API SYMS_Arch
syms_mach_arch_from_cputype(SYMS_MachCpuType cputype){
  SYMS_Arch result = SYMS_Arch_Null;
  switch (cputype){
    case SYMS_MachCpuType_X86:    result = SYMS_Arch_X86;   break;
    case SYMS_MachCpuType_X86_64: result = SYMS_Arch_X64;   break;
    case SYMS_MachCpuType_ARM:    result = SYMS_Arch_ARM32; break;
    case SYMS_MachCpuType_ARM64:  result = SYMS_Arch_ARM;   break;
  }
  return(result);
}

#endif // SYMS_MACH_C
