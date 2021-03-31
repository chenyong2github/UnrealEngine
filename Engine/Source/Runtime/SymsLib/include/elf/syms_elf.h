// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_LINUX_INCLUDE_H
#define SYMS_LINUX_INCLUDE_H

#ifndef ELF_API
#define ELF_API SYMS_API
#endif

//  File   : syms_elf.h
//  Author : Nikita Smith
//  Created: 2020/05/30
//  Purpose: Declarations of ELF data structs and rountines

typedef U32 Syms_Elf32_Addr;
typedef U16 Syms_Elf32_Half;
typedef U32 Syms_Elf32_Off;
typedef S32 Syms_Elf32_Sword;
typedef U32 Syms_Elf32_Word;

typedef U64 Syms_Elf64_Addr;
typedef U16 Syms_Elf64_Half;
typedef S16 Syms_Elf64_SHalf;
typedef U64 Syms_Elf64_Off;
typedef S32 Syms_Elf64_Sword;
typedef U32 Syms_Elf64_Word;
typedef U64 Syms_Elf64_Xword;
typedef S64 Syms_Elf64_Sxword;

#define SYMS_ELF32_ST_BIND(i) ((i) >> 4)
#define SYMS_ELF32_ST_TYPE(i) ((i) & 0xF)
#define SYMS_ELF32_INFO(b, t) (((b) << 4) + ((t) & 0xf))

#define SYMS_ELF64_ST_BIND(i)    ((i) >> 4)
#define SYMS_ELF64_ST_TYPE(i)    ((i) & 0xf)
#define SYMS_ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

#define SYMS_STN_UNDEF 0

#define SYMS_STB_LOCAL      0
#define SYMS_STB_GLOBAL     1
#define SYMS_STB_WEAK       2
#define SYMS_STB_LOOS      10
#define SYMS_STB_HIOS      12
#define SYMS_STB_LOPROC    13
#define SYMS_STB_HIPROC    15

#define SYMS_STT_NOTYPE     0
#define SYMS_STT_OBJECT     1
#define SYMS_STT_FUNC       2
#define SYMS_STT_SECTION    3
#define SYMS_STT_FILE       4
#define SYMS_STT_COMMON     5
#define SYMS_STT_TLS        6
#define SYMS_STT_LOOS      10
#define SYMS_STT_HIOS      12
#define SYMS_STT_LOPROC    13
#define SYMS_STT_HIPROC    15

#define SYMS_ELF32_ST_VISIBILITY(o) ((o) & 0x3)
#define SYMS_ELF64_ST_VISIBILITY(o) ((o) & 0x3)

#define SYMS_STV_DEFAULT    0
#define SYMS_STV_INTERNAL   1
#define SYMS_STV_HIDDEN     2
#define SYMS_STV_PROTECTED  3

// p_type
#define SYMS_PT_NONE        0u
#define SYMS_PT_LOAD        1u
#define SYMS_PT_DYNAMIC     2u
#define SYMS_PT_INTERP      3u
#define SYMS_PT_NOTE        4u
#define SYMS_PT_SHLIB       5u
#define SYMS_PT_PHDR        6u
#define SYMS_PT_LOPROC      0x70000000u
#define SYMS_PT_HIPROC      0x7fffffffu
// Specific to Sun
#define SYMS_PT_LOSUNW      0x6ffffffau
#define SYMS_PT_SUNWBSS     0x6ffffffbu
#define SYMS_PT_SUNWSTACK   0x6ffffffau
#define SYMS_PT_HISUNW      0x6fffffffu

// e_machine
#define SYMS_EM_NONE           0u
#define SYMS_EM_M32            1u
#define SYMS_EM_SPARC          2u
#define SYMS_EM_386            3u
#define SYMS_EM_68K            4u
#define SYMS_EM_88K            5u
#define SYMS_EM_IAMCU          6u
#define SYMS_EM_860            7u
#define SYMS_EM_MIPS           8u
#define SYMS_EM_S370           9u
#define SYMS_EM_MIPS_RS3_LE    10u
#define SYMS_EM_PARISC         15u
#define SYMS_EM_PPC_OLD        17u /* Old version of PowerPC. Deprecated */
#define SYMS_EM_SPARC32PLUS    18u /* Sun's "v8plus" */
#define SYMS_EM_960            19u
#define SYMS_EM_PPC            20u
#define SYMS_EM_PPC64          21u
#define SYMS_EM_S390           22u
#define SYMS_EM_SPU            23u
#define SYMS_EM_V800           36u
#define SYMS_EM_FR20           37u
#define SYMS_EM_RH32           38u
#define SYMS_EM_MCORE          39u
#define SYMS_EM_ARM            40u
#define SYMS_EM_SH             42u
#define SYMS_EM_IA_64          50u
#define SYMS_EM_X86_64         62u
#define SYMS_EM_AARCH64        183u
#define SYMS_EM_RISCV          243u

