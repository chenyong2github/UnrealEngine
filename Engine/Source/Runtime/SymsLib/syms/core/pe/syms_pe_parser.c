// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_PE_PARSER_C
#define SYMS_PE_PARSER_C

////////////////////////////////
// NOTE(allen): Functions

SYMS_API SYMS_PeFileAccel*
syms_pe_file_accel_from_data(SYMS_Arena *arena, SYMS_String8 data){
  // based range setup
  void *base = data.str;
  SYMS_U64Range range = {0, data.size};
  
  // read dos header
  SYMS_DosHeader dos_header = {0};
  syms_based_range_read_struct(base, range, 0, &dos_header);
  
  // fill result
  SYMS_PeFileAccel *result = (SYMS_PeFileAccel*)&syms_format_nil;
  if (dos_header.magic == SYMS_DOS_MAGIC){
    result = syms_push_array(arena, SYMS_PeFileAccel, 1);
    result->format = SYMS_FileFormat_PE;
    result->coff_off = dos_header.coff_file_offset;
  }
  
  return(result);
}

SYMS_API SYMS_PeBinAccel*
syms_pe_bin_accel_from_file(SYMS_Arena *arena, SYMS_String8 data, SYMS_PeFileAccel *accel){
  // based range setup
  void *base = data.str;
  SYMS_U64Range range = {0, data.size};
  
  // read pe magic
  SYMS_U32 coff_off = accel->coff_off;
  SYMS_U32 pe_magic = 0;
  syms_based_range_read_struct(base, range, coff_off, &pe_magic);
  
  // check pe magic
  SYMS_PeBinAccel *result = (SYMS_PeBinAccel*)&syms_format_nil;
  if (pe_magic == SYMS_PE_MAGIC){
    
    // read coff header
    SYMS_U32 coff_header_off = coff_off + sizeof(pe_magic);
    SYMS_CoffHeader coff_header = {0};
    syms_based_range_read_struct(base, range, coff_header_off, &coff_header);
    
    // range of optional extension header ("optional" for short)
    SYMS_U32 optional_size = coff_header.size_of_optional_header;
    SYMS_U64 after_coff_header_off = coff_header_off + sizeof(coff_header);
    SYMS_U64 after_optional_header_off = after_coff_header_off + optional_size;
    SYMS_U64Range optional_range = {0};
    optional_range.min = SYMS_ClampTop(after_coff_header_off, data.size);
    optional_range.max = SYMS_ClampTop(after_optional_header_off, data.size);
    
    // get sections
    SYMS_U64 sec_array_off = optional_range.max;
    SYMS_U64 sec_array_raw_opl = sec_array_off + coff_header.section_count*sizeof(SYMS_CoffSection);
    SYMS_U64 sec_array_opl = SYMS_ClampTop(sec_array_raw_opl, data.size);
    SYMS_U64 clamped_sec_count = (sec_array_opl - sec_array_off)/sizeof(SYMS_CoffSection);
    SYMS_CoffSection *sections = (SYMS_CoffSection*)((SYMS_U8*)base + sec_array_off);
    
    // get data directory from optional
    SYMS_U16 data_dir_offset = 0;
    SYMS_U32 data_dir_count = 0;
    if (optional_size > 0){
      // read magic number
      SYMS_U16 optional_magic = 0;
      syms_based_range_read_struct(base, optional_range, 0, &optional_magic);
      
      // read optional
      SYMS_U32 reported_data_dir_offset = 0;
      SYMS_U32 reported_data_dir_count = 0;
      switch (optional_magic){
        case SYMS_PE32_MAGIC:
        {
          SYMS_PeOptionalPe32 pe_optional = {0};
          syms_based_range_read_struct(base, optional_range, 0, &pe_optional);
          reported_data_dir_offset = sizeof(pe_optional);
          reported_data_dir_count = pe_optional.data_dir_count;
        }break;
        case SYMS_PE32PLUS_MAGIC:
        {
          SYMS_PeOptionalPe32Plus pe_optional = {0};
          syms_based_range_read_struct(base, optional_range, 0, &pe_optional);
          reported_data_dir_offset = sizeof(pe_optional);
          reported_data_dir_count = pe_optional.data_dir_count;
        }break;
      }
      
      // fill outputs from optional
      SYMS_U32 data_dir_max = (optional_size - data_dir_offset)/sizeof(SYMS_PeDataDirectory);
      data_dir_count = SYMS_ClampTop(reported_data_dir_count, data_dir_max);
      data_dir_offset = optional_range.min + reported_data_dir_offset;
    }
    
    // read info about debug file
    SYMS_U32 dbg_time = 0;
    SYMS_U32 dbg_age = 0;
    SYMS_PeGuid dbg_guid = {0};
    SYMS_U64 dbg_path_off = 0;
    SYMS_U64 dbg_path_size = 0;
    
    if (SYMS_PeDataDirectoryIndex_DEBUG < data_dir_count){
      SYMS_PeDataDirectory dbg_data_dir = {0};
      SYMS_U64 dbg_data_dir_offset = data_dir_offset + sizeof(SYMS_PeDataDirectory)*SYMS_PeDataDirectoryIndex_DEBUG;
      syms_based_range_read_struct(base, range, dbg_data_dir_offset, &dbg_data_dir);
      
      // find section that virtually contains the debug data
      SYMS_CoffSection *dbg_data_sec = 0;
      SYMS_CoffSection *sec = sections;
      for (SYMS_U64 i = 0; i < clamped_sec_count; i += 1, sec += 1){
        SYMS_U64 first = sec->virt_off;
        SYMS_U64 opl   = first + sec->virt_size;
        if (first <= dbg_data_dir.virt_off && dbg_data_dir.virt_off < opl){
          dbg_data_sec = sec;
          break;
        }
      }
      
      // read debug directory
      if (dbg_data_sec != 0){
        SYMS_U64 dbg_data_offset_from_sec = dbg_data_dir.virt_off - dbg_data_sec->virt_off;
        SYMS_U64 dbg_data_offset = dbg_data_sec->file_off + dbg_data_offset_from_sec;
        SYMS_PeDebugDirectory dbg_data = {0};
        SYMS_U64 dbg_data_size = SYMS_ClampTop(dbg_data_dir.virt_size, sizeof(dbg_data));
        syms_based_range_read(base, range, dbg_data_offset, dbg_data_size, &dbg_data);
        
        // extract external file info from codeview header
        if (dbg_data.type == SYMS_PeDebugDirectoryType_CODEVIEW){
          SYMS_U64 cv_offset = dbg_data.file_offset;
          SYMS_U32 cv_magic = 0;
          syms_based_range_read_struct(base, range, cv_offset, &cv_magic);
          switch (cv_magic){
            default:break;
            case SYMS_CODEVIEW_PDB20_MAGIC:
            {
              SYMS_PeCvHeaderPDB20 cv = {0};
              syms_based_range_read_struct(base, range, cv_offset, &cv);
              // TODO(allen): can we extend the ext match key system to use this in some way?
              dbg_time = cv.time;
              dbg_age = cv.age;
              dbg_path_off = cv_offset + sizeof(cv);
            }break;
            case SYMS_CODEVIEW_PDB70_MAGIC:
            {
              SYMS_PeCvHeaderPDB70 cv = {0};
              syms_based_range_read_struct(base, range, cv_offset, &cv);
              dbg_guid = cv.guid;
              dbg_age = cv.age;
              dbg_path_off = cv_offset + sizeof(cv);
            }break;
          }
          if (dbg_path_off > 0){
            SYMS_String8 dbg_path = syms_based_range_read_string(base, range, dbg_path_off);
            dbg_path_size = dbg_path.size;
          }
        }
      }
    }
    
    // fill result
    result = syms_push_array(arena, SYMS_PeBinAccel, 1);
    result->format = SYMS_FileFormat_PE;
    result->section_array_off = sec_array_off;
    result->section_count = clamped_sec_count;
    result->dbg_path_off = dbg_path_off;
    result->dbg_path_size = dbg_path_size;
    result->dbg_guid = dbg_guid;
    result->arch = syms_arch_from_coff_machine_type(coff_header.machine);
    
    
    // TODO(allen): HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK
    // This only works because we use it after initializing the rest of PeBinAccel.
    // The good news is we don't really want to bake this in here long term anyways
    // so the goal is to get this moved out into a separate UnwindAccel parsing stage.
    
    // TODO(allen): only applies for "intel" architectures - need a handler for
    // arm and mips data structures too.
    
    // read pdata
    SYMS_U64 pdata_off = 0;
    SYMS_U64 pdata_count = 0;
    
    {
      // try to get pdata section
      SYMS_CoffSection *pdata_sec = 0;
      SYMS_CoffSection *coff_sec = (SYMS_CoffSection*)(data.str + sec_array_off);
      for (SYMS_U64 n = 1; n <= clamped_sec_count; n += 1, coff_sec += 1){
        
        // extract name
        SYMS_U8 *name_base = coff_sec->name;
        SYMS_U8 *name_ptr = name_base;
        SYMS_U8 *name_opl = name_base + 8;
        for (;name_ptr < name_opl && *name_ptr != 0; name_ptr += 1);
        SYMS_String8 sec_name = syms_str8_range(name_base, name_ptr);
        
        // check name
        if (syms_string_match(sec_name, syms_str8_lit(".pdata"), 0)){
          pdata_sec = coff_sec;
          break;
        }
      }
      
      // determine pdata offset & count
      if (pdata_sec != 0){
        SYMS_U64 off = pdata_sec->file_off;
        SYMS_U64 size = pdata_sec->file_size;
        
        // save offset to pdata
        pdata_off = off;
        
        // scan range of pdata to eliminate nulls at the end
        SYMS_PeIntelPdata *first_pdata_intel = (SYMS_PeIntelPdata*)(data.str + off);
        SYMS_PeIntelPdata *opl_pdata_intel = first_pdata_intel + (size/sizeof(SYMS_PeIntelPdata));
        if (first_pdata_intel < opl_pdata_intel){
          SYMS_PeIntelPdata *pdata_intel_ptr = opl_pdata_intel - 1;
          for (;pdata_intel_ptr >= first_pdata_intel && pdata_intel_ptr->voff_first == 0;
               pdata_intel_ptr -= 1);
          pdata_intel_ptr += 1;
          pdata_count = (SYMS_U64)(pdata_intel_ptr - first_pdata_intel);
        }
      }
    }
    
    // fill pdata
    result->pdata_off = pdata_off;
    result->pdata_count = pdata_count;
  }
  
  return(result);
}

