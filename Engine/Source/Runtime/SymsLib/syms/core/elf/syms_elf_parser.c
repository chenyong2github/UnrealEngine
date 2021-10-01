// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_ELF_PARSER_C
#define SYMS_ELF_PARSER_C

////////////////////////////////
//~ NOTE(rjf): Functions

//- rjf: low-level header/section parsing

SYMS_API SYMS_ElfImgHeader
syms_elf_img_header_from_file(SYMS_String8 file)
{
  SYMS_ElfImgHeader img;
  syms_memzero_struct(&img);
  void *file_base = file.str;
  SYMS_U64Range file_range = syms_make_u64_range(0, file.size);
  
  //- rjf: predetermined read offsets
  SYMS_U64 elf_header_off = 0;
  
  //- rjf: figure out if this file starts with a ELF header
  SYMS_U8 sig[SYMS_ElfIdentifier_NINDENT];
  SYMS_B32 sig_is_elf = syms_false;
  {
    syms_memset(sig, 0, sizeof(sig));
    syms_based_range_read(file_base, file_range, elf_header_off, sizeof(sig), sig);
    sig_is_elf = (sig[SYMS_ElfIdentifier_MAG0] == 0x7f &&
                  sig[SYMS_ElfIdentifier_MAG1] == 'E'  &&
                  sig[SYMS_ElfIdentifier_MAG2] == 'L'  &&
                  sig[SYMS_ElfIdentifier_MAG3] == 'F');
  }
  
  //- rjf: parse ELF header
  SYMS_ElfEhdr64 elf_header;
  SYMS_B32 is_32bit = syms_false;
  SYMS_B32 good_elf_header = syms_false;
  if(sig_is_elf)
  {
    syms_memzero_struct(&elf_header);
    SYMS_U64 bytes_read = 0;
    switch(sig[SYMS_ElfIdentifier_CLASS])
    {
      //- rjf: parse 32-bit header
      case SYMS_ElfClass_32:
      {
        SYMS_ElfEhdr32 elf_header_32;
        syms_memzero_struct(&elf_header_32);
        bytes_read = syms_based_range_read_struct(file_base, file_range, elf_header_off, &elf_header_32);
        elf_header = syms_elf_ehdr64_from_ehdr32(elf_header_32);
        good_elf_header = bytes_read == sizeof(elf_header_32);
        is_32bit = syms_true;
      }break;
      //- rjf: parse 64-bit header
      case SYMS_ElfClass_64:
      {
        syms_memzero_struct(&elf_header);
        bytes_read = syms_based_range_read_struct(file_base, file_range, elf_header_off, &elf_header);
        good_elf_header = bytes_read == sizeof(elf_header);
      }break;
      default:break;
    }
  }
  
  //- rjf: parse section header
  SYMS_U64 sh_name_low_offset  = SYMS_U64_MAX;
  SYMS_U64 sh_name_high_offset = SYMS_U64_MAX;
  SYMS_B32 good_section_header = syms_false;
  SYMS_ElfShdr64 section_header;
  if(good_elf_header)
  {
    syms_memzero_struct(&section_header);
    SYMS_U64 bytes_read = 0;
    SYMS_U64 shstr_off = elf_header.e_shoff + elf_header.e_shentsize*elf_header.e_shstrndx;
    switch (sig[SYMS_ElfIdentifier_CLASS]) {
      case SYMS_ElfClass_32: {
        SYMS_ElfShdr32 section_header_32;
        syms_memzero_struct(&section_header_32);
        bytes_read = syms_based_range_read_struct(file_base, file_range, shstr_off, &section_header_32);
        section_header = syms_elf_shdr64_from_shdr32(section_header_32);
        good_section_header = bytes_read == sizeof(section_header_32);
      } break;
      case SYMS_ElfClass_64: {
        bytes_read = syms_based_range_read_struct(file_base, file_range, shstr_off, &section_header);
        good_section_header = bytes_read == sizeof(section_header);
      } break;
    }
    
    //- rjf: save base address and high address of string header names
    sh_name_low_offset  = section_header.sh_offset;
    sh_name_high_offset = sh_name_low_offset + section_header.sh_size;
  }
  
  //- rjf: parse program headers
  SYMS_U64 base_address = 0;
  if(good_section_header)
  {
    SYMS_U64 program_header_off = elf_header.e_phoff;
    SYMS_U64 program_header_num = (elf_header.e_phnum != SYMS_U16_MAX) ? elf_header.e_phnum : section_header.sh_info;
    
    //- rjf: search for base address, which is stored in program header that is closest to entry point
    SYMS_U64 prev_delta = SYMS_U64_MAX;
    for(SYMS_U64 i = 0; i < program_header_num; i += 1)
    {
      SYMS_ElfPhdr64 program_header;
      syms_memzero_struct(&program_header);
      switch(sig[SYMS_ElfIdentifier_CLASS])
      {
        case SYMS_ElfClass_32:
        {
          SYMS_ElfPhdr32 h32;
          syms_memzero_struct(&h32);
          syms_based_range_read_struct(file_base, file_range, program_header_off + i*sizeof(SYMS_ElfPhdr32), &h32);
          program_header = syms_elf_phdr64_from_phdr32(h32);
        }break;
        
        case SYMS_ElfClass_64:
        {
          syms_based_range_read_struct(file_base, file_range, program_header_off + i*sizeof(SYMS_ElfPhdr64), &program_header);
        }break;
      }
      
      if(program_header.p_type == SYMS_ElfPKind_Load)
      {
        if(elf_header.e_entry >= program_header.p_vaddr)
        {
          SYMS_U64 delta = elf_header.e_entry - program_header.p_vaddr;
          if(delta < prev_delta)
          {
            base_address = program_header.p_vaddr;
            prev_delta = delta;
          }
        }
      }
    }
  }
  
  //- rjf: determine SYMS_Arch from the ELF machine kind
  SYMS_Arch arch = SYMS_Arch_Null;
  switch(elf_header.e_machine)
  {
    default:break;
    case SYMS_ElfMachineKind_AARCH64: arch = SYMS_Arch_ARM;   break;
    case SYMS_ElfMachineKind_ARM:     arch = SYMS_Arch_ARM32; break;
    case SYMS_ElfMachineKind_386:     arch = SYMS_Arch_X86;   break;
    case SYMS_ElfMachineKind_X86_64:  arch = SYMS_Arch_X64;   break;
    case SYMS_ElfMachineKind_PPC:     arch = SYMS_Arch_PPC;   break;
    case SYMS_ElfMachineKind_PPC64:   arch = SYMS_Arch_PPC64; break;
    case SYMS_ElfMachineKind_IA_64:   arch = SYMS_Arch_IA64;  break;
  }
  
  //- rjf: fill img
  if(good_elf_header)
  {
    img.valid = 1;
    img.is_32bit = is_32bit;
    img.ehdr = elf_header;
    img.arch = arch;
    img.sh_name_low_offset = sh_name_low_offset;
    img.sh_name_high_offset = sh_name_high_offset;
    img.base_address = base_address;
  }
  
  return img;
}