#define SYMS_ELFOSABI_NONE       0u
#define SYMS_ELFOSABI_SYSV       0u
#define SYMS_ELFOSABI_HPUX       1u
#define SYMS_ELFOSABI_NETBSD     2u
#define SYMS_ELFOSABI_GNU        3u
#define SYMS_ELFOSABI_HURD       4u
#define SYMS_ELFOSABI_86OPEN     5u
#define SYMS_ELFOSABI_SOLARIS    6u
#define SYMS_ELFOSABI_AIX        7u
#define SYMS_ELFOSABI_IRIX       8u
#define SYMS_ELFOSABI_FREEBSD    9u
#define SYMS_ELFOSABI_TRU64      10u
#define SYMS_ELFOSABI_MODESTO    11u
#define SYMS_ELFOSABI_OPENBSD    12u
#define SYMS_ELFOSABI_OPENVMS    13u
#define SYMS_ELFOSABI_NSK	       14u
#define SYMS_ELFOSABI_AROS	     15u
#define SYMS_ELFOSABI_FENIXOS    16u
#define SYMS_ELFOSABI_CLOUDABI   17u
#define SYMS_ELFOSABI_OPENVOS    18u
#define SYMS_ELFOSABI_ARM_FDPIC  65u
#define SYMS_ELFOSABI_ARM        97u
#define SYMS_ELFOSABI_STANDALONE 255u

/* sh_type */
#define SYMS_SHT_NULL     0
#define SYMS_SHT_PROGBITS 1
#define SYMS_SHT_SYMTAB   2
#define SYMS_SHT_STRTAB   3
#define SYMS_SHT_RELA     4
#define SYMS_SHT_HASH     5
#define SYMS_SHT_DYNAMIC  6
#define SYMS_SHT_NOTE     7
#define SYMS_SHT_NOBITS   8
#define SYMS_SHT_REL      9
#define SYMS_SHT_SHLIB    10
#define SYMS_SHT_DYNSYM   11
#define SYMS_SHT_INIT_ARRAY	    14		/* Array of ptrs to init functions */
#define SYMS_SHT_FINI_ARRAY	    15		/* Array of ptrs to finish functions */
#define SYMS_SHT_PREINIT_ARRAY  16		/* Array of ptrs to pre-init funcs */
#define SYMS_SHT_GROUP	        17		/* Section contains a section group */
#define SYMS_SHT_SYMTAB_SHNDX   18		/* Indices for SHN_XINDEX entries */

#define SYMS_SHT_GNU_INCREMENTAL_INPUTS 0x6fff4700   /* incremental build data */
#define SYMS_SHT_GNU_ATTRIBUTES 0x6ffffff5	/* Object attributes */
#define SYMS_SHT_GNU_HASH	0x6ffffff6	/* GNU style symbol hash table */
#define SYMS_SHT_GNU_LIBLIST	0x6ffffff7	/* List of prelink dependencies */

/* The next three section types are defined by Solaris, and are named
   SHT_SUNW*.  We use them in GNU code, so we also define SHT_GNU*
   versions.  */
#define SYMS_SHT_SUNW_verdef	0x6ffffffd	/* Versions defined by file */
#define SYMS_SHT_SUNW_verneed 0x6ffffffe	/* Versions needed by file */
#define SYMS_SHT_SUNW_versym	0x6fffffff	/* Symbol versions */

#define SYMS_SHT_GNU_verdef	  SYMS_SHT_SUNW_verdef
#define SYMS_SHT_GNU_verneed	SYMS_SHT_SUNW_verneed
#define SYMS_SHT_GNU_versym	  SYMS_SHT_SUNW_versym

#define SYMS_SHT_LOPROC   0x70000000
#define SYMS_SHT_HIPROC   0x7fffffff
#define SYMS_SHT_LOUSER   0x80000000
#define SYMS_SHT_HIUSER   0xffffffff

