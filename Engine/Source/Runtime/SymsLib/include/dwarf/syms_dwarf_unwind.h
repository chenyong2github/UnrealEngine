// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_DWARF_UNWIND_INCLUDE_H
#define SYMS_DWARF_UNWIND_INCLUDE_H

#ifndef DW_API
#define DW_API SYMS_API
#endif

typedef struct DwArchInfo 
{
  DwMode mode;
  SymsArch arch;
  dw_uint word_size;
  dw_uint addr_size;
  dw_uint ip_regid;
  dw_uint sp_regid;
} DwArchInfo; 

typedef struct DwUserCallbacks 
{
  DwArchInfo arch_info;

  void *regread_ctx;
  dw_regread_sig *regread;

  void *regwrite_ctx;
  dw_regwrite_sig *regwrite;

  void *memread_ctx;
  dw_memread_sig *memread;
} DwUserCallbacks;

#define DW_PRESERVED_REGS_COUNT_NT      33
#define DW_PRESERVED_REGS_COUNT_UNIX    17
#define DW_PRESERVED_REGS_MAX           (SYMS_MAX(DW_PRESERVED_REGS_COUNT_NT, DW_PRESERVED_REGS_COUNT_UNIX))

#define DW_CFA_OPCODE_MASK 0xC0
#define DW_CFA_OPER_MASK   0x3F
typedef enum 
{
  DW_CFA_NOP                   = 0x0,
  DW_CFA_SET_LOC               = 0x1,
  DW_CFA_ADVANCE_LOC1          = 0x2,
  DW_CFA_ADVANCE_LOC2          = 0x3,
  DW_CFA_ADVANCE_LOC4          = 0x4,
  DW_CFA_OFFSET_EXT            = 0x5,
  DW_CFA_RESTORE_EXT           = 0x6,
  DW_CFA_UNDEFINED             = 0x07,
  DW_CFA_SAME_VALUE            = 0x8,
  DW_CFA_REGISTER              = 0x9,
  DW_CFA_REMEMBER_STATE        = 0xA,
  DW_CFA_RESTORE_STATE         = 0xB,
  DW_CFA_DEF_CFA               = 0xC,
  DW_CFA_DEF_CFA_REGISTER      = 0xD,
  DW_CFA_DEF_CFA_OFFSET        = 0xE,
  DW_CFA_DEF_CFA_EXPR          = 0xF,
  DW_CFA_EXPR                  = 0x10,
  DW_CFA_OFFSET_EXT_SF         = 0x11,
  DW_CFA_DEF_CFA_SF            = 0x12,
  DW_CFA_DEF_CFA_OFFSET_SF     = 0x13,
  DW_CFA_VAL_OFFSET            = 0x14,
  DW_CFA_VAL_OFFSET_SF         = 0x15,
  DW_CFA_VAL_EXPR              = 0x16,

  DW_CFA_ADVANCE_LOC = 0x40,
  DW_CFA_OFFSET      = 0x80,
  DW_CFA_RESTORE     = 0xC0,

  DW_CFA_USER_LO = 0x1C,
  DW_CFA_USER_HI = 0x3F
} DwCFAOpcodeType;

#define DW_EH_PE_FORMAT_MASK    0x0F
#define DW_EH_PE_APPLY_MASK     0x70
#define DW_EH_PE_INDIRECT       0x80

#define DW_EH_PE_OMIT       0xFFu
/* Pointer sized unsigned value */
#define DW_EH_PE_PTR        0x00u
/* Unsigned LE base-128 value */
#define DW_EH_PE_ULEB128    0x01u
/* Unsigned 16-bit value */
#define DW_EH_PE_UDATA2     0x02u
/* Unsigned 32-bit value */
#define DW_EH_PE_UDATA4     0x03u
/* Unsigned 64-bit value */
#define DW_EH_PE_UDATA8     0x04u
/* Signed pointer */
#define DW_EH_PE_SIGNED     0x08u
/* Signed LE base-128 value */
#define DW_EH_PE_SLEB128    0x09u
/* Signed 16-bit value */
#define DW_EH_PE_SDATA2     0x0Au
/* Signed 32-bit value */
#define DW_EH_PE_SDATA4     0x0Bu
/* Signed 64-bit value */
#define DW_EH_PE_SDATA8     0x0Cu
typedef dw_uint DwEhPointerEncodingFormat;

typedef enum 
{
  DW_EH_PE_ABSPTR  = 0x00, /* Absolute value */
  DW_EH_PE_PCREL   = 0x10, /* Relative to address of encoded value */
  DW_EH_PE_TEXTREL = 0x20, /* Text-Relative (GCC-specific) */
  DW_EH_PE_DATAREL = 0x30, /* Data-Relative */
  /* The following are not documented by LSB v1.3, yet they are used by
     GCC, presumably they aren't documented by LSB since they aren't
     used on Linux:  */
  DW_EH_PE_FUNCREL = 0x40, /* Start of procedure relative */
  DW_EH_PE_ALIGNED = 0x50  /* Aligned Pointer */
} DwEHPointerEncodingApplication;