SYMS_API SYMS_ElfSectionArray
syms_elf_section_array_from_img_header(SYMS_Arena *arena, SYMS_String8 file, SYMS_ElfImgHeader img)
{
  SYMS_ElfSectionArray result = {0};
  void *file_base = file.str;
  SYMS_U64Range file_range = syms_make_u64_range(0, file.size);
  
  //- rjf: figure out section count
  SYMS_U64 section_count = img.ehdr.e_shnum;
  
  //- rjf: figure out section range and section header size (32-bit or 64-bit)
  SYMS_U64Range section_range = syms_make_u64_range(SYMS_U64_MAX, SYMS_U64_MAX);
  SYMS_U64 section_header_size = SYMS_U64_MAX;
  {
    SYMS_U64 section_range_base = img.ehdr.e_shoff+img.ehdr.e_shentsize;
    switch(img.is_32bit)
    {
      case syms_true:  section_header_size = sizeof(SYMS_ElfShdr32); break;
      case syms_false: section_header_size = sizeof(SYMS_ElfShdr64); break;
      default: break;
    }
    section_range.min = section_range_base;
    section_range.max = section_range_base+img.ehdr.e_shnum*section_header_size;
  }
  
  //- rjf: allocate sections
  SYMS_ElfSection *sections = syms_push_array_zero(arena, SYMS_ElfSection, section_count);
  
  //- rjf: parse section headers
  for(SYMS_U64 section_idx = 0; section_idx < section_count; section_idx += 1)
  {
    // rjf: parse section header
    SYMS_ElfShdr64 header;
    if(img.is_32bit)
    {
      // NOTE(rjf): In the case of 32-bit ELF files, we need to convert the 32-bit section
      // headers to the 64-bit format, which is what we'll be using everywhere else.
      SYMS_ElfShdr32 header32;
      syms_based_range_read_struct(file_base, section_range, section_idx*sizeof(header32), &header32);
      header = syms_elf_shdr64_from_shdr32(header32);
    }
    else
    {
      syms_based_range_read_struct(file_base, section_range, section_idx*sizeof(header), &header);
    }
    
    // rjf: parse section name
    SYMS_String8 name = syms_based_range_read_string(file_base, file_range, img.sh_name_low_offset + header.sh_name);
    SYMS_String8 name_stabilized = syms_push_string_copy(arena, name);
    
    // allen: determine virt size vs file size
    SYMS_U64 virt_size = header.sh_size;
    if ((header.sh_flags & SYMS_ElfSectionFlag_ALLOC) == 0){
      virt_size = 0;
    }
    SYMS_U64 file_size = header.sh_size;
    if (header.sh_type == SYMS_ElfSectionCode_NOBITS){
      file_size = 0;
    }
    
    // rjf: fill section data
    sections[section_idx].code = (SYMS_ElfSectionCode)header.sh_type;
    sections[section_idx].virtual_range = syms_make_u64_range(header.sh_addr, header.sh_addr + virt_size);
    sections[section_idx].file_range = syms_make_u64_range(header.sh_offset, header.sh_offset + file_size);
    sections[section_idx].name = name_stabilized;
  }
  
  //- rjf: fill result
  result.v = sections;
  result.count = section_count;
  
  //- rjf: if the last section is empty, subtract 1 from the count so we do not report it
  // to the user as real information
  if(result.count > 0 && result.v[result.count-1].name.size == 0)
  {
    result.count -= 1;
  }
  
  return result;
}

