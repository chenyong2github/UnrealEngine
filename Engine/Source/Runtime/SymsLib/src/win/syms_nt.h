// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_WINDOWS_INCLUDE_H
#define SYMS_WINDOWS_INCLUDE_H

/******************************************************************************
 * File   : syms_nt.h                                                         *
 * Author : Nikita Smith                                                      *
 * Created: 2020/05/30                                                        *
 * Purpose: Declarations for NT executables                                   *
 ******************************************************************************/

#ifndef NT_API
#define NT_API SYMS_API
#endif

typedef struct SymsGUID {
  U32 data1;
  U16 data2;
  U16 data3;
  U8  data4[8];
} SymsGUID;

SYMS_COMPILER_ASSERT(sizeof(SymsGUID) == 16);
typedef struct SymsPeDebug {
  U32 characteristics;
  U32 time_stamp;
  U16 major_ver;
  U16 minor_ver;
  U32 type;
  U32 sizeof_data;
  U32 raw_data_rva;
  U32 raw_data_ptr;
} SymsPeDebug;

#pragma pack(push)

#define SYMS_DOS_MAGIC 0x5a4d
typedef struct SymsDosHeader {
  U16 e_magic;                 /* Magic number 'M' 'Z' */
  U16 e_cblp;                  /* Bytes on last page of file */
  U16 e_cp;                    /* Pages in file */
  U16 e_crlc;                  /* Relocations */
  U16 e_cparhdr;               /* Size of header in paragraphs */
  U16 e_minalloc;              /* Minimum extra paragraphs needed */
  U16 e_maxalloc;              /* Maximum extra paragraphs needed */
  U16 e_ss;                    /* Initial (relative) SS value */
  U16 e_sp;                    /* Initial SP value */
  U16 e_csum;                  /* Checksum */
  U16 e_ip;                    /* Initial IP value */
  U16 e_cs;                    /* Initial (relative) CS value */
  U16 e_lfarlc;                /* File address of relocation table */
  U16 e_ovno;                  /* Overlay number */
  U16 e_res[4];                /* Reserved words */
  U16 e_oemid;                 /* OEM identifier (for e_oeminfo) */
  U16 e_oeminfo;               /* OEM information; e_oemid specific */
  U16 e_res2[10];              /* Reserved words */
  S32 e_lfanew;                /* File address of the PE header */
} SymsDosHeader;

typedef enum {
  SYMS_NT_FILE_HEADER_MACHINE_UNKNOWN = 0x0,
  SYMS_NT_FILE_HEADER_MACHINE_X86 = 0x14c,
  SYMS_NT_FILE_HEADER_MACHINE_X64 = 0x8664,
  SYMS_NT_FILE_HEADER_MACHINE_AM33 = 0x1d3,       // NOTE(nick): Matsushita AM33
  SYMS_NT_FILE_HEADER_MACHINE_ARM = 0x1c0,
  SYMS_NT_FILE_HEADER_MACHINE_ARM64 = 0xaa64,
  SYMS_NT_FILE_HEADER_MACHINE_ARMNT = 0x1c4,
  SYMS_NT_FILE_HEADER_MACHINE_EBC = 0xebc,
  SYMS_NT_FILE_HEADER_MACHINE_I386 = 0x14c,
  SYMS_NT_FILE_HEADER_MACHINE_IA64 = 0x200,
  SYMS_NT_FILE_HEADER_MACHINE_M32R = 0x9041,      // NOTE(nick): Mitsubishi M32R little-endian
  SYMS_NT_FILE_HEADER_MACHINE_MIPS16 = 0x266,
  SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU = 0x366,    // NOTE(nick): MIPS with FPU
  SYMS_NT_FILE_HEADER_MACHINE_MIPSFPU16 = 0x466,  // NOTE(nick): MIPS16 with FPU
  SYMS_NT_FILE_HEADER_MACHINE_POWERPC = 0x1f0,    // NOTE(nick): Power PC little-endian
  SYMS_NT_FILE_HEADER_MACHINE_POWERPCFP = 0x1f1,  // NOTE(nick): Power PC with floating point support
  SYMS_NT_FILE_HEADER_MACHINE_R4000 = 0x166, 
  SYMS_NT_FILE_HEADER_MACHINE_RISCV32 = 0x5032,
  SYMS_NT_FILE_HEADER_MACHINE_RISCV64 = 0x5064,
  SYMS_NT_FILE_HEADER_MACHINE_RISCV128 = 0x5128,
  SYMS_NT_FILE_HEADER_MACHINE_SH3 = 0x1a2,        // NOTE(nick): Hitachi SH3
  SYMS_NT_FILE_HEADER_MACHINE_SH3DSP = 0x1a3,     // NOTE(nick): Hitachi SH3 DSP
  SYMS_NT_FILE_HEADER_MACHINE_SH4 = 0x1a6,        // NOTE(nick): Hitachi SH4
  SYMS_NT_FILE_HEADER_MACHINE_SH5 = 0x1a8,        // NOTE(nick): Hitachi SH5
  SYMS_NT_FILE_HEADER_MACHINE_THUMB = 0x1c2,
  SYMS_NT_FILE_HEADER_MACHINE_WCEMIPSV2 = 0x169   // NOTE(nick): MIPS little-endian WCE v2
} SymsNTFileHeaderMachineType_e;
typedef U16 SymsNTFileHeaderMachineType;

