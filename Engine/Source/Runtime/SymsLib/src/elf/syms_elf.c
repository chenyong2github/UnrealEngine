// Copyright Epic Games, Inc. All Rights Reserved.
/******************************************************************************
 * File   : syms_elf.c                                                        *
 * Author : Nikita Smith                                                      *
 * Created: 2020/05/30                                                        *
 * Purpose: Implementation of routines for dealing with ELF format            *
 ******************************************************************************/

#include "syms_elf.h"

SYMS_COMPILER_ASSERT(sizeof(SymsSymtabIter) <= sizeof(SymsModImpl));

SYMS_API syms_bool
syms_img_init_elf(SymsImage *img, void *img_data, SymsUWord img_size, SymsLoadImageFlags flags)
{
  SymsImageElf *img_elf = (SymsImageElf *)img->impl;
  SymsBuffer img_read = syms_buffer_init(img_data, img_size);
  U8 sig[SYMS_EI_NIDENT];
  u16 e_machine = SYMS_EM_NONE;
  u32 i;
  u32 phdr_num;
  SymsAddr shstr_off;
  SymsAddr base_addr = 0;
  SymsAddr prev_delta;

  if (!syms_buffer_read(&img_read, sig, sizeof(sig)))
    return syms_false;
  if (sig[SYMS_EI_MAG0] != 0x7f || 
      sig[SYMS_EI_MAG1] != 'E'  ||
      sig[SYMS_EI_MAG2] != 'L'  || 
      sig[SYMS_EI_MAG3] != 'F')
    return syms_false; // signature mismatch, data is not of ELF format


  // seek back to origin, so we can read entire SymsElf32/SymsElf64 struct
  syms_buffer_seek(&img_read, 0);
  switch (sig[SYMS_EI_CLASS]) {
  case SYMS_ELFCLASS32: {
    SymsElf32 *elf = syms_buffer_push_struct(&img_read, SymsElf32);
    SymsElfShdr32 *shstr;

    if (!elf)
      break;
    // seek to section header that contains strings
    shstr_off = elf->e_shoff + elf->e_shentsize*elf->e_shstrndx;
    if (!syms_buffer_seek(&img_read, shstr_off))
      break;
    // read string section header
    shstr = syms_buffer_push_struct(&img_read, SymsElfShdr32);
    if (!shstr)
      break;
    if (shstr->sh_type == SYMS_SHT_NOBITS)
      break;

    // save base address of program-header names
    img_elf->sh_name_lo = (flags & SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY) ? shstr->sh_addr : shstr->sh_offset;
    img_elf->sh_name_hi = img_elf->sh_name_lo + shstr->sh_size;

    // seek to first program header
    if (!syms_buffer_seek(&img_read, elf->e_phoff))
      break;
    // search for base address, which is stored in program header that is closest to entry point
    phdr_num = elf->e_phnum != SYMS_UINT16_MAX ? elf->e_phnum : shstr->sh_info;
    prev_delta = SYMS_ADDR_MAX;
    for (i = 0; i < phdr_num; ++i) {
      SymsElfPhdr64 *phdr = syms_buffer_push_struct(&img_read, SymsElfPhdr64);
      if (!phdr)
        break;
      if (phdr->p_type == SYMS_PT_LOAD) {
        if (elf->e_entry >= phdr->p_vaddr) {
          SymsAddr delta = elf->e_entry - phdr->p_vaddr;
          if (delta < prev_delta) {
            base_addr = phdr->p_vaddr;
            prev_delta = delta;
          }
        }
      }
    }

    if (elf->e_shoff >= img_size)
      break;

    e_machine = elf->e_machine;

    img->type             = SYMS_IMAGE_ELF;
    img->header_class     = SYMS_IMAGE_HEADER_CLASS_32;
    img_elf->u.header32 = elf;
  } break;
  case SYMS_ELFCLASS64: {
    SymsElf64 *elf = syms_buffer_push_struct(&img_read, SymsElf64);
    SymsElfShdr64 *shstr;

    if (!elf)
      break;
    // seek to .shstrtab
    shstr_off = elf->e_shoff + elf->e_shentsize * elf->e_shstrndx;
    if (!syms_buffer_seek(&img_read, shstr_off))
      break;
    // read string section header
    shstr = syms_buffer_push_struct(&img_read, SymsElfShdr64);
    if (shstr) {
      if (shstr->sh_type == SYMS_SHT_NOBITS)
        break;

      // save base address of program-header names
      img_elf->sh_name_lo = (flags & SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY) ? shstr->sh_addr : shstr->sh_offset;
      img_elf->sh_name_hi = img_elf->sh_name_lo + shstr->sh_size;
    } else {
      img_elf->sh_name_lo = 0;
      img_elf->sh_name_hi = 0;
    }

    // seek to first program header
    if (!syms_buffer_seek(&img_read, elf->e_phoff))
      break;
    // search for base address, which is stored in program header that is closest to entry point
    phdr_num = elf->e_phnum != SYMS_UINT16_MAX ? elf->e_phnum : shstr->sh_info;
    prev_delta = SYMS_ADDR_MAX;
    for (i = 0; i < phdr_num; ++i) {
      SymsElfPhdr64 *phdr = syms_buffer_push_struct(&img_read, SymsElfPhdr64);
      if (!phdr)
        break;
      if (phdr->p_type == SYMS_PT_LOAD) {
        if (elf->e_entry >= phdr->p_vaddr) {
          SymsAddr delta = elf->e_entry - phdr->p_vaddr;
          if (delta < prev_delta) {
            base_addr = phdr->p_vaddr;
            prev_delta = delta;
          }
        }
      }
    }

    if (elf->e_shoff >= img_size)
      break;

    e_machine = elf->e_machine;

    img->type             = SYMS_IMAGE_ELF;
    img->header_class     = SYMS_IMAGE_HEADER_CLASS_64;
    img_elf->u.header64 = elf;
  } break;
  }

  if (img->type == SYMS_IMAGE_ELF) {
    img->base_addr = base_addr;

    // convert e_machine to SymsArch
    switch (e_machine) {
    default: SYMS_ASSERT_FAILURE_PARANOID("undefined e_machine");
    case SYMS_EM_AARCH64: img->arch = SYMS_ARCH_ARM;   break;
    case SYMS_EM_ARM:     img->arch = SYMS_ARCH_ARM32; break;
    case SYMS_EM_386:     img->arch = SYMS_ARCH_X86;   break;
    case SYMS_EM_X86_64:  img->arch = SYMS_ARCH_X64;   break;
    case SYMS_EM_PPC:     img->arch = SYMS_ARCH_PPC;   break;
    case SYMS_EM_PPC64:   img->arch = SYMS_ARCH_PPC64; break;
    case SYMS_EM_IA_64:   img->arch = SYMS_ARCH_IA64;  break;
    }
  }
  
  return (img->type == SYMS_IMAGE_ELF);
}