SYMS_API SYMS_ElfExtDebugRef
syms_elf_ext_debug_ref_from_elf_section_array(SYMS_String8 file, SYMS_ElfSectionArray sections)
{
  void *file_base = file.str;
  SYMS_U64Range file_range = syms_make_u64_range(0, file.size);
  SYMS_ElfExtDebugRef result;
  syms_memzero_struct(&result);
  for(SYMS_U64 section_idx = 0; section_idx < sections.count; section_idx += 1)
  {
    if(syms_string_match(sections.v[section_idx].name, syms_str8_lit(".gnu_debuglink"), 0))
    {
      //- rjf: offsets
      SYMS_U64 path_off     = sections.v[section_idx].file_range.min;
      SYMS_U64 checksum_off = SYMS_U64_MAX;
      
      //- rjf: read external debug info path
      result.path = syms_based_range_read_string(file_base, file_range, path_off);
      SYMS_U64 path_bytes = result.path.size + 1;
      
      //- rjf: calculate checksum read offset; pad to the next 4-byte boundary
      checksum_off = path_off + path_bytes;
      checksum_off += (checksum_off % 4);
      
      //- rjf: read checksum
      syms_based_range_read_struct(file_base, file_range, checksum_off, &result.external_file_checksum);
      
      break;
    }
  }
  return result;
}

//- rjf: high-level API canonical conversions

SYMS_API SYMS_SecInfo
syms_elf_section_info_from_elf_section(SYMS_ElfSection elf_section){
  SYMS_SecInfo result = {0};
  result.vrange = elf_section.virtual_range;
  result.frange = elf_section.file_range;
  result.name = elf_section.name;
  return(result);
}

