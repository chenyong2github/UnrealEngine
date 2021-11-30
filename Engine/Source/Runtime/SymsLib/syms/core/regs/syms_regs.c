// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_REGS_C
#define SYMS_REGS_C

////////////////////////////////
// NOTE(allen): Basic Register Helpers

SYMS_API SYMS_String8
syms_reg_from_metadata_id(SYMS_RegMetadata *metadata, SYMS_U64 count, void *reg_file, SYMS_RegID reg_id){
  SYMS_String8 result = {0};
  if (reg_id <= count){
    SYMS_RegMetadata *m = metadata + reg_id;
    result.str = (SYMS_U8*)reg_file + m->offset;
    result.size = m->size;
  }
  return(result);
}

#endif // SYMS_REGS_C
