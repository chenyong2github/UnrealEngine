// Copyright Epic Games, Inc. All Rights Reserved.
/* date = May 11th 2021 4:54 pm */

#ifndef SYMS_ELF_H
#define SYMS_ELF_H

//- rjf: p_type
typedef enum SYMS_ElfPKind{
  SYMS_ElfPKind_Null      = 0u,
  SYMS_ElfPKind_Load      = 1u,
  SYMS_ElfPKind_Dynamic   = 2u,
  SYMS_ElfPKind_Interp    = 3u,
  SYMS_ElfPKind_Note      = 4u,
  SYMS_ElfPKind_SHLib     = 5u,
  SYMS_ElfPKind_PHDR      = 6u,
  SYMS_ElfPKind_LowProc   = 0x70000000u,
  SYMS_ElfPKind_HighProc  = 0x7fffffffu,
  
  //- specific to Sun
  SYMS_ElfPKind_LowSunW   = 0x6ffffffau,
  SYMS_ElfPKind_SunWBSS   = 0x6ffffffbu,
  SYMS_ElfPKind_SunWStack = 0x6ffffffau,
  SYMS_ElfPKind_HighSunW  = 0x6fffffffu,
}SYMS_ElfPKind;

//- rjf: e_machine
typedef enum SYMS_ElfMachineKind{
  SYMS_ElfMachineKind_NONE           = 0u, 
  SYMS_ElfMachineKind_M32            = 1u, 
  SYMS_ElfMachineKind_SPARC          = 2u, 
  SYMS_ElfMachineKind_386            = 3u, 
  SYMS_ElfMachineKind_68K            = 4u, 
  SYMS_ElfMachineKind_88K            = 5u, 
  SYMS_ElfMachineKind_IAMCU          = 6u, 
  SYMS_ElfMachineKind_860            = 7u, 
  SYMS_ElfMachineKind_MIPS           = 8u, 
  SYMS_ElfMachineKind_S370           = 9u, 
  SYMS_ElfMachineKind_MIPS_RS3_LE    = 10u, 
  SYMS_ElfMachineKind_PARISC         = 15u, 
  SYMS_ElfMachineKind_PPC_OLD        = 17u, // nick: Old version of PowerPC. Deprecated
  SYMS_ElfMachineKind_SPARC32PLUS    = 18u, // nick: Sun's "v8plus"
  SYMS_ElfMachineKind_960            = 19u, 
  SYMS_ElfMachineKind_PPC            = 20u, 
  SYMS_ElfMachineKind_PPC64          = 21u, 
  SYMS_ElfMachineKind_S390           = 22u, 
  SYMS_ElfMachineKind_SPU            = 23u, 
  SYMS_ElfMachineKind_V800           = 36u, 
  SYMS_ElfMachineKind_FR20           = 37u, 
  SYMS_ElfMachineKind_RH32           = 38u, 
  SYMS_ElfMachineKind_MCORE          = 39u, 
  SYMS_ElfMachineKind_ARM            = 40u, 
  SYMS_ElfMachineKind_SH             = 42u, 
  SYMS_ElfMachineKind_IA_64          = 50u, 
  SYMS_ElfMachineKind_X86_64         = 62u, 
  SYMS_ElfMachineKind_AARCH64        = 183u, 
  SYMS_ElfMachineKind_RISCV          = 243u, 
}SYMS_ElfMachineKind;

typedef enum SYMS_ElfClass{
  SYMS_ElfClass_None = 0,
  SYMS_ElfClass_32   = 1,
  SYMS_ElfClass_64   = 2,
}SYMS_ElfClass;