typedef enum {
  // NOTE(nick/MSDN): Image only, Windows CE, and Microsoft Windows NT and later. 
  // This indicates that the file does not contain base relocations and must therefore be loaded at its preferred base address. 
  // If the base address is not available, the loader reports an error. 
  // The default behavior of the linker is to strip base relocations from executable (EXE) files. 
  SYMS_NT_FILE_HEADER_FLAG_RELOCS_STRIPPED = 0x1,

  // NOTE(nick/MSDN): Image only. This indicates that the image file is valid and can be run. 
  // If this flag is not set, it indicates a linker error. 
  SYMS_NT_FILE_HEADER_FLAG_EXECUTABLE_IMAGE = 0x2,

  // NOTE(nick/MSDN): COFF line numbers have been removed. This flag is deprecated and should be zero.
  SYMS_NT_FILE_HEADER_FLAG_LINE_NUMS_STRIPPED = 0x4,

  // NOTE(nick/MSDN): COFF symbol table entries for local symbols have been removed. This flag is deprecated and should be zero.  
  SYMS_NT_FILE_HEADER_FLAG_LOCAL_SYM_STRIPPED = 0x8,

  // NOTE(nick/MSDN): Application can handle > 2-GB addresses. 
  SYMS_NT_FILE_HEADER_FLAG_LARGE_ADDRESS_AWARE = 0x20,

  // NOTE(nick/MSDN): Machine is based on a 32-bit-word architecture.
  SYMS_NT_FILE_HEADER_FLAG_32BIT_MACHINE = 0x100,

  // NOTE(nick/MSDN): Debugging information is removed from the image file. 
  SYMS_NT_FILE_HEADER_FLAG_DEBUG_STRIPPED = 0x200,

  // NOTE(nick/MSDN): If the image is on removable media, fully load it and copy it to the swap file. 
  SYMS_NT_FILE_HEADER_FLAG_REMOVABLE_RUN_FROM_SWAP = 0x400,

  // NOTE(nick/MSDN): If the image is on network media, fully load it and copy it to the swap file. 
  SYMS_NT_FILE_HEADER_FLAG_NET_RUN_FROM_SWAP = 0x800,

  // NOTE(nick/MSDN): The image file is a system file, not a user program. 
  SYMS_NT_FILE_HEADER_FLAG_SYSTEM = 0x1000,

  // NOTE(nick/MSDN): The image file is a dynamic-link library (DLL). 
  // Such files are considered executable files for almost all purposes, 
  // although they cannot be directly run. 
  SYMS_NT_FILE_HEADER_FLAG_DLL = 0x2000,

  // NOTE(nick/MSDN): The file should be run only on a uniprocessor machine. 
  SYMS_NT_FILE_HEADER_FLAG_UP_SYSTEM_ONLY = 0x4000
} SymsNTFileHeaderFlags_e;
typedef U16 SymsNTFileHeaderFlags;