SYMS_INTERNAL SymsSecIterElf
syms_sec_iter_init_elf(SymsImage *image)
{
  SymsImageElf *elf = (SymsImageElf *)image->impl;
  SymsSecIterElf result;
  SymsAddr secs_lo = 0;
  SymsAddr secs_hi = 0;

  result.image = image;
  result.sh_name_lo = elf->sh_name_lo;
  result.sh_name_hi = elf->sh_name_hi;
  result.header_index = 0;
  result.header_count = 0;

  switch (image->header_class) {
  case SYMS_IMAGE_HEADER_CLASS_NULL: {
    syms_memzero(&result, sizeof(result));
  } break;
  case SYMS_IMAGE_HEADER_CLASS_32: {
    secs_lo = elf->u.header32->e_shoff + elf->u.header32->e_shentsize;
    secs_hi = secs_lo + elf->u.header32->e_shnum*sizeof(SymsElfShdr32);
    result.header_count = elf->u.header32->e_shnum;
  } break;
  case SYMS_IMAGE_HEADER_CLASS_64: {
    secs_lo = elf->u.header64->e_shoff;
    secs_hi = secs_lo + elf->u.header64->e_shnum*sizeof(SymsElfShdr64);
    result.header_count = elf->u.header64->e_shnum;
  } break;
  }

  void *sec_data = (U8 *)image->img_data + secs_lo;
  result.headers = syms_buffer_init(sec_data, image->img_data_size - secs_lo);

  return result;
}

SYMS_INTERNAL syms_bool
syms_sec_iter_next_elf(SymsSecIterElf *iter, SymsElfShdr64 *sec_out)
{
  SymsImage *image = iter->image;
  syms_bool result = syms_false;

  switch (image->header_class) {
  case SYMS_IMAGE_HEADER_CLASS_NULL: {
    syms_memzero(sec_out, sizeof(*sec_out));
  } break;
  case SYMS_IMAGE_HEADER_CLASS_32: {
    // convert header to 64bit
    SymsElfShdr32 *elf32 = syms_buffer_push_struct(&iter->headers, SymsElfShdr32);
    if (elf32) {
      sec_out->sh_name      = elf32->sh_name;
      sec_out->sh_type      = elf32->sh_type;
      sec_out->sh_flags     = elf32->sh_flags;
      sec_out->sh_addr      = elf32->sh_addr;
      sec_out->sh_offset    = elf32->sh_offset;
      sec_out->sh_size      = elf32->sh_size;
      sec_out->sh_link      = elf32->sh_link;
      sec_out->sh_info      = elf32->sh_info;
      sec_out->sh_addralign = elf32->sh_addralign;
      sec_out->sh_entsize   = elf32->sh_entsize;
      result = syms_true;
    }
  } break;
  case SYMS_IMAGE_HEADER_CLASS_64: {
    SymsElfShdr64 *elf64 = syms_buffer_push_struct(&iter->headers, SymsElfShdr64);
    if (elf64) {
      *sec_out = *elf64;
      result = syms_true;
    }
  } break;
  default: break;
  } 
#if 0
  { 
    sec_out->name = syms_string_init_lit("");
    if (header.sh_name < iter->sh_name_lo && iter->sh_name_lo + header.sh_name < img->img_data_size) {
      SymsBuffer buffer = syms_buffer_init(img->img_data, img->img_data_size);
      if (syms_buffer_seek(&buffer, iter->sh_name_lo + header.sh_name)) {
        sec_out->name = syms_buffer_read_string(&buffer);
      }
    }

    sec_out->data_size = header.sh_size;
    sec_out->off = header.sh_offset;
    sec_out->va = header.sh_addr;
    sec_out->u.elf = header;

    // check if section has data in file
    sec_out->data = 0;
    if (header.sh_type != SYMS_SHT_NOBITS) {
      SymsAddr data_lo = (img->flags & SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY) ? header.sh_addr : header.sh_offset;
      SymsAddr data_hi = data_lo + header.sh_size;
      if (data_hi <= img->img_data_size) {
        sec_out->data = (void *)((U8 *)img->img_data + data_lo);
      }
    }

    // advance to next section
    iter->header_index += 1;

    result = syms_true;
  }
#endif

  return result;
}