typedef enum SYMS_ElfIdentifier{
  SYMS_ElfIdentifier_MAG0    = 0,
  SYMS_ElfIdentifier_MAG1    = 1,
  SYMS_ElfIdentifier_MAG2    = 2,
  SYMS_ElfIdentifier_MAG3    = 3,
  SYMS_ElfIdentifier_CLASS   = 4,
  SYMS_ElfIdentifier_DATA    = 5,
  SYMS_ElfIdentifier_VERSION = 6,
  SYMS_ElfIdentifier_PAD     = 7,
  SYMS_ElfIdentifier_NINDENT = 16,
}SYMS_ElfIdentifier;

typedef struct SYMS_ElfEhdr64 SYMS_ElfEhdr64;
struct SYMS_ElfEhdr64
{
  SYMS_U8 e_ident[SYMS_ElfIdentifier_NINDENT];
  SYMS_U16 e_type;
  SYMS_U16 e_machine;
  SYMS_U32 e_version;
  SYMS_U64 e_entry;
  SYMS_U64 e_phoff;
  SYMS_U64 e_shoff;
  SYMS_U32 e_flags;
  SYMS_U16 e_ehsize;
  SYMS_U16 e_phentsize;
  SYMS_U16 e_phnum;
  SYMS_U16 e_shentsize;
  SYMS_U16 e_shnum;
  SYMS_U16 e_shstrndx;
};

typedef struct SYMS_ElfEhdr32 SYMS_ElfEhdr32;
struct SYMS_ElfEhdr32
{
  SYMS_U8 e_ident[SYMS_ElfIdentifier_NINDENT];
  SYMS_U16 e_type;
  SYMS_U16 e_machine;
  SYMS_U32 e_version;
  SYMS_U32 e_entry;
  SYMS_U32 e_phoff;
  SYMS_U32 e_shoff;
  SYMS_U32 e_flags;
  SYMS_U16 e_ehsize;
  SYMS_U16 e_phentsize;
  SYMS_U16 e_phnum;
  SYMS_U16 e_shentsize;
  SYMS_U16 e_shnum;
  SYMS_U16 e_shstrndx;
};

typedef struct SYMS_ElfShdr64 SYMS_ElfShdr64;
struct SYMS_ElfShdr64
{
  SYMS_U32 sh_name;
  SYMS_U32 sh_type;
  SYMS_U64 sh_flags;
  SYMS_U64 sh_addr;
  SYMS_U64 sh_offset;
  SYMS_U64 sh_size;
  SYMS_U32 sh_link;
  SYMS_U32 sh_info;
  SYMS_U64 sh_addralign;
  SYMS_U64 sh_entsize;
};

typedef struct SYMS_ElfShdr32 SYMS_ElfShdr32;
struct SYMS_ElfShdr32
{
  SYMS_U32 sh_name;
  SYMS_U32 sh_type;
  SYMS_U32 sh_flags;
  SYMS_U32 sh_addr;
  SYMS_U32 sh_offset;
  SYMS_U32 sh_size;
  SYMS_U32 sh_link;
  SYMS_U32 sh_info;
  SYMS_U32 sh_addralign;
  SYMS_U32 sh_entsize;
};

typedef struct SYMS_ElfPhdr64 SYMS_ElfPhdr64;
struct SYMS_ElfPhdr64
{
  SYMS_U32 p_type;
  SYMS_U32 p_flags;
  SYMS_U64 p_offset;
  SYMS_U64 p_vaddr;
  SYMS_U64 p_paddr;
  SYMS_U64 p_filesz;
  SYMS_U64 p_memsz;
  SYMS_U64 p_align;
};

typedef struct SYMS_ElfPhdr32 SYMS_ElfPhdr32;
struct SYMS_ElfPhdr32
{
  SYMS_U32 p_type;
  SYMS_U32 p_offset;
  SYMS_U32 p_vaddr;
  SYMS_U32 p_paddr;
  SYMS_U32 p_filesz;
  SYMS_U32 p_memsz;
  SYMS_U32 p_flags;
  SYMS_U32 p_align;
};