typedef enum
{
  SYMS_NT_SUBSYSTEM_UNKNOWN = 0,
  SYMS_NT_SUBSYSTEM_NATIVE = 1,
  SYMS_NT_SUBSYSTEM_WINDOWS_GUI = 2,
  SYMS_NT_SUBSYSTEM_WINDOWS_CUI = 3,
  SYMS_NT_SUBSYSTEM_OS2_CUI = 5,
  SYMS_NT_SUBSYSTEM_POSIX_CUI = 7,
  SYMS_NT_SUBSYSTEM_NATIVE_WINDOWS = 8,
  SYMS_NT_SUBSYSTEM_WINDOWS_CE_GUI = 9,
  SYMS_NT_SUBSYSTEM_EFI_APPLICATION = 10,
  SYMS_NT_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11,
  SYMS_NT_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12,
  SYMS_NT_SUBSYSTEM_EFI_ROM = 13,
  SYMS_NT_SUBSYSTEM_XBOX = 14,
  SYMS_NT_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16
} SymsNTSubsystem_e;
typedef U16 SymsNTSubsystem;

typedef enum
{
  SYMS_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA       = 0x20,
  SYMS_DLL_CHARACTERISTICS_DYNAMIC_BASE          = 0x40,
  SYMS_DLL_CHARACTERISTICS_FORCE_INTEGRITY       = 0x80,
  SYMS_DLL_CHARACTERISTICS_NX_COMPAT             = 0x100,
  SYMS_DLL_CHARACTERISTICS_NO_ISOLATION          = 0x200,
  SYMS_DLL_CHARACTERISTICS_NO_SEH                = 0x400,
  SYMS_DLL_CHARACTERISTICS_NO_BIND               = 0x800,
  SYMS_DLL_CHARACTERISTICS_APPCONTAINER          = 0x1000,
  SYMS_DLL_CHARACTERISTICS_WDM_DRIVER            = 0x2000,
  SYMS_DLL_CHARACTERISTICS_GUARD_CF              = 0x4000,
  SYMS_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000
} SymsDllCharacteristics_e;

typedef struct SymsNTSection {
  char Name[8];

  union {
    U32 physical_addr;
    U32 virtual_size;
  } misc;

  U32 virtual_addr;
  U32 sizeof_raw_data;
  U32 ptr_to_raw_data;
  U32 ptr_to_relocs;
  U32 ptr_to_linenumbers;
  U16 num_relocs;
  U16 num_lines;
  U32 characteristics;
} SymsNTSection;

typedef enum {
  SYMS_NT_DEBUG_DIR_UNKNOWN,
  SYMS_NT_DEBUG_DIR_COFF,
  SYMS_NT_DEBUG_DIR_CODEVIEW,
  SYMS_NT_DEBUG_DIR_FPO,
  SYMS_NT_DEBUG_DIR_MISC
} SymsNTDebugDirType;

typedef struct SymsNTDataDir {
  U32 rva;
  U32 len;
} SymsNTDataDir;

typedef struct SymsNTDebugDir {
  U32 characteristics;
  U32 time_stamp;
  U16 major_ver;
  U16 minor_ver;
  U32 type;
  U32 sizeof_data;
  U32 raw_data_rva;
  U32 raw_data_ptr;
} SymsNTDebugDir;

typedef enum {
  SYMS_NT_DATA_DIR_EXPORT = 0,
  SYMS_NT_DATA_DIR_IMPORT = 1,
  SYMS_NT_DATA_DIR_RESOURCES = 2,
  SYMS_NT_DATA_DIR_EXCEPTIONS = 3,
  SYMS_NT_DATA_DIR_CERT = 4,
  SYMS_NT_DATA_DIR_BASE_RELOC = 5,
  SYMS_NT_DATA_DIR_DEBUG = 6,
  SYMS_NT_DATA_DIR_ARCH = 7,
  SYMS_NT_DATA_DIR_GLOBAL_PTR = 8,
  SYMS_NT_DATA_DIR_TLS = 9,
  SYMS_NT_DATA_DIR_LOAD_CONFIG = 10,
  SYMS_NT_DATA_DIR_BOUND_IMPORT = 11,
  SYMS_NT_DATA_DIR_IMPORT_ADDR = 12,
  SYMS_NT_DATA_DIR_DELAY_IMPORT = 13,
  SYMS_NT_DATA_DIR_COM_DESCRIPTOR = 14,
  SYMS_NT_DATA_DIR_RESERVED = 15,

  SYMS_NT_DATA_DIR_MAX
} SymsNTDataDirType;