SYMS_API SYMS_Arch
syms_pe_arch_from_bin(SYMS_PeBinAccel *bin){
  SYMS_Arch result = bin->arch;
  return(result);
}

SYMS_API SYMS_ExtFileList
syms_pe_ext_file_list_from_bin(SYMS_Arena *arena, SYMS_String8 data, SYMS_PeBinAccel *bin){
  SYMS_ExtFileList result = {0};
  if (bin->dbg_path_size > 0 && bin->dbg_path_off + bin->dbg_path_size <= data.size){
    SYMS_String8 name = {0};
    name.str = data.str + bin->dbg_path_off;
    name.size = bin->dbg_path_size;
    SYMS_ExtFileNode *node = syms_push_array_zero(arena, SYMS_ExtFileNode, 1);
    SYMS_QueuePush(result.first, result.last, node);
    result.node_count += 1;
    node->ext_file.file_name = name;
    syms_memmove(&node->ext_file.match_key, &bin->dbg_guid, sizeof(bin->dbg_guid));
  }
  return(result);
}

SYMS_API SYMS_CoffSection
syms_pe_coff_section(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 n){
  SYMS_CoffSection result = {0};
  if (1 <= n && n <= bin->section_count){
    SYMS_U64 off = bin->section_array_off + (n - 1)*sizeof(result);
    syms_memmove(&result, data.str + off, sizeof(result));
  }
  return(result);
}