typedef enum SYMS_ElfSectionCode{
  SYMS_ElfSectionCode_NULL                   = 0,
  SYMS_ElfSectionCode_PROGBITS               = 1,
  SYMS_ElfSectionCode_SYMTAB                 = 2,
  SYMS_ElfSectionCode_STRTAB                 = 3,
  SYMS_ElfSectionCode_RELA                   = 4,
  SYMS_ElfSectionCode_HASH                   = 5,
  SYMS_ElfSectionCode_DYNAMIC                = 6,
  SYMS_ElfSectionCode_NOTE                   = 7,
  SYMS_ElfSectionCode_NOBITS                 = 8,
  SYMS_ElfSectionCode_REL                    = 9,
  SYMS_ElfSectionCode_SHLIB                  = 10,
  SYMS_ElfSectionCode_DYNSYM                 = 11,
  SYMS_ElfSectionCode_INIT_ARRAY	           = 14,                // nick: Array of ptrs to init functions
  SYMS_ElfSectionCode_FINI_ARRAY	           = 15,                // nick: Array of ptrs to finish functions
  SYMS_ElfSectionCode_PREINIT_ARRAY          = 16,                // nick: Array of ptrs to pre-init funcs
  SYMS_ElfSectionCode_GROUP	                = 17,                // nick: Section contains a section group
  SYMS_ElfSectionCode_SYMTAB_SHNDX           = 18,                // nick: Indices for SHN_XINDEX entries
  SYMS_ElfSectionCode_GNU_INCREMENTAL_INPUTS = 0x6fff4700,        // nick: incremental build data
  SYMS_ElfSectionCode_GNU_ATTRIBUTES         = 0x6ffffff5,        // nick: Object attributes
  SYMS_ElfSectionCode_GNU_HASH               = 0x6ffffff6,        // nick: GNU style symbol hash table
  SYMS_ElfSectionCode_GNU_LIBLIST	          = 0x6ffffff7,        // nick: List of prelink dependencies
  
  // NOTE(nick): The next three section types are defined by Solaris, and are
  // named SHT_SUNW*.  We use them in GNU code, so we also define SHT_GNU*
  // versions.
  SYMS_ElfSectionCode_SUNW_verdef	   = 0x6ffffffd,	// nick: Versions defined by file
  SYMS_ElfSectionCode_SUNW_verneed    = 0x6ffffffe,	// nick: Versions needed by file
  SYMS_ElfSectionCode_SUNW_versym	   = 0x6fffffff,	// nick: Symbol versions
  
  SYMS_ElfSectionCode_GNU_verdef	    = SYMS_ElfSectionCode_SUNW_verdef,
  SYMS_ElfSectionCode_GNU_verneed	   = SYMS_ElfSectionCode_SUNW_verneed,
  SYMS_ElfSectionCode_GNU_versym	    = SYMS_ElfSectionCode_SUNW_versym,
  
  SYMS_ElfSectionCode_LOPROC          = 0x70000000,
  SYMS_ElfSectionCode_HIPROC          = 0x7fffffff,
  SYMS_ElfSectionCode_LOUSER          = 0x80000000,
  SYMS_ElfSectionCode_HIUSER          = 0xffffffff,
}SYMS_ElfSectionCode;

enum{
  SYMS_ElfSectionFlag_WRITE            = 0x1,
  SYMS_ElfSectionFlag_ALLOC            = 0x2,
  SYMS_ElfSectionFlag_EXECINSTR        = 0x4,
  SYMS_ElfSectionFlag_MERGE            = 0x10,
  SYMS_ElfSectionFlag_STRINGS          = 0x20,
  SYMS_ElfSectionFlag_INFO_LINK        = 0x40,
  SYMS_ElfSectionFlag_LINK_ORDER       = 0x80,
  SYMS_ElfSectionFlag_OS_NONCONFORMING = 0x100,
  SYMS_ElfSectionFlag_GROUP            = 0x200,
  SYMS_ElfSectionFlag_TLS              = 0x400,
  SYMS_ElfSectionFlag_MASKOS           = 0xff0000,
  SYMS_ElfSectionFlag_AMD64_LARGE      = 0x10000000,
  SYMS_ElfSectionFlag_ORDERED          = 0x40000000,
  SYMS_ElfSectionFlagT_EXCLUDE         = 0x80000000,
  SYMS_ElfSectionFlag_MASKPROC         = 0xf0000000
};