typedef enum {
  SYMS_CODEVIEW_SIG_V410  = 0x3930424e,
  SYMS_CODEVIEW_SIG_V500  = 0x3131424e,
  SYMS_CODEVIEW_SIG_PDB20 = 0x3031424e,
  SYMS_CODEVIEW_SIG_PDB70 = 0x53445352
} SymsCodeViewSig_e;
typedef U32 SymsCodeViewSig;

typedef struct SymsCodeViewHeaderPDB20 {
  U32 sig;

  // Offset in memory where debug info resides. If file is external this is 0
  U32 off;

  // Time when debug info was created (in seconds since 01.01.1970) 
  U32 time;

  // Initially set 1, and inceremented every time a PDB file is updated.
  U32 age;

  // Name of the file with the debug info 
  // char name[];
} SymsCodeViewHeaderPDB20;

typedef struct SymsCodeViewHeaderPDB70 {
  U32 sig;

  // This GUID must match the one from the debug info file.
  SymsGUID guid;

  // Initially set 1, and inceremented every time a PDB file is updated.
  U32 age;

  // Name of the file with the debug info 
  // char name[];
} SymsCodeViewHeaderPDB70;

#define SYMS_NT_FILE_HEADER_SIG 0x00004550u
typedef struct SymsNTFileHeader {
  SymsNTFileHeaderMachineType machine;
  U16 number_of_sections;
  U32 time_date_stamp;
  U32 pointer_to_symbol_table;
  U32 number_of_symbols;
  U16 size_of_optional_header;
  SymsNTFileHeaderFlags flags;
} SymsNTFileHeader;

typedef struct SymsNTOptionalHeader32 {
  U16 magic;
  U8 major_linker_version;
  U8 minor_linker_version;
  U32 sizeof_code;
  U32 sizeof_inited_data;
  U32 sizeof_uninited_data;
  U32 entry_point_va;
  U32 code_base;
  U32 data_base;
  U32 image_base;
  U32 section_alignment;
  U32 file_alignment;
  U16 major_os_ver;
  U16 minor_os_ver;
  U16 major_img_ver;
  U16 minor_img_ver;
  U16 major_subsystem_ver;
  U16 minor_subsystem_ver;
  U32 win32_version_value;
  U32 sizeof_image;
  U32 sizeof_headers;
  U32 check_sum;
  U16 subsystem;
  U16 dll_characteristics;
  U32 sizeof_stack_reserve;
  U32 sizeof_stack_commit;
  U32 sizeof_heap_reserve;
  U32 sizeof_heap_commit;
  U32 loader_flags;
  U32 number_of_rva_and_sizes;
  SymsNTDataDir dirs[SYMS_NT_DATA_DIR_MAX];
} SymsNTOptionalHeader32;

typedef struct SymsNTOptionalHeader64 {
  U16 magic;
  U8 major_linker_version;
  U8 minor_linker_version;
  U32 sizeof_code;
  U32 sizeof_inited_data;
  U32 sizeof_uninited_data;
  U32 entry_point_va;
  U32 code_base;
  U64 image_base;
  U32 section_alignment;
  U32 file_alignment;
  U16 major_os_ver;
  U16 minor_os_ver;
  U16 major_img_ver;
  U16 minor_img_ver;
  U16 major_subsystem_ver;
  U16 minor_subsystem_ver;
  U32 win32_version_value;
  U32 sizeof_image;
  U32 sizeof_headers;
  U32 check_sum;
  U16 subsystem;
  U16 dll_characteristics;
  U64 sizeof_stack_reserve;
  U64 sizeof_stack_commit;
  U64 sizeof_heap_reserve;
  U64 sizeof_heap_commit;
  U32 loader_flags;
  U32 number_of_rva_and_sizes;
  SymsNTDataDir dirs[SYMS_NT_DATA_DIR_MAX];
} SymsNTOptionalHeader64;

typedef struct SymsNTImageHeader32 {
  SymsNTFileHeader file_header;
  SymsNTOptionalHeader32 opt_header;
} SymsNTImageHeader32;