ELF_API syms_bool
syms_symtab_iter_init(struct SymsInstance *instance, SymsSymtabIter *iter)
{
  syms_bool result = syms_false;

  SymsSection symtab;
  SymsSection strtab;

  syms_bool symtab_found = syms_img_sec_from_name(instance, syms_string_lit(".symtab"), &symtab);
  syms_bool strtab_found = syms_img_sec_from_name(instance, syms_string_lit(".strtab"), &strtab);

  syms_memzero(iter, sizeof(*iter));

  if (symtab_found && strtab_found) {
    SymsImage *img = &instance->img;
    if (img->type == SYMS_IMAGE_ELF) {
      switch (img->header_class) {
      case SYMS_IMAGE_HEADER_CLASS_NULL: iter->count = 0; break;
      case SYMS_IMAGE_HEADER_CLASS_32: iter->count = syms_trunc_u32(symtab.data_size / sizeof(SymsElf32Sym)); break;
      case SYMS_IMAGE_HEADER_CLASS_64: iter->count = syms_trunc_u32(symtab.data_size / sizeof(SymsElf64Sym)); break;
      }
      iter->symtab_cursor = syms_buffer_init(symtab.data, symtab.data_size);
      iter->strtab_cursor = syms_buffer_init(strtab.data, strtab.data_size);
      iter->header_class = img->header_class;
    }
    result = syms_true;
  }

  return result;
}

ELF_API syms_bool
syms_symtab_iter_next(SymsSymtabIter *iter, SymsSymtabEntry *entry_out)
{
  syms_bool result = syms_false;
  switch (iter->header_class) {
  case SYMS_IMAGE_HEADER_CLASS_NULL: break;
  case SYMS_IMAGE_HEADER_CLASS_32: break;
  case SYMS_IMAGE_HEADER_CLASS_64: {
    SymsElf64Sym *sym = syms_buffer_push_struct(&iter->symtab_cursor, SymsElf64Sym);
    if (sym) {
      if (syms_buffer_seek(&iter->strtab_cursor, sym->st_name)) {
        entry_out->name = syms_buffer_read_string(&iter->strtab_cursor);
        entry_out->bind = SYMS_ELF64_ST_BIND(sym->st_info);
        entry_out->type = SYMS_ELF64_ST_TYPE(sym->st_info);
        entry_out->vis = SYMS_ELF64_ST_VISIBILITY(sym->st_info);
        entry_out->index = sym->st_shndx;
        entry_out->value = sym->st_value;
        entry_out->size = sym->st_size;
      } else {
        entry_out->name = syms_string_init_lit("");
        entry_out->bind = SYMS_STB_LOCAL;
        entry_out->type = 0;
        entry_out->vis = SYMS_STV_DEFAULT;
        entry_out->index = sym->st_shndx;
        entry_out->value = 0;
        entry_out->size = 0;
      }
      result = syms_true;
    }
  } break;
  }
  return result;
}

SYMS_INTERNAL SymsProc
syms_proc_from_stt_func(SymsSymtabEntry *stt_func)
{
  SymsProc result;
  SYMS_ASSERT(stt_func->type == SYMS_STT_FUNC);

  result.type_id = syms_type_id_null();
  result.va = stt_func->value;
  result.len = syms_trunc_u32(stt_func->size);
  result.dbg_start_va = result.va;
  result.dbg_end_va = result.va + result.len;
  result.name_ref.type = SYMS_STRING_REF_TYPE_STR;
  result.name_ref = syms_string_ref_str(stt_func->name); //.u.str = stt_func->name;

  return result;
}