#define SYMS_SHN_UNDEF      0
#define SYMS_SHN_LORESERVE  0xff00
#define SYMS_SHN_LOPROC     0xff00
#define SYMS_SHN_BEFORE     0xff00
#define SYMS_SHN_AFTER      0xff01
#define SYMS_SHN_HIPROC     0xff1f
#define SYMS_SHN_ABS        0xfff1
#define SYMS_SHN_COMMON     0xfff2
#define SYMS_SHN_HIRESERVE  0xffff

#define SYMS_SHF_WRITE            0x1ul
#define SYMS_SHF_ALLOC            0x2ul
#define SYMS_SHF_EXECINSTR        0x4ul
#define SYMS_SHF_MERGE            0x10ul
#define SYMS_SHF_STRINGS          0x20ul
#define SYMS_SHF_INFO_LINK        0x40ul
#define SYMS_SHF_LINK_ORDER       0x80ul
#define SYMS_SHF_OS_NONCONFORMING 0x100ul
#define SYMS_SHF_GROUP            0x200ul
#define SYMS_SHF_TLS              0x400ul
#define SYMS_SHF_COMPRESSED       0x800ul
#define SYMS_SHF_ORDERED          0x4000000ul
#define SYMS_SHF_EXCLUDE          0x8000000ul
#define SYMS_SHF_GNU_BUILD_NOTE   0x100000ul
#define SYMS_SHF_GNU_MBIND        0x01000000ul

#define SYMS_SHF_MASKOS           0x0ff00000ul
#define SYMS_SHF_MASKPROC         0xf0000000ul

#define SYMS_ELFCLASSNONE 0
#define SYMS_ELFCLASS32   1
#define SYMS_ELFCLASS64   2

#define SYMS_ET_NONE    0u
// NOTE(nick): https://refspecs.linuxfoundation.org/elf/elf.pdf (page 15)
//
// File holds code and data for linking with other object files to 
// create an executable or a shared object file.
#define SYMS_ET_REL     1u
// File holds a program suitable for execution.
#define SYMS_ET_EXEC    2u
// File holds code and data suitable for linking in two contexts. First, the 
// link editor may process it with other relocatable and shared object file to
// create another object file. Second, the dynamic linker combines it with
// with an executable file and other shared objects to create a process image.
#define SYMS_ET_DYN     3u
// Contents of file is not specified (it's used for processor specific data).
#define SYMS_ET_CORE    4u
#define SYMS_ET_LOPROC  0xff00u
#define SYMS_ET_HIPROC  0xffffu

#if 0 // OribsOS flags for e_type
#define SYMS_SCE_EXEC    0xfe10u
#define SYMS_SCE_DYNEXEC 0xfe00u
#endif

typedef struct 
{
  Syms_Elf32_Word st_name;
  Syms_Elf32_Addr st_value;
  Syms_Elf32_Word st_size;
  unsigned char st_info;
  unsigned char st_other;
  Syms_Elf32_Half st_shndx;
} SymsElf32Sym;

typedef struct 
{
  Syms_Elf64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  Syms_Elf64_Half st_shndx;
  Syms_Elf64_Addr st_value;
  Syms_Elf64_Xword st_size;
} SymsElf64Sym;

typedef struct SymsElfShdr32 
{
  U32 sh_name;
  U32 sh_type;
  U32 sh_flags;
  U32 sh_addr;
  U32 sh_offset;
  U32 sh_size;
  U32 sh_link;
  U32 sh_info;
  U32 sh_addralign;
  U32 sh_entsize;
} SymsElfShdr32;

typedef struct SymsElfShdr64 
{
  U32 sh_name;		// Section name, index in string tbl 
  U32 sh_type;		// Type of section 
  U64 sh_flags;		// Miscellaneous section attributes 
  U64 sh_addr;		// Section virtual addr at execution 
  U64 sh_offset;	// Section file offset 
  U64 sh_size;		// Size of section in bytes 
  U32 sh_link;		// Index of another section 
  U32 sh_info;		// Additional section information 
  U64 sh_addralign;	// Section alignment 
  U64 sh_entsize;	// Entry size if section holds table 
} SymsElfShdr64;