typedef struct SymsNTImageHeader64 {
  SymsNTFileHeader file_header;
  SymsNTOptionalHeader64 opt_header;
} SymsNTImageHeader64;

#define SYMS_NT_IMAGE_SECTION_HEADER_CODE                0x20ul
#define SYMS_NT_IMAGE_SECTION_HEADER_INITED_DATA         0x40ul
#define SYMS_NT_IMAGE_SECTION_HEADER_UNINITED_DATA       0x80ul
#define SYMS_NT_IMAGE_SECTION_HEADER_LNK_INFO            0x200ul
#define SYMS_NT_IMAGE_SECTION_HEADER_LNK_REMOVE          0x800ul
#define SYMS_NT_IMAGE_SECTION_HEADER_LNK_COMDAT          0x1000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_NO_DEFER_SPEC_EXC   0x4000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_GPREL               0x8000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_NOT_CACHED      0x04000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_NOT_PAGED       0x08000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_SHARED          0x10000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_EXECUTE         0x20000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_READ            0x40000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_WRITE           0x80000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_PURGEABLE       0x00020000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_LOCK            0x00040000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_MEM_PRELOAD         0x00080000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_DISCARDABLE         0x02000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_LNK_NRELOC_OVFL     0x01000000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_1BYTES        0x00100000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_2BYTES        0x00200000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_4BYTES        0x00300000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_8BYTES        0x00400000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_16BYTES       0x00500000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_32BYTES       0x00600000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_64BYTES       0x00700000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_128BYTES      0x00800000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_256BYTES      0x00900000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_512BYTES      0x00A00000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_1024BYTES     0x00B00000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_2048BYTES     0x00C00000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_4096BYTES     0x00D00000ul
#define SYMS_NT_IMAGE_SECTION_HEADER_ALIGN_8192BYTES     0x00E00000ul
typedef u32 SymsNTImageSectionHeaderFlags;

typedef U32 SymsNTImageSectionHeaderType;

typedef struct SymsNTImageSectionHeader {
  char name[8];

  union {
    U32 physical_address;
    U32 virtual_size;
  } u;

  U32 va;
  U32 sizeof_rawdata;
  U32 rawdata_ptr;
  U32 realloc_ptr;
  U32 linenumbers_ptr;
  U16 realloc_count;
  U16 linenumbers_count;
  U32 flags;
} SymsNTImageSectionHeader;

typedef struct SymsNTImageExportTable {
  U32 characteristics;
  U32 time_stamp;
  U16 major_version;
  U16 minor_version;
  U32 name;
  U32 base;
  U32 num_funcs;
  U32 num_names;
  U32 funcs_rva;
  U32 names_rva;
  U32 ordinals_rva;
} SymsNTImageExportTable;

typedef struct SymsNTPdataPacked {
  U32 rva_lo;
  U32 rva_hi;
  U32 uw_info_rva;
} SymsNTPdataPacked;
SYMS_COMPILER_ASSERT(sizeof(SymsNTPdataPacked) == 12);

typedef struct SymsNTPdata {
  SymsAddr lo;
  SymsAddr hi;
  SymsAddr uwinfo;
} SymsNTPdata;

// NOTE(nick): X(NAME, VALUE, NUM_CODES)
#define SYMS_NT_UWOP_LIST \
  X(SYMS_NT_OP_PUSH_NONVOL,      0, 1) \
  X(SYMS_NT_OP_ALLOC_LARGE,      1, 2) \
  X(SYMS_NT_OP_ALLOC_SMALL,      2, 1) \
  X(SYMS_NT_OP_SET_FPREG,        3, 1) \
  X(SYMS_NT_OP_SAVE_NONVOL,      4, 2) \
  X(SYMS_NT_OP_SAVE_NONVOL_FAR,  5, 3) \
  X(SYMS_NT_OP_EPILOG,           6, 2) \
  X(SYMS_NT_OP_SPARE_CODE,       7, 3) \
  X(SYMS_NT_OP_SAVE_XMM128,      8, 2) \
  X(SYMS_NT_OP_SAVE_XMM128_FAR,  9, 3) \
  X(SYMS_NT_OP_PUSH_MACHFRAME,  10, 1) \
  /* end of the list */