SYMS_API SYMS_SecInfoArray
syms_pe_sec_info_array_from_bin(SYMS_Arena *arena, SYMS_String8 data, SYMS_PeBinAccel *bin){
  SYMS_SecInfoArray result = {0};
  result.count = bin->section_count;
  result.sec_info = syms_push_array_zero(arena, SYMS_SecInfo, result.count);
  
  SYMS_SecInfo *sec_info = result.sec_info;
  SYMS_CoffSection *coff_sec = (SYMS_CoffSection*)(data.str + bin->section_array_off);
  
  for (SYMS_U64 i = 0; i < result.count; i += 1, sec_info += 1, coff_sec += 1){
    // extract name
    SYMS_U8 *name = coff_sec->name;
    SYMS_U8 *name_ptr = name;
    SYMS_U8 *name_opl = name + 8;
    for (;name_ptr < name_opl && *name_ptr != 0; name_ptr += 1);
    
    // fill sec info
    sec_info->name = syms_push_string_copy(arena, syms_str8_range(name, name_ptr));
    sec_info->vrange.min = coff_sec->virt_off;
    sec_info->vrange.max = coff_sec->virt_off + coff_sec->virt_size;
    sec_info->frange.min = coff_sec->file_off;
    sec_info->frange.max = coff_sec->file_off + coff_sec->file_size;
  }
  
  return(result);
}