SYMS_API SYMS_String8
syms_elf_sec_name_from_elf_section(SYMS_ElfSection elf_section){
  SYMS_String8 result = elf_section.name;
  return(result);
}

//- rjf: file accelerator

SYMS_API SYMS_ElfFileAccel *
syms_elf_file_accel_from_data(SYMS_Arena *arena, SYMS_String8 string)
{
  SYMS_ElfFileAccel *file_accel = syms_push_array(arena, SYMS_ElfFileAccel, 1);
  syms_memzero_struct(file_accel);
  file_accel->header = syms_elf_img_header_from_file(string);
  if (file_accel->header.valid){
    file_accel->format = SYMS_FileFormat_ELF;
  }
  return file_accel;
}

//- rjf: binary

SYMS_API SYMS_ElfBinAccel *
syms_elf_bin_accel_from_file(SYMS_Arena *arena, SYMS_String8 data, SYMS_ElfFileAccel *file_accel)
{
  SYMS_ElfBinAccel *bin_accel = syms_push_array(arena, SYMS_ElfBinAccel, 1);
  syms_memzero_struct(bin_accel);
  syms_memmove(&bin_accel->header, &file_accel->header, sizeof(file_accel->header));
  bin_accel->format = file_accel->format;
  bin_accel->sections = syms_elf_section_array_from_img_header(arena, data, file_accel->header);
  return bin_accel;
}

SYMS_API SYMS_ExtFileList
syms_elf_ext_file_list_from_bin(SYMS_Arena *arena, SYMS_String8 file, SYMS_ElfBinAccel *bin_accel)
{
  SYMS_ExtFileList list;
  syms_memzero_struct(&list);
  SYMS_ElfExtDebugRef ext_debug_ref = syms_elf_ext_debug_ref_from_elf_section_array(file, bin_accel->sections);
  if(ext_debug_ref.path.size != 0)
  {
    SYMS_ExtFileNode *node = syms_push_array(arena, SYMS_ExtFileNode, 1);
    node->ext_file.file_name = ext_debug_ref.path;
    syms_memzero_struct(&node->ext_file.match_key);
    syms_memmove(node->ext_file.match_key.v, &ext_debug_ref.external_file_checksum, sizeof(SYMS_U32));
    SYMS_QueuePush(list.first, list.last, node);
    list.node_count += 1;
  }
  return list;
}

SYMS_API SYMS_SecInfoArray
syms_elf_sec_info_array_from_bin(SYMS_Arena *arena, SYMS_String8 data, SYMS_ElfBinAccel *bin)
{
  SYMS_SecInfoArray array;
  syms_memzero_struct(&array);
  array.count = bin->sections.count;
  array.sec_info = syms_push_array_zero(arena, SYMS_SecInfo, array.count);
  for(SYMS_U64 idx = 0; idx < array.count; idx += 1)
  {
    array.sec_info[idx] = syms_elf_section_info_from_elf_section(bin->sections.v[idx]);
  }
  return array;
}

SYMS_API SYMS_U64
syms_elf_default_vbase_from_bin(SYMS_ElfBinAccel *bin)
{
  return bin->header.base_address;
}

SYMS_API SYMS_Arch
syms_elf_arch_from_bin(SYMS_ElfBinAccel *bin){
  SYMS_Arch result = bin->header.arch;
  return(result);
}

////////////////////////////////
// NOTE(allen): ELF Specific Helpers

SYMS_API SYMS_ElfSection*
syms_elf_sec_from_bin_name__unstable(SYMS_ElfBinAccel *bin, SYMS_String8 name){
  SYMS_ElfSection *result = 0;
  for (SYMS_ElfSection *section = bin->sections.v, *opl = bin->sections.v + bin->sections.count;
       section < opl;
       section += 1){
    if (syms_string_match(name, section->name, 0)){
      result = section;
      break;
    }
  }
  return(result);
}

#endif // SYMS_ELF_PARSER_C