typedef enum {
#define X(NAME, VALUE, NUM_CODES) NAME = VALUE,
  SYMS_NT_UWOP_LIST
#undef X
  SYMS_NT_OP_COUNT
} SymsNTUnwindOp;

#define SYMS_NT_UNWIND_CODE_FLAGS_GET_OP_CODE(x) (((x) & 0x0f) >> 0x00)
#define SYMS_NT_UNWIND_CODE_FLAGS_GET_OP_INFO(x) (((x) & 0xf0) >> 0x04)
typedef union SymsNTUnwindCode {
  struct {
    U8 off;
    U8 flags;
  } u;
  U16 frame_off;
} SymsNTUnwindCode;
SYMS_COMPILER_ASSERT(sizeof(SymsNTUnwindCode) == 2);

enum {
  // NOTE(nick): Describe type of exception handler, never figured out what these mean.
  SYMS_NT_UNWIND_INFO_EHANDLER = (1 << 0),
  SYMS_NT_UNWIND_INFO_UHANDLER = (1 << 1),
  SYMS_NT_UNWIND_INFO_FHANDLER = SYMS_NT_UNWIND_INFO_EHANDLER|SYMS_NT_UNWIND_INFO_UHANDLER,

  // NOTE(nick): Last code of unwind info is actually a SymsNTPdataPacked and it contains address to next unwind info.
  SYMS_NT_UNWIND_INFO_CHAINED  = (1 << 2)
};
typedef U8 SymsNTUnwindInfoFlags;

#define SYMS_NT_UNWIND_INFO_GET_CODE_COUNT(codes_num)    (((codes_num) + 1) & ~1)

// NOTE(nick): Version of unwind info; from windows xp to windows 10 version is 1.
#define SYMS_NT_UNWIND_INFO_HEADER_GET_VERSION(x)        (((x) & 0x07) >> 0x00)

// NOTE(nick): Masks out flags, see SymsNTUnwindInfoFlags.
#define SYMS_NT_UNWIND_INFO_HEADER_GET_FLAGS(x)          (((x) & 0xf8) >> 0x03)

// NOTE(nick): Retrieves bits that indicate register kind.
#define SYMS_NT_UWNIND_INFO_FRAME_GET_REG(x)             (((x) & 0x0f) >> 0x00)

// NOTE(nick): Retrieves offset from register for stack frame.
#define SYMS_NT_UNWIND_INFO_FRAME_GET_OFF(x)             (((x) & 0xf0) >> 0x04)

typedef struct SymsNTUnwindInfo {
  U8 header;

  U8 prolog_size;

  U8 codes_num;

  U8 frame;

  //SymsNTUnwindCode codes[0];
} SymsNTUnwindInfo;
SYMS_COMPILER_ASSERT(sizeof(SymsNTUnwindInfo) == 4);