////////////////////////////////
//~ NOTE(allen): Auxiliary Vectors

// these appear in /proc/<pid>/auxv of a process, they are not in elf files

typedef enum SYMS_ElfAuxType{
  SYMS_ElfAuxType_NULL = 0,
  SYMS_ElfAuxType_PHDR = 3,   // program headers
  SYMS_ElfAuxType_PHENT = 4,  // size of a program header
  SYMS_ElfAuxType_PHNUM = 5,  // number of program headers
  SYMS_ElfAuxType_PAGESZ = 6, // system page size
  SYMS_ElfAuxType_BASE = 7,   // interpreter base address
  SYMS_ElfAuxType_FLAGS = 8,
  SYMS_ElfAuxType_ENTRY = 9,  // program entry point
  SYMS_ElfAuxType_UID = 11,
  SYMS_ElfAuxType_EUID = 12,
  SYMS_ElfAuxType_GID = 13,
  SYMS_ElfAuxType_EGID = 14,
  SYMS_ElfAuxType_PLATFORM = 15, // 'platform' as a string (TODO(allen): study)
  SYMS_ElfAuxType_HWCAP = 16,
  SYMS_ElfAuxType_CLKTCK = 17,
  SYMS_ElfAuxType_DCACHEBSIZE = 19,
  SYMS_ElfAuxType_ICACHEBSIZE = 20,
  SYMS_ElfAuxType_UCACHEBSIZE = 21,
  SYMS_ElfAuxType_IGNOREPPC = 22,
  SYMS_ElfAuxType_SECURE = 23,
  SYMS_ElfAuxType_BASE_PLATFORM = 24, // 'platform' as a string (different) (TODO(allen): study)
  SYMS_ElfAuxType_RANDOM = 25, // addres to 16 random bytes
  SYMS_ElfAuxType_HWCAP2 = 26,
  SYMS_ElfAuxType_EXECFN = 31, // file name of executable
  
  SYMS_ElfAuxType_SYSINFO = 32,
  SYMS_ElfAuxType_SYSINFO_EHDR = 33,
  
  // cool info about caches? (TODO(allen): study)
  SYMS_ElfAuxType_L1I_CACHESIZE = 40,
  SYMS_ElfAuxType_L1I_CACHEGEOMETRY = 41,
  SYMS_ElfAuxType_L1D_CACHESIZE = 42,
  SYMS_ElfAuxType_L1D_CACHEGEOMETRY = 43,
  SYMS_ElfAuxType_L2_CACHESIZE = 44,
  SYMS_ElfAuxType_L2_CACHEGEOMETRY = 45,
  SYMS_ElfAuxType_L3_CACHESIZE = 46,
  SYMS_ElfAuxType_L3_CACHEGEOMETRY = 47,
} SYMS_ElfAuxType;

typedef struct SYMS_ElfAuxv32{
  SYMS_U32 a_type;
  SYMS_U32 a_val;
} SYMS_ElfAuxv32;

typedef struct SYMS_ElfAuxv64{
  SYMS_U64 a_type;
  SYMS_U64 a_val;
} SYMS_ElfAuxv64;

////////////////////////////////
//~ NOTE(allen): Dynamic Structures

// these appear in the virtual address space of a process, they are not in elf files

