// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_MACH_PARSER_H
#define SYMS_MACH_PARSER_H

////////////////////////////////
//~ NOTE(allen): MACH Parser Types

typedef struct SYMS_MachFileAccel{
  SYMS_FileFormat format;
  SYMS_B32 is_swapped;
  SYMS_B32 is_fat;
} SYMS_MachFileAccel;

typedef struct SYMS_MachBinListAccel{
  SYMS_FileFormat format;
  SYMS_MachFatArch *fats;
  SYMS_U32 count;
} SYMS_MachBinListAccel;

typedef struct SYMS_MachSegmentNode{
  struct SYMS_MachSegmentNode *next;
  SYMS_MachSegmentCommand64 data;
} SYMS_MachSegmentNode;

typedef struct SYMS_MachSectionNode{
  struct SYMS_MachSectionNode *next;
  SYMS_MachSection64 data;
} SYMS_MachSectionNode;

typedef struct SYMS_MachBinAccel{
  SYMS_FileFormat format;
  
  SYMS_Arch arch;
  
  SYMS_MachSegmentCommand64 *segments;
  SYMS_U32 segment_count;
  
  SYMS_MachSection64 *sections;
  SYMS_U32 section_count;
} SYMS_MachBinAccel;

////////////////////////////////
//~ NOTE(allen): MACH Parser Functions

// accelerators
//  mach specific
SYMS_API SYMS_MachBinAccel*     syms_mach_bin_from_base_range(SYMS_Arena *arena, void *base, SYMS_U64Range range);

// main api
SYMS_API SYMS_MachFileAccel*    syms_mach_file_accel_from_data(SYMS_Arena *arena, SYMS_String8 data);
SYMS_API SYMS_B32               syms_mach_file_is_bin(SYMS_MachFileAccel *file);
SYMS_API SYMS_MachBinAccel*     syms_mach_bin_accel_from_file(SYMS_Arena *arena, SYMS_String8 data,
                                                              SYMS_MachFileAccel *file);
SYMS_API SYMS_B32               syms_mach_file_is_bin_list(SYMS_MachFileAccel *file_accel);
SYMS_API SYMS_MachBinListAccel* syms_mach_bin_list_accel_from_file(SYMS_Arena *arena, SYMS_String8 data,
                                                                   SYMS_MachFileAccel *file);

// arch
SYMS_API SYMS_Arch              syms_mach_arch_from_bin(SYMS_MachBinAccel *bin);

// bin list
SYMS_API SYMS_BinInfoArray      syms_mach_bin_info_array_from_bin_list(SYMS_Arena *arena,
                                                                       SYMS_MachBinListAccel *bin_list);
SYMS_API SYMS_MachBinAccel*     syms_mach_bin_accel_from_bin_list_number(SYMS_Arena *arena, SYMS_String8 data,
                                                                         SYMS_MachBinListAccel *bin_list,
                                                                         SYMS_U64 n);

// binary secs
SYMS_API SYMS_SecInfoArray      syms_mach_sec_info_array_from_bin(SYMS_Arena *arena, SYMS_String8 data,
                                                                  SYMS_MachBinAccel *bin);

SYMS_API SYMS_U64               syms_mach_default_vbase_from_bin(SYMS_MachBinAccel *bin);

#endif // SYMS_MACH_PARSER_H