#define SYMS_NT_EXCEPTION_BREAKPOINT               0x80000003u
#define SYMS_NT_EXCEPTION_SINGLE_STEP              0x80000004u
#define SYMS_NT_EXCEPTION_LONG_JUMP                0x80000026u
#define SYMS_NT_EXCEPTION_ACCESS_VIOLATION         0xC0000005u
#define SYMS_NT_EXCEPTION_ARRAY_BOUNDS_EXCEEDED    0xC000008Cu
#define SYMS_NT_EXCEPTION_DATA_TYPE_MISALIGNMENT   0x80000002u
#define SYMS_NT_EXCEPTION_GUARD_PAGE_VIOLATION     0x80000001u
#define SYMS_NT_EXCEPTION_FLT_DENORMAL_OPERAND     0xC000008Du
#define SYMS_NT_EXCEPTION_FLT_DEVIDE_BY_ZERO       0xC000008Eu
#define SYMS_NT_EXCEPTION_FLT_INEXACT_RESULT       0xC000008Fu
#define SYMS_NT_EXCEPTION_FLT_INVALID_OPERATION    0xC0000090u
#define SYMS_NT_EXCEPTION_FLT_OVERFLOW             0xC0000091u
#define SYMS_NT_EXCEPTION_FLT_STACK_CHECK          0xC0000092u
#define SYMS_NT_EXCEPTION_FLT_UNDERFLOW            0xC0000093u
#define SYMS_NT_EXCEPTION_INT_DIVIDE_BY_ZERO       0xC0000094u
#define SYMS_NT_EXCEPTION_INT_OVERFLOW             0xC0000095u
#define SYMS_NT_EXCEPTION_PRIVILEGED_INSTRUCTION   0xC0000096u
#define SYMS_NT_EXCEPTION_ILLEGAL_INSTRUCTION      0xC000001Du
#define SYMS_NT_EXCEPTION_IN_PAGE_ERROR            0xC0000006u
#define SYMS_NT_EXCEPTION_INVALID_DISPOSITION      0xC0000026u
#define SYMS_NT_EXCEPTION_NONCONTINUABLE           0xC0000025u
#define SYMS_NT_EXCEPTION_STACK_OVERFLOW           0xC00000FDu
#define SYMS_NT_EXCEPTION_INVALID_HANDLE           0xC0000008u
#define SYMS_NT_EXCEPTION_UNWIND_CONSOLIDATE       0x80000029u
#define SYMS_NT_EXCEPTION_DLL_NOT_FOUND            0xC0000135u
#define SYMS_NT_EXCEPTION_ORDINAL_NOT_FOUND        0xC0000138u
#define SYMS_NT_EXCEPTION_ENTRY_POINT_NOT_FOUND    0xC0000139u
#define SYMS_NT_EXCEPTION_DLL_INIT_FAILED          0xC0000142u
#define SYMS_NT_EXCEPTION_CONTROL_C_EXIT           0xC000013Au
#define SYMS_NT_EXCEPTION_FLT_MULTIPLE_FAULTS      0xC00002B4u
#define SYMS_NT_EXCEPTION_FLT_MULTIPLE_TRAPS       0xC00002B5u
#define SYMS_NT_EXCEPTION_NAT_CONSUMPTION          0xC00002C9u
#define SYMS_NT_EXCEPTION_HEAP_CORRUPTION          0xC0000374u
#define SYMS_NT_EXCEPTION_STACK_BUFFER_OVERRUN     0xC0000409u
#define SYMS_NT_EXCEPTION_INVALID_CRUNTIME_PARAM   0xC0000417u
#define SYMS_NT_EXCEPTION_ASSERT_FAILURE           0xC0000420u
#define SYMS_NT_EXCEPTION_NO_MEMORY                0xC0000017u
#define SYMS_VC_EXCEPTION_THROW                    0xE06D7363u

#pragma pack(pop)

typedef struct SymsImageNT {
  SymsDosHeader *dos_header;
  SymsNTFileHeader *file_header;
  union {
    SymsNTOptionalHeader32 *header32;
    SymsNTOptionalHeader64 *header64;
    void *header;
  } u;
  SymsString pdb_path;
  U32 pdb_age;
  U32 pdb_time;
  SymsGUID pdb_guid;
  U32 pdata_count;
} SymsImageNT;

typedef struct SymsSectIterNT {
  struct SymsImage *img;
  U32 header_index;
  U32 header_count;
  SymsNTImageSectionHeader *headers;
} SymsSecIterNT;

SYMS_INTERNAL SymsErrorCode
syms_find_nearest_pdata(struct SymsInstance *instance, SymsAddr ip, SymsNTPdata *pdata_out);

SYMS_INTERNAL SymsNTPdata
syms_unpack_pdata(struct SymsInstance *instance, SymsNTPdataPacked *pdata);


NT_API syms_bool
syms_img_init_nt(struct SymsImage *img, void *img_data, SymsUWord img_size, SymsLoadImageFlags flags);

NT_API const char *
syms_get_nt_machine_str(u32 machine);

SYMS_INTERNAL SymsSecIterNT
syms_sec_iter_init_nt(struct SymsImage *img);

SYMS_INTERNAL syms_bool
syms_sec_iter_next_nt(SymsSecIterNT *iter, SymsNTImageSectionHeader *sec_out);

SYMS_INTERNAL SymsNTPdata
syms_unpack_pdata(struct SymsInstance *instance, struct SymsNTPdataPacked *pdata);

SYMS_INTERNAL SymsAddr
syms_get_rebase_nt(const SymsImageNT *nt, SymsImageHeaderClass header_class, SymsAddr base);

#endif