typedef enum 
{
  DW_VIRTUAL_UNWIND_DATA_NULL,
  DW_VIRTUAL_UNWIND_DATA_EH_FRAME,
  DW_VIRTUAL_UNWIND_DATA_DEBUG_FRAME
} DwVirtualUnwindDataType;

#define DW_CIE_VERSION      3
#define DW_CIE_VERSION_MAX  4

typedef struct DwCommonInfoEntry 
{
  SymsAddr init_cfi_offset;
  SymsAddr init_cfi_size;
  SymsAddr end_offset;

  SymsUWord cie_id;
  SymsString augmentation;

  syms_bool is_aug_sized;
  syms_bool is_sig_frame;
  syms_bool have_abi_maker;

  U8 version;
  U8 fde_encoding;
  U8 lsda_encoding;
  U8 handler_encoding;

  S64 addr_size;
  S64 segsel_size;

  S64 code_align;
  U64 data_align;
  U64 ret_addr_reg;

  SymsAddr handler_ip;
} DwCommonInfoEntry;

typedef enum DwCfiRegisterType 
{
  DW_CFI_REGISTER_TYPE_UNDEF,
  DW_CFI_REGISTER_TYPE_SAME,
  DW_CFI_REGISTER_TYPE_CFAREL,
  DW_CFI_REGISTER_TYPE_REG,
  DW_CFI_REGISTER_TYPE_EXPR,
  DW_CFI_REGISTER_TYPE_VAL_EXPR,
  DW_CFI_REGISTER_TYPE_INVALID
} DwCfiRegisterType;

typedef union DwCfiRegValue 
{
  SymsSWord w;
  DwEncodedLocationExpr e;
} DwCfiRegValue;

static const dw_uint DW_CFA_COLUMN_REG = (DW_PRESERVED_REGS_MAX + 0);
static const dw_uint DW_CFA_COLUMN_OFF = (DW_PRESERVED_REGS_MAX + 1);

typedef struct DwCfiRow 
{
  DwCfiRegisterType type[DW_PRESERVED_REGS_MAX + 2];
  DwCfiRegValue value[DW_PRESERVED_REGS_MAX + 2];
} DwCfiRow;

#define DW_CFI_PROGRAM_STACK_MAX 128
typedef struct DwCfiProgram 
{
  DwCfiRow rules;

  DwCfiRow *frame;
  DwCfiRow stack[DW_CFI_PROGRAM_STACK_MAX];

  dw_uint reg_count;
  syms_bool setup_cfa;
  S64 ret_addr_regid;
  SymsAddr cfa;
} DwCfiProgram;

typedef struct DwFrameInfo 
{
  DwVirtualUnwindDataType source_type;
  SymsAddr eh_frame;
  SymsAddr image_base;
  DwArchInfo arch_info;
} DwFrameInfo;

typedef struct DwVirtualUnwind 
{
  DwCfiProgram program;
} DwVirtualUnwind;

typedef struct DwFrameDescEntry 
{
  /* NOTE(nick): Offset where FDE was read */
  SymsAddr data_off;

  /* NOTE(nick): Offset of CFI for this FDE */
  SymsAddr cfi_offset;

  /* NOTE(nick): Number of bytes that FDE occupies */
  U64 data_size;

  /* NOTE(nick): Number of bytes that CFI take up */
  U64 cfi_size;

  /* NOTE(nick): Address of first instruction in procedure, that FDE describes. */
  SymsAddr start_ip;

  /* NOTE(nick): Number of bytes that instructions occupy. */
  SymsAddr range_ip;

  /* NOTE(nick): Address of LSDA. */
  SymsAddr lsda_ip;
} DwFrameDescEntry;

typedef struct DwFrameDescEntryIter 
{
  DwFrameInfo *frame_info;
  DwBinRead secdata;
  DwCommonInfoEntry cie;
  SymsAddr cie_offset;
} DwFrameDescEntryIter;

DW_API syms_bool
dw_virtual_unwind_init(SymsImageType image_type, DwVirtualUnwind *context_out);

DW_API syms_bool
dw_virtual_unwind_frame(DwVirtualUnwind *context,
            SymsArch arch,
            DwVirtualUnwindDataType sec_bytes_type,
            void *sec_bytes, SymsUMM sec_bytes_size,
            SymsAddr image_base,
            SymsAddr sec_bytes_base,
            void *memread_ctx, dw_memread_sig *memread,
            void *regread_ctx, dw_regread_sig *regread,
            void *regwrite_ctx, dw_regwrite_sig *regwrite);

SYMS_INTERNAL SymsAddr
dw_parse_pointer(DwFrameInfo *frame_info, DwBinRead *secdata, dw_uint encoding);

#endif /* SYMS_DWARF_UNWIND_INCLUDE_H */