////////////////////////////////
// NOTE(allen): PE Specific Helpers

SYMS_API SYMS_U64
syms_pe_binary_search_intel_pdata(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 voff){
  SYMS_U64 result = 0;
  
  // check if this bin includes a pdata array
  if (bin->pdata_count > 0){
    SYMS_PeIntelPdata *pdata_array = (SYMS_PeIntelPdata*)(data.str + bin->pdata_off);
    if (voff >= pdata_array[0].voff_first){
      
      // binary search:
      //  find max index s.t. pdata_array[index].voff_first <= voff
      //  we assume (i < j) -> (pdata_array[i].voff_first < pdata_array[j].voff_first)
      SYMS_U64 index = bin->pdata_count;
      SYMS_U64 min = 0;
      SYMS_U64 opl = bin->pdata_count;
      for (;;){
        SYMS_U64 mid = (min + opl)/2;
        SYMS_PeIntelPdata *pdata = pdata_array + mid;
        if (voff < pdata->voff_first){
          opl = mid;
        }
        else if (pdata->voff_first < voff){
          min = mid;
        }
        else{
          index = mid;
          break;
        }
        if (min + 1 >= opl){
          index = min;
          break;
        }
      }
      
      // if we are in range fill result
      {
        SYMS_PeIntelPdata *pdata = pdata_array + index;
        if (pdata->voff_first <= voff && voff < pdata->voff_one_past_last){
          result = bin->pdata_off + index*sizeof(SYMS_PeIntelPdata);
        }
      }
    }
  }
  
  return(result);
}

SYMS_API SYMS_U64
syms_pe_sec_number_from_voff(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 voff){
  SYMS_U64 sec_count = bin->section_count;
  SYMS_CoffSection *sec_array = (SYMS_CoffSection*)((SYMS_U8*)data.str + bin->section_array_off);
  SYMS_CoffSection *sec_ptr = sec_array;
  SYMS_U64 result = 0;
  for (SYMS_U64 i = 1; i <= sec_count; i += 1, sec_ptr += 1){
    if (sec_ptr->virt_off <= voff && voff < sec_ptr->virt_off + sec_ptr->virt_size){
      result = i;
      break;
    }
  }
  return(result);
}

SYMS_API void*
syms_pe_ptr_from_sec_number(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 n){
  void *result = 0;
  SYMS_U64 sec_count = bin->section_count;
  if (1 <= n && n <= sec_count){
    SYMS_CoffSection *sec_array = (SYMS_CoffSection*)((SYMS_U8*)data.str + bin->section_array_off);
    SYMS_CoffSection *sec = sec_array + n - 1;
    if (sec->file_size > 0){
      result = data.str + sec->file_off;
    }
  }
  return(result);
}

SYMS_API void*
syms_pe_ptr_from_foff(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 foff){
  void *result = 0;
  if (foff < data.size){
    result = data.str + foff;
  }
  return(result);
}

SYMS_API void*
syms_pe_ptr_from_voff(SYMS_String8 data, SYMS_PeBinAccel *bin, SYMS_U64 voff){
  //- get the section for this voff
  SYMS_U64 sec_count = bin->section_count;
  SYMS_CoffSection *sec_array = (SYMS_CoffSection*)((SYMS_U8*)data.str + bin->section_array_off);
  SYMS_CoffSection *sec_ptr = sec_array;
  SYMS_CoffSection *sec = 0;
  for (SYMS_U64 i = 1; i <= sec_count; i += 1, sec_ptr += 1){
    if (sec_ptr->virt_off <= voff && voff < sec_ptr->virt_off + sec_ptr->virt_size){
      sec = sec_ptr;
      break;
    }
  }
  
  //- adjust to file pointer
  void *result = 0;
  if (sec != 0 & sec_ptr->file_size > 0){
    result = data.str + voff - sec->virt_off + sec->file_off;
  }
  
  return(result);
}

#endif //SYMS_PE_PARSER_C