typedef struct SymsElfPhdr32 
{
  U32	p_type;
  U32	p_offset;
  U32	p_vaddr;
  U32	p_paddr;
  U32	p_filesz;
  U32	p_memsz;
  U32	p_flags;
  U32	p_align;
} SymsElfPhdr32;

typedef struct SymsElfPhdr64 
{
  U32 p_type;
  U32 p_flags;
  U64 p_offset;   // Segment file offset 
  U64 p_vaddr;    // Segment virtual address 
  U64 p_paddr;    // Segment physical address 
  U64 p_filesz;   // Segment size in file 
  U64 p_memsz;    // Segment size in memory 
  U64 p_align;    // Segment alignment, file & memory
} SymsElfPhdr64;

#define SYMS_EI_MAG0    0
#define SYMS_EI_MAG1    1
#define SYMS_EI_MAG2    2
#define SYMS_EI_MAG3    3
#define SYMS_EI_CLASS   4
#define SYMS_EI_DATA    5
#define SYMS_EI_VERSION 6
#define SYMS_EI_PAD     7
#define SYMS_EI_NIDENT  16

#define SYMS_ELF_SIG    0x464c457f

#define SYMS_EV_NONE    0
#define SYMS_EV_CURRENT 1

typedef struct SymsElf32 
{
  U8 e_ident[SYMS_EI_NIDENT];
  U16 e_type;
  U16 e_machine;
  U16 e_version;
  Syms_Elf32_Addr e_entry;
  Syms_Elf32_Off e_phoff;
  Syms_Elf32_Off e_shoff;
  U32 e_flags;
  U16 e_ehsize;
  U16 e_phentsize;
  U16 e_phnum;
  U16 e_shentsize;
  U16 e_shnum;
  U16 e_shstrndx;
} SymsElf32;

typedef struct SymsElf64 
{
  U8 e_ident[SYMS_EI_NIDENT];
  U16 e_type;
  U16 e_machine;
  U16 e_version;
  Syms_Elf64_Addr e_entry;
  Syms_Elf64_Off e_phoff;
  Syms_Elf64_Off e_shoff;
  U32 e_flags;
  U16 e_ehsize;
  U16 e_phentsize;
  U16 e_phnum;
  U16 e_shentsize;
  U16 e_shnum;
  U16 e_shstrndx;
} SymsElf64;

// ----------------------------------------------------------------------------

typedef struct SymsImageElf 
{
  SymsAddr sh_name_lo;
  SymsAddr sh_name_hi;
  union {
    SymsElf32 *header32;
    SymsElf64 *header64;
  } u;
} SymsImageElf;

typedef struct SymsSecIterElf 
{
  struct SymsImage *image;
  SymsAddr sh_name_lo;
  SymsAddr sh_name_hi;
  U32 header_index;
  U32 header_count;
  SymsBuffer headers;
} SymsSecIterElf;

typedef struct SymsSymtabIter 
{
  U32 index;
  U32 count;
  SymsBuffer symtab_cursor;
  SymsBuffer strtab_cursor;
  SymsImageHeaderClass header_class;
} SymsSymtabIter;
SYMS_COMPILER_ASSERT(sizeof(SymsSymtabIter) <= sizeof(SymsModImpl));

typedef struct SymsSymtabEntry 
{
  SymsString name;
  SymsAddr value;
  SymsUWord size;
  U32 index;
  U8 bind;
  U8 type;
  U8 vis;
} SymsSymtabEntry;

SYMS_API syms_bool
syms_img_init_elf(struct SymsImage *img, void *img_data, SymsUWord img_size, SymsLoadImageFlags flags);

SYMS_INTERNAL SymsSecIterElf
syms_sec_iter_init_elf(struct SymsImage *img);

SYMS_INTERNAL syms_bool
syms_sec_iter_next_elf(SymsSecIterElf *iter, SymsElfShdr64 *sec_out);

SYMS_INTERNAL struct SymsProc
syms_proc_from_stt_func(SymsSymtabEntry *stt_func);

ELF_API syms_bool
syms_symtab_iter_init(struct SymsInstance *instance, SymsSymtabIter *iter);

ELF_API syms_bool
syms_symtab_iter_next(SymsSymtabIter *iter, SymsSymtabEntry *entry_out);

SYMS_INTERNAL SymsAddr
syms_get_rebase_elf(const SymsImageElf *elf, SymsImageHeaderClass header_class, SymsAddr old_base, SymsAddr base);

#endif