typedef enum SYMS_ElfDynTag{
  SYMS_ElfDynTag_NULL = 0,
  SYMS_ElfDynTag_NEEDED = 1,
  SYMS_ElfDynTag_PLTRELSZ = 2,
  SYMS_ElfDynTag_PLTGOT = 3,
  SYMS_ElfDynTag_HASH = 4,
  SYMS_ElfDynTag_STRTAB = 5,
  SYMS_ElfDynTag_SYMTAB = 6,
  SYMS_ElfDynTag_RELA = 7,
  SYMS_ElfDynTag_RELASZ = 8,
  SYMS_ElfDynTag_RELAENT = 9,
  SYMS_ElfDynTag_STRSZ = 10,
  SYMS_ElfDynTag_SYMENT = 11,
  SYMS_ElfDynTag_INIT = 12,
  SYMS_ElfDynTag_FINI = 13,
  SYMS_ElfDynTag_SONAME = 14,
  SYMS_ElfDynTag_RPATH = 15,
  SYMS_ElfDynTag_SYMBOLIC = 16,
  SYMS_ElfDynTag_REL = 17,
  SYMS_ElfDynTag_RELSZ = 18,
  SYMS_ElfDynTag_RELENT = 19,
  SYMS_ElfDynTag_PLTREL = 20,
  SYMS_ElfDynTag_DEBUG = 21,
  SYMS_ElfDynTag_TEXTREL = 22,
  SYMS_ElfDynTag_JMPREL = 23,
  SYMS_ElfDynTag_BIND_NOW = 24,
  SYMS_ElfDynTag_INIT_ARRAY = 25,
  SYMS_ElfDynTag_FINI_ARRAY = 26,
  SYMS_ElfDynTag_INIT_ARRAYSZ = 27,
  SYMS_ElfDynTag_FINI_ARRAYSZ = 28,
  SYMS_ElfDynTag_RUNPATH = 29,
  SYMS_ElfDynTag_FLAGS = 30,
  SYMS_ElfDynTag_PREINIT_ARRAY = 32,
  SYMS_ElfDynTag_PREINIT_ARRAYSZ = 33,
  SYMS_ElfDynTag_SYMTAB_SHNDX = 34,
  SYMS_ElfDynTag_LOOS = 0x6000000D,
  SYMS_ElfDynTag_HIOS = 0x6ffff000,
  SYMS_ElfDynTag_LOPROC = 0x70000000,
  SYMS_ElfDynTag_HIPROC = 0x7fffffff,
} SYMS_ElfDynTag;

typedef struct SYMS_ElfDyn32{
  SYMS_U32 tag;
  SYMS_U32 val;
} SYMS_ElfDyn32;

typedef struct SYMS_ElfDyn64{
  SYMS_U64 tag;
  SYMS_U64 val;
} SYMS_ElfDyn64;

typedef struct SYMS_ElfLinkMap32{
  SYMS_U32 base;
  SYMS_U32 name;
  SYMS_U32 ld;
  SYMS_U32 next;
} SYMS_ElfLinkMap32;

typedef struct SYMS_ElfLinkMap64{
  SYMS_U64 base;
  SYMS_U64 name;
  SYMS_U64 ld;
  SYMS_U64 next;
} SYMS_ElfLinkMap64;

////////////////////////////////
//~ rjf: ELF Format Functions

SYMS_C_LINKAGE_BEGIN

// TODO(allen): avoid extra copies (use pointers where convenient)
SYMS_API SYMS_ElfEhdr64 syms_elf_ehdr64_from_ehdr32(SYMS_ElfEhdr32 h32);
SYMS_API SYMS_ElfShdr64 syms_elf_shdr64_from_shdr32(SYMS_ElfShdr32 h32);
SYMS_API SYMS_ElfPhdr64 syms_elf_phdr64_from_phdr32(SYMS_ElfPhdr32 h32);
// TODO(allen): auxv?
// TODO(allen): dyn?
// TODO(allen): linkmap?

// TODO(allen): promote to base
SYMS_API SYMS_U32 syms_elf_gnu_debuglink_crc32(SYMS_U32 crc, SYMS_String8 data);

SYMS_C_LINKAGE_END

#endif // SYMS_ELF_H
