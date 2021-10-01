// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_MACH_PARSER_C
#define SYMS_MACH_PARSER_C

////////////////////////////////
//~ NOTE(allen): MACH Parser Functions

SYMS_API SYMS_MachBinAccel*
syms_mach_bin_from_base_range(SYMS_Arena *arena, void *base, SYMS_U64Range range){
  SYMS_MachBinAccel *result = (SYMS_MachBinAccel*)&syms_format_nil;
  
  //- read properties from magic
  SYMS_U32 magic = 0;
  syms_based_range_read_struct(base, range, 0, &magic);
  
  SYMS_B32 is_mach = syms_false;
  SYMS_B32 is_swapped = syms_false;
  SYMS_B32 is_32 = syms_false;
  switch (magic){
    case SYMS_MACH_MAGIC_32:
    {
      is_mach = syms_true;
      is_32 = syms_true;
    }break;
    case SYMS_MACH_MAGIC_64:
    {
      is_mach = syms_true;
    }break;
    
    case SYMS_MACH_CIGAM_32:
    {
      is_mach = syms_true;
      is_swapped = syms_true;
      is_32 = syms_true;
    }break;
    case SYMS_MACH_CIGAM_64:
    {
      is_mach = syms_true;
      is_swapped = syms_true;
    }break;
  }
  
  if (is_mach){
    //- read header
    SYMS_U64 after_header_off = 0;
    SYMS_MachHeader64 header = {0};
    if (is_32){
      SYMS_MachHeader32 header32 = {0};
      after_header_off = syms_based_range_read_struct(base, range, 0, &header32);
      if (is_swapped){
        syms_mach_header32_endian_swap_in_place(&header32);
      }
      syms_mach_header64_from_header32(&header, &header32);
    }
    else{
      after_header_off = syms_based_range_read_struct(base, range, 0, &header);
      if (is_swapped){
        syms_mach_header64_endian_swap_in_place(&header);
      }
    }
    
    //- gather segment and section lists
    SYMS_ArenaTemp scratch = syms_get_scratch(&arena, 1);
    
    SYMS_MachSegmentNode *segment_first = 0;
    SYMS_MachSegmentNode *segment_last = 0;
    SYMS_U32 segment_count = 0;
    
    SYMS_MachSectionNode *section_first = 0;
    SYMS_MachSectionNode *section_last = 0;
    SYMS_U32 section_count = 0;
    
    SYMS_U64 next_cmd_off = after_header_off;
    for (SYMS_U32 i = 0; i < header.ncmds; i += 1){
      //- align read offset
      SYMS_U64 cmd_off = 0;
      if (is_32){
        cmd_off = SYMS_AlignPow2(next_cmd_off, 4);
      }
      else{
        cmd_off = SYMS_AlignPow2(next_cmd_off, 8);
      }
      
      //- read command
      SYMS_MachLoadCommand lc = {0};
      syms_based_range_read_struct(base, range, cmd_off, &lc);
      switch (lc.type){
        // TODO(allen): SYMS_MachSegmentCommand32?
        
        case SYMS_MachLoadCommandType_SEGMENT_64:
        {
          // read segment 64
          SYMS_MachSegmentCommand64 segment_command64 = {0};
          syms_based_range_read_struct(base, range, cmd_off, &segment_command64);
          if (is_swapped){
            syms_mach_segment_command64_endian_swap_in_place(&segment_command64);
          }
          SYMS_U64 after_seg_off = cmd_off + sizeof(SYMS_MachSegmentCommand64);
          
          // push segment node
          SYMS_MachSegmentNode *segment_node = syms_push_array_zero(scratch.arena, SYMS_MachSegmentNode, 1);
          segment_node->data = segment_command64;
          SYMS_QueuePush(segment_first, segment_last, segment_node);
          segment_count += 1;
          
          // loop over sections
          SYMS_U64 next_sec_off = after_seg_off;
          SYMS_U64 sec_count = segment_command64.nsects;
          for (SYMS_U32 k = 0; k < sec_count; k += 1){
            // read section 64
            SYMS_U64 sec_off = next_sec_off;
            SYMS_MachSection64 section64 = {0};
            syms_based_range_read_struct(base, range, sec_off, &section64);
            if (is_swapped){
              syms_mach_section64_endian_swap_in_place(&section64);
            }
            next_sec_off = sec_off + sizeof(SYMS_MachSection64);
            
            // push section node
            SYMS_MachSectionNode *section_node = syms_push_array_zero(scratch.arena, SYMS_MachSectionNode, 1);
            section_node->data = section64;
            SYMS_QueuePush(section_first, section_last, section_node);
            section_count += 1;
          }
        }break;
      }
      
      next_cmd_off = cmd_off + lc.size;
    }
    
    //- segment array from list
    SYMS_MachSegmentCommand64 *segments = syms_push_array(arena, SYMS_MachSegmentCommand64, segment_count);
    {
      SYMS_MachSegmentNode *segment_node = segment_first;
      SYMS_MachSegmentCommand64 *segment_ptr = segments;
      SYMS_MachSegmentCommand64 *segment_opl = segments + segment_count;
      for (; segment_ptr < segment_opl; segment_ptr += 1, segment_node = segment_node->next){
        *segment_ptr = segment_node->data;
      }
    }
    
    //- section array from list
    SYMS_MachSection64 *sections = syms_push_array(arena, SYMS_MachSection64, section_count);
    {
      SYMS_MachSectionNode *section_node = section_first;
      SYMS_MachSection64 *section_ptr = sections;
      SYMS_MachSection64 *section_opl = sections + section_count;
      for (; section_ptr < section_opl; section_ptr += 1, section_node = section_node->next){
        *section_ptr = section_node->data;
      }
    }
    
    //- fill result
    result = syms_push_array(arena, SYMS_MachBinAccel, 1);
    result->format = SYMS_FileFormat_MACH;
    result->arch = syms_mach_arch_from_cputype(header.cputype);
    result->segment_count = segment_count;
    result->segments = segments;
    result->section_count = section_count;
    result->sections = sections;
    
    syms_release_scratch(scratch);
  }
  
  return(result);
}

