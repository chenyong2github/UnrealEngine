// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_REGS_H
#define SYMS_REGS_H

////////////////////////////////
// NOTE(allen): Basic Register Types

typedef union SYMS_Reg16{
  SYMS_U8 v[2];
  SYMS_U16 u16;
} SYMS_Reg16;

typedef union SYMS_Reg32{
  SYMS_U8 v[4];
  SYMS_U32 u32;
  SYMS_F32 f32;
} SYMS_Reg32;

typedef union SYMS_Reg64{
  SYMS_U8 v[8];
  SYMS_U64 u64;
  SYMS_F64 f64;
} SYMS_Reg64;

#pragma pack(push, 1)
typedef struct SYMS_Reg80{
  SYMS_U64 int1_frac63;
  SYMS_U16 sign1_exp15;
} SYMS_Reg80;
#pragma pack(pop)

typedef union SYMS_Reg128{
  SYMS_U8 v[16];
  SYMS_U32 u32[4];
  SYMS_F32 f32[4];
  SYMS_U64 u64[2];
  SYMS_F64 f64[2];
} SYMS_Reg128;

typedef union SYMS_Reg256{
  SYMS_U8 v[32];
  SYMS_U32 u32[8];
  SYMS_F32 f32[8];
  SYMS_U64 u64[4];
  SYMS_F64 f64[4];
} SYMS_Reg256;

typedef struct SYMS_RegSection{
  SYMS_U32 offset;
  SYMS_U32 size;
} SYMS_RegSection;

typedef struct SYMS_RegMetadata{
  SYMS_U32 offset;
  SYMS_U32 size;
  SYMS_String8 name;
  SYMS_U32 reg_class;
  SYMS_U32 alias_to;
} SYMS_RegMetadata;

////////////////////////////////
// NOTE(allen): Basic Register Helpers

SYMS_API SYMS_String8 syms_reg_from_metadata_id(SYMS_RegMetadata *metadata, SYMS_U64 count,
                                                void *reg_file, SYMS_RegID reg_id);
#define syms_reg_from_arch_id(AR,f,i) syms_reg_from_metadata_id(reg_metadata_##AR,SYMS_Reg##AR##Code_COUNT,(f),(i))

#endif // SYMS_REGS_H