SYMS_API SYMS_MachFileAccel*
syms_mach_file_accel_from_data(SYMS_Arena *arena, SYMS_String8 data){
  void *base = data.str;
  SYMS_U64Range range = syms_make_u64_range(0, data.size);
  
  SYMS_U32 magic = 0;
  syms_based_range_read_struct(base, range, 0, &magic);
  
  SYMS_B32 is_mach = syms_false;
  SYMS_B32 is_fat = syms_false;
  switch (magic){
    case SYMS_MACH_MAGIC_32:
    case SYMS_MACH_CIGAM_32:
    case SYMS_MACH_MAGIC_64:
    case SYMS_MACH_CIGAM_64:
    {
      is_mach = syms_true;
    }break;
    
    case SYMS_MACH_FAT_MAGIC:
    case SYMS_MACH_FAT_CIGAM:
    {
      is_mach = syms_true;
      is_fat = syms_true;
    }break;
  }
  
  SYMS_B32 is_swapped = syms_false;
  if (is_mach){
    switch (magic){
      case SYMS_MACH_CIGAM_32:
      case SYMS_MACH_CIGAM_64:
      case SYMS_MACH_FAT_CIGAM:
      {
        is_swapped = syms_true;
      }break;
    }
  }
  
  SYMS_MachFileAccel *result = (SYMS_MachFileAccel *)&syms_format_nil;
  if (is_mach){
    result = syms_push_array(arena, SYMS_MachFileAccel, 1);
    result->format = SYMS_FileFormat_MACH;
    result->is_swapped = is_swapped;
    result->is_fat = is_fat;
  }
  return(result);
}

SYMS_API SYMS_B32
syms_mach_file_is_bin(SYMS_MachFileAccel *file){
  SYMS_B32 result = (!file->is_fat);
  return(result);
}

SYMS_API SYMS_MachBinAccel *
syms_mach_bin_accel_from_file(SYMS_Arena *arena, SYMS_String8 data, SYMS_MachFileAccel *file){
  SYMS_MachBinAccel *result = (SYMS_MachBinAccel*)&syms_format_nil;
  if (!file->is_fat){
    SYMS_U64Range range = syms_make_u64_range(0, data.size);
    result = syms_mach_bin_from_base_range(arena, data.str, range);
  }
  return(result);
}

SYMS_API SYMS_B32
syms_mach_file_is_bin_list(SYMS_MachFileAccel *file_accel){
  SYMS_B32 result = file_accel->is_fat;
  return(result);
}

SYMS_API SYMS_MachBinListAccel*
syms_mach_bin_list_accel_from_file(SYMS_Arena *arena, SYMS_String8 data, SYMS_MachFileAccel *file){
  SYMS_MachBinListAccel *result = (SYMS_MachBinListAccel*)&syms_format_nil;
  
  if (file->is_fat){
    void *base = data.str;
    SYMS_U64Range range = syms_make_u64_range(0, data.size);
    
    SYMS_B32 is_swapped = file->is_swapped;
    
    SYMS_U32 read_offset = 0;
    SYMS_MachFatHeader fat_header = {0};
    read_offset += syms_based_range_read_struct(base, range, read_offset, &fat_header);
    if (is_swapped){
      syms_mach_fat_header_endian_swap_in_place(&fat_header);
    }
    
    SYMS_U64 fat_count = fat_header.nfat_arch;
    SYMS_MachFatArch *fats = syms_push_array_zero(arena, SYMS_MachFatArch, fat_count);
    
    SYMS_MachFatArch *fat_ptr = fats;
    for (SYMS_U32 i = 0; i < fat_count; i += 1, fat_ptr += 1){
      read_offset += syms_based_range_read_struct(base, range, read_offset, fat_ptr);
      if (is_swapped){
        syms_mach_fat_arch_endian_swap_in_place(fat_ptr);
      }
    }
    
    result = syms_push_array(arena, SYMS_MachBinListAccel, 1);
    result->format = SYMS_FileFormat_MACH;
    result->count = fat_count;
    result->fats = fats;
  }
  
  return(result);
}


////////////////////////////////
// NOTE(allen): Arch

SYMS_API SYMS_Arch
syms_mach_arch_from_bin(SYMS_MachBinAccel *bin){
  SYMS_Arch result = bin->arch;
  return(result);
}


////////////////////////////////
// NOTE(allen): Bin List

SYMS_API SYMS_BinInfoArray
syms_mach_bin_info_array_from_bin_list(SYMS_Arena *arena, SYMS_MachBinListAccel *bin_list){
  // allocate bin info array
  SYMS_U64 count = bin_list->count;
  SYMS_BinInfo *bin_info = syms_push_array(arena, SYMS_BinInfo, count);
  
  // fill bin info array
  SYMS_MachFatArch *fat_ptr = bin_list->fats;
  SYMS_BinInfo *bin_info_ptr = bin_info;
  for (SYMS_U64 i = 0; i < count; i += 1, bin_info_ptr += 1, fat_ptr += 1){
    bin_info_ptr->arch = syms_mach_arch_from_cputype(fat_ptr->cputype);
    bin_info_ptr->range = syms_make_u64_range(fat_ptr->offset, fat_ptr->offset + fat_ptr->size);
  } 
  
  // package up and return
  SYMS_BinInfoArray result = {0};
  result.count = count;
  result.bin_info = bin_info;
  return(result);
}

SYMS_API SYMS_MachBinAccel*
syms_mach_bin_accel_from_bin_list_number(SYMS_Arena *arena, SYMS_String8 data,
                                         SYMS_MachBinListAccel *bin_list, SYMS_U64 n){
  SYMS_MachBinAccel *result = (SYMS_MachBinAccel*)&syms_format_nil;
  if (1 <= n && n <= bin_list->count){
    SYMS_MachFatArch *fat = &bin_list->fats[n - 1];
    SYMS_U64Range range = syms_make_u64_range(fat->offset, fat->offset + fat->size);
    result = syms_mach_bin_from_base_range(arena, data.str, range);
  }
  return(result);
}


////////////////////////////////
// NOTE(allen): Binary Secs

SYMS_API SYMS_SecInfoArray
syms_mach_sec_info_array_from_bin(SYMS_Arena *arena, SYMS_String8 data, SYMS_MachBinAccel *bin){
  SYMS_SecInfoArray array;
  array.count = bin->section_count;
  array.sec_info = syms_push_array_zero(arena, SYMS_SecInfo, array.count);
  
  SYMS_MachSection64 *mach_sec = bin->sections;
  SYMS_MachSection64 *mach_sec_opl = bin->sections + array.count;
  SYMS_SecInfo *sec_info = array.sec_info;
  for (; mach_sec < mach_sec_opl; sec_info += 1, mach_sec += 1){
    SYMS_U8 *ptr = mach_sec->sectname;
    SYMS_U8 *opl = mach_sec->sectname + SYMS_ARRAY_SIZE(mach_sec->sectname);
    for (;ptr < opl && *ptr != 0; ptr += 1);
    
    sec_info->name = syms_str8_range(mach_sec->sectname, ptr);
    // TODO(allen): figure out when these ranges actually apply and when the section isn't
    // there one side or the other.
    sec_info->vrange = syms_make_u64_range(mach_sec->addr,   mach_sec->addr +   mach_sec->size);
    sec_info->frange = syms_make_u64_range(mach_sec->offset, mach_sec->offset + mach_sec->size);
  }
  return array;
}

SYMS_API SYMS_U64
syms_mach_default_vbase_from_bin(SYMS_MachBinAccel *bin)
{
  // TODO(rjf): @nick verify
  SYMS_U64 min_vbase = 0;
  for(SYMS_U64 segment_idx = 0; segment_idx < bin->segment_count; segment_idx += 1)
  {
    SYMS_MachSegmentCommand64 *segment = bin->segments + segment_idx;
    if(min_vbase == 0 || segment->vmaddr < min_vbase)
    {
      min_vbase = segment->vmaddr;
    }
  }
  return min_vbase;
}

#endif // SYMS_MACH_PARSER_C
