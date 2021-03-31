// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_PUBLIC_INCLUDE_H
#define SYMS_PUBLIC_INCLUDE_H

#ifndef SYMS_API
#define SYMS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
  typedef unsigned __int64 SYMS_UINT64;
  typedef unsigned __int32 SYMS_UINT32;
#else
  #include <stdint.h>
  typedef uint64_t SYMS_UINT64;
  typedef uint32_t SYMS_UINT32;
#endif

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define SYMS_X86
#define SYMS_X64
#define SYMS_LITTLE_ENDIAN
#define SYMS_ADDR_MAX 0xffffffffffffffff
  typedef SYMS_UINT64 SymsAddr;
#elif defined(i386) || defined(__i386) || defined(__i386__) || defined(_X86_) || defined(_M_I86)
#define SYMS_X86
#define SYMS_LITTLE_ENDIAN
#define SYMS_ADDR_MAX 0xffffff
  typedef SYMS_UINT32 SymsAddr;
#elif defined(__arm__) || defined(_M_ARM) 
#define SYMS_ARM
#define SYMS_LITTLE_ENDIAN
  typedef uint32_t SymsAddr;
#define SYMS_ADDR_MAX 0xffffffff
#elif defined(__aarch64__)
#define SYMS_ARM64
#define SYMS_LITTLE_ENDIAN
  typedef uint64_t SymsAddr;
#define SYMS_ADDR_MAX 0xffffffffffffffff
#else
#error "arch not defined"
#endif

typedef SymsAddr SymsUMM;

typedef unsigned int  syms_uint;
typedef signed   int  syms_int;
typedef syms_int      syms_bool;
#define SYMS_INT_MAX  0xffffff
#define SYMS_UINT_MAX SYMS_INT_MAX

#define SYMS_INVALID_MOD_ID SYMS_INT_MAX
typedef syms_uint SymsModID; // Module IDentifier
typedef syms_uint SymsRegID;

typedef struct SymsInstance       SymsInstance;
typedef struct SymsArena          SymsArena;
typedef struct SymsArenaFrame     SymsArenaFrame;
typedef struct SymsBlockAllocator SymsBlockAllocator;

// -----------------------------------------------------------------------------

#define SYMS_VERSION_MAJOR  0
#define SYMS_VERSION_MINOR  7
#define SYMS_VERSION_STR    SYMS_TOSTR(SYMS_VERSION_MAJOR) "." SYMS_TOSTR(SYMS_VERSION_MINOR)
SYMS_API const char * syms_get_version(void);

#define SYMS_RESULT_OK(x)   ((x) == SYMS_ERR_OK)
#define SYMS_RESULT_FAIL(x) ((x) != SYMS_ERR_OK)
enum
{
  SYMS_ERR_INVALID_CODE_PATH = 0,
  SYMS_ERR_OK = 1,
  SYMS_ERR_NOMEM = 2,
  SYMS_ERR_MAYBE = 3,
  SYMS_ERR_INVAL = 4,
  SYMS_ERR_NODATA = 5,
  SYMS_ERR_NOIMG = 6,
  SYMS_ERR_NOSYM = 7,
  SYMS_ERR_NOLINETABLE = 8,
  SYMS_ERR_DEBUG_ABBREV = 9,
  SYMS_ERR_UNKNOWN_SYMBOL = 10,
  SYMS_ERR_INREAD = 11,
};
typedef syms_uint SymsErrorCode;

enum 
{
  SYMS_ARCH_NULL,
  SYMS_ARCH_ARM,
  SYMS_ARCH_ARM32,
  SYMS_ARCH_PPC,
  SYMS_ARCH_PPC64,
  SYMS_ARCH_IA64,
  SYMS_ARCH_X64,
  SYMS_ARCH_X86,
  SYMS_ARCH_COUNT
};
typedef syms_uint SymsArch;

typedef struct SymsString
{
  syms_uint len;
  const char *data;
} SymsString;

//
// Setup
//

SYMS_API SymsInstance *
syms_init_ex(SymsAddr rebase);
// Creates instance of this library, storage for it is allocated on the heap
// so it is your resposibilitiy to free memory with syms_quit.
//
// - First argument sets base for virtual addresses, image files that have non-zero base address
//   are remapped. Original base address can be accessed with syms_get_orig_rebase. Dynamic rebase
//   is supported, you can change base at runtime with syms_set_rebase.
//
// Upon success returns instance != NULL.

SYMS_API SymsInstance *
syms_init(void);

SYMS_API void
syms_quit(SymsInstance *instance);
// Frees memory allocated by instance.

// When you have image that was processed by dynamic linker set this flag.
#define SYMS_LOAD_IMAGE_FLAGS_FROM_MEMORY 0x1
typedef syms_uint SymsLoadImageFlags;

SYMS_API SymsErrorCode
syms_load_image(SymsInstance *instance, void *image_base, SymsUMM image_length, SymsLoadImageFlags flags); 
// Attaches image to instance and if format is windows executable now you can query
// pdb path with syms_get_pdb_path.

SYMS_API void
syms_free_image(SymsInstance *instance);
// Releases allocated memory for image.

// Defers build of lookup tables. 
//
// Building lookup tables is the most expensive part of 
// loading debug info (in terms of CPU cycles). Set this
// flag if you want to thread this step. A simple job queue
// will suffice. Retrieve job counter with "syms_get_module_build_count",
// then, in each worker thread, execute "syms_build_module"
// with module id that is less then the counter.
#define SYMS_LOAD_DEBUG_INFO_FLAGS_DEFER_BUILD_MODULE 0x1
typedef syms_uint SymsLoadDebugInfoFlags;

typedef struct
{
  // NOTE(nick): Path or file name of debug info file (e.g. test.pdb, debug_info.dwo).
  //
  // It is used to configure inputs for DWARF parser. Debug info itself does not
  // contain any identifying pieces of data. And so we must resort to using this string
  // to figure out inputs. In case where image already contains a section that conflicts
  // with defined file, syms will prioritizes file over section.
  const char *path;

  // NOTE(nick): Points to the first byte in file.
  void *base;

  // NOTE(nick): Number of bytes that can be read starting from "base" pointer.
  SymsUMM size;
} SymsFile;

// TODO(nick): File API
#if 0
SYMS_API SymsErrorCode
syms_load_debug_info(SymsInstance *instance, char **files, syms_uint file_count, SymsLoadDebugInfoFlags flags);
#endif

SYMS_API SymsErrorCode
syms_load_debug_info_ex(SymsInstance *instance, SymsFile *files, syms_uint file_count, SymsLoadDebugInfoFlags flags);
// Loads debug info from specified files. File data must be kept in memory until you destroy instance or
// free debug info.
//
// "files"     : Array of external debug info files. To get paths to external files use "syms_get_debug_info_paths".
// "file_count": Number of files.
// "flags"     : See "SymsLoadDebugInfoFlags"
//
// On success returns "syms_true".

SYMS_API void
syms_free_debug_info(SymsInstance *instance);
// Releases memory allocated for debug info.

enum 
{
  SYMS_IMAGE_NULL, // Image is present
  SYMS_IMAGE_NT,   // Win32, Win64
  SYMS_IMAGE_ELF   // Linux
};
typedef syms_uint SymsImageType;

// Image loader assings according to magic flags.
enum 
{
  SYMS_IMAGE_HEADER_CLASS_NULL, // Bit-type of image was not resolved detected
  SYMS_IMAGE_HEADER_CLASS_32,   // 32-bit
  SYMS_IMAGE_HEADER_CLASS_64    // 64-bit
};
typedef syms_uint SymsImageHeaderClass;

typedef char SymsImageImpl[512];
typedef struct SymsImage 
{
  SymsImageType type;
  SymsImageHeaderClass header_class;
  SymsArch arch;
  SymsLoadImageFlags flags;
  void *img_data;
  SymsUMM img_data_size;
  SymsAddr base_addr;
  SymsImageImpl impl;
  //char impl[128];
} SymsImage;

SYMS_API SymsImageType
syms_get_image_type(const SymsInstance *instance);

SYMS_API SymsImageHeaderClass
syms_get_image_header_class(const SymsInstance *instance);
// Get image header class that was read from binary data.

SYMS_API syms_uint
syms_get_image_size(const SymsInstance *instance);
// Get image bytes size.

typedef char SymsDebugFileIterImpl[600];
typedef struct
{
  syms_uint file_index;
  SymsInstance *instance;
  SymsDebugFileIterImpl impl;
} SymsDebugFileIter;

SYMS_API syms_bool
syms_debug_file_iter_init(SymsDebugFileIter *iter, SymsInstance *instance);

SYMS_API syms_bool
syms_debug_file_iter_next(SymsDebugFileIter *iter, SymsString *path_out);

SYMS_API syms_uint
syms_get_debug_file_count(SymsInstance *instance);

enum 
{
  SYMS_FORMAT_NULL,
  SYMS_FORMAT_PDB,      // Program Database
  SYMS_FORMAT_DWARF,    // Dwarf
  SYMS_FORMAT_ELFSYMTAB // Elf Symbol table
};
typedef syms_uint SymsFormatType;

typedef char SymsDebugInfoImpl[1024];
typedef struct SymsDebugInfo 
{
  SymsFormatType type;
  SymsDebugInfoImpl impl;
} SymsDebugInfo;

SYMS_API SymsFormatType
syms_get_symbol_type(const SymsInstance *instance);
// return value is indentifier for debug info type parser 

SYMS_API SymsImage *
syms_get_image(SymsInstance *instance);

SYMS_API SymsDebugInfo *
syms_get_debug_info(SymsInstance *instance);

SYMS_API void
syms_set_rebase(SymsInstance *instance, SymsAddr rebase);
// Dynamically rebase addresses in the instance.

SYMS_API SymsAddr
syms_get_rebase(const SymsInstance *instance);
// Get current rebase address.

SYMS_API SymsAddr
syms_get_orig_rebase(const SymsInstance *instance);
// Original base address that is extracted from image header.

SYMS_API syms_uint
syms_get_addr_size(SymsInstance *instance);
// Address size is based on architecture index encoded in image or pdb 

SYMS_API syms_uint
syms_get_addr_size_ex(SymsArch arch);

SYMS_API SymsArch
syms_get_arch(SymsInstance *instance);

SYMS_API syms_uint 
syms_get_proc_count(SymsInstance *instance);
// Count of infered procedures from debug info.

SYMS_API syms_uint
  syms_get_line_count(SymsInstance *instance);
// Counts total number of source lines loaded.

SYMS_API syms_uint
syms_get_module_build_count(SymsInstance *instance);
// Return value maximum number of modules that can be built. 
// if there are no modules to build return value is 0. 

SYMS_API SymsErrorCode
syms_build_module(SymsInstance *instance, SymsModID mod_id, SymsArena *arena);
// mod_id: Build lookup tables for module identifier (it is not thread safe to build
//         two modules with same identifier).
// 
// arena: Pointer to allocator.
//
// Errors:
//  SYMS_ERR_INVAL Invalid module identifier, nothing is built.
// 
// See also: syms_get_module_build_count
//

SYMS_API void
syms_set_rebase(SymsInstance *instance, SymsAddr rebase);
// Set base for virtual addresses.

SYMS_API SymsAddr
syms_get_rebase(const SymsInstance *instance);

SYMS_API SymsAddr
syms_get_orig_rebase(const SymsInstance *instance);
// Returns address at which compiler/linker based addresses for symbols.
//
// If image is compiled to be relocatable at runtime then original base is zero.

SYMS_API syms_bool
syms_match_image_with_debug_info(const SymsImage *image, const SymsDebugInfo *debug_info);

// --------------------------------------------------------------------------------

enum 
{
  SYMS_STRING_REF_TYPE_NULL,

  // NOTE(nick): Regular string
  SYMS_STRING_REF_TYPE_STR,

  // NOTE(nick): Reference into pdb file, for more info look into libs/pdb.h
  SYMS_STRING_REF_TYPE_PDB,

  // NOTE(nick): This is a hack for dwarf strings encoded in line table.
  // Directory and file name are stored in different locations, and requires
  // decoder, us, to merge them at runtime.
  SYMS_STRING_REF_TYPE_DW_PATH
};
typedef syms_uint SymsStringRefType;

typedef char SymsStringRefImpl[64];
typedef struct SymsStringRef 
{
  SymsStringRefType type;
  SymsStringRefImpl impl;
} SymsStringRef;

SYMS_API syms_uint
syms_strref_get_size(SymsInstance *instance, SymsStringRef *ref);
// Retrieves number of bytes string references takes up.

SYMS_API syms_uint
syms_read_strref(SymsInstance *instance, SymsStringRef *syms_ref, void *bf, syms_uint bf_max);
// Reads string reference to a buffer. If size of the buffer is smaller then
// size of the string reference this function will read as many bytes as possible.
//
// On success returns number of written bytes including '\0'.

SYMS_API char *
syms_read_strref_arena(SymsInstance *instance, SymsStringRef *ref, SymsArena *arena);

SYMS_API SymsString
syms_read_strref_str(SymsInstance *instance, SymsStringRef *ref, SymsArena *arena);

SYMS_API syms_bool
syms_string_cmp_strref(SymsInstance *instance, SymsString a, SymsStringRef b);

SYMS_API SymsStringRef
syms_string_ref_str(SymsString str);

// --------------------------------------------------------------------------------

enum
{
  SYMS_TYPE_ID_NULL,
  SYMS_TYPE_ID_PDB,
  SYMS_TYPE_ID_DW,
  SYMS_TYPE_ID_ELF,
  SYMS_TYPE_ID_BUILTIN,
  SYMS_TYPE_ID_COUNTER,
};
typedef syms_uint SymsTypeIDKind;

typedef char SymsTypeIDImpl[32];
typedef struct
{
  syms_uint kind;
  SymsTypeIDImpl impl;
} SymsTypeID;

typedef SymsTypeID SymsTypeRef;

// --------------------------------------------------------------------------------
// Type Map

typedef enum 
{
  SYMS_TYPE_NULL,
  /* Integers */
  SYMS_TYPE_INT8,
  SYMS_TYPE_INT16,
  SYMS_TYPE_INT32,
  SYMS_TYPE_INT64,
  SYMS_TYPE_INT128,
  SYMS_TYPE_INT256,
  SYMS_TYPE_INT512,
  SYMS_TYPE_UINT8,
  SYMS_TYPE_UINT16,
  SYMS_TYPE_UINT32,
  SYMS_TYPE_UINT64,
  SYMS_TYPE_UINT128,
  SYMS_TYPE_UINT256,
  SYMS_TYPE_UINT512,
  /* Floats */
  SYMS_TYPE_FLOAT16,
  SYMS_TYPE_FLOAT32,
  SYMS_TYPE_FLOAT32PP, /* NOTE(nick): Partial precision */
  SYMS_TYPE_FLOAT48,
  SYMS_TYPE_FLOAT64,
  SYMS_TYPE_FLOAT80,
  SYMS_TYPE_FLOAT128,
  SYMS_TYPE_CHAR,
  SYMS_TYPE_VOID,
  SYMS_TYPE_BOOL,
  SYMS_TYPE_PTR,
  SYMS_TYPE_ARR,
  SYMS_TYPE_ENUM,
  SYMS_TYPE_PROC,
  SYMS_TYPE_PROC_PARAM,
  SYMS_TYPE_TYPEDEF,
  SYMS_TYPE_STRUCT,
  SYMS_TYPE_UNION,
  SYMS_TYPE_CLASS,
  SYMS_TYPE_METHOD,
  SYMS_TYPE_VIRTUAL_TABLE,
  SYMS_TYPE_BASE_CLASS,
  SYMS_TYPE_BITFIELD,
  SYMS_TYPE_COMPLEX32,
  SYMS_TYPE_COMPLEX64,
  SYMS_TYPE_COMPLEX80,
  SYMS_TYPE_COMPLEX128,
  SYMS_TYPE_VARIADIC,

  SYMS_TYPE_STRING,
  SYMS_TYPE_WCHAR,

  SYMS_TYPE_ARRPTR,

  SYMS_TYPE_INVALID
} SymsTypeKind_e;
typedef syms_uint SymsTypeKind;

enum
{
  SYMS_TYPE_MDFR_NULL = 0,

  // NOTE(nick): atomic qualified type.
  SYMS_TYPE_MDFR_ATOMIC = (1 << 0),

  // NOTE(nick): const qualified type.
  SYMS_TYPE_MDFR_CONST = (1 << 1),

  // NOTE(nick): Same as const, but the data lives in read-only part of the RAM. 
  SYMS_TYPE_MDFR_IMMUTABLE = (1 << 2),

  // NOTE(nick): When a data type is marked for compiler to use as little space as possible.
  SYMS_TYPE_MDFR_PACKED = (1 << 3),

  // NOTE(nick): A reference to (lvalue of) an object of the type being modifid. 
  SYMS_TYPE_MDFR_REF = (1 << 4),

  // NOTE(nick): restrict qualified type.
  SYMS_TYPE_MDFR_RESTRICT = (1 << 5),

  // NOTE(nick): rvalue ref to an object of the type being modified (for example, in C++)
  SYMS_TYPE_MDFR_RVALUE_REF = (1 << 6),

  // NOTE(nick): shared qualified type. 
  SYMS_TYPE_MDFR_SHARED = (1 << 7),

  // NOTE(nick): volatile qualified type. 
  SYMS_TYPE_MDFR_VOLATILE = (1 << 8),

  // NOTE(nick): Assigned when memory values of this type can be displayed as text.
  SYMS_TYPE_MDFR_CHAR = (1 << 9),

  // NOTE(nick): set when nested UDT is encountered
  SYMS_TYPE_MDFR_NESTED = (1 << 10),

  SYMS_TYPE_MDFR_FWDREF = (1 << 11),

  SYMS_TYPE_MDFR_INVALID
};
typedef syms_uint SymsTypeModifier;

typedef struct
{
  syms_uint arg_count;            // Argumnet count
  SymsTypeID *arglist_id;  // Argument list
  SymsTypeID *return_id;   // Return type
} SymsTypeProc;

typedef struct
{
  syms_uint arg_count;            // Argument count
  SymsTypeID *arglist_id;  // Argument list
  SymsTypeID *return_id;   // Return type
  SymsTypeID *class_id;    // OOP Class
  SymsTypeID *this_id;     // OOP this
} SymsTypeMethod;


// Array counter, when array has more dimensions entry_id 
// points to next dimension
typedef struct
{
  SymsUMM entry_count;         
  SymsTypeID *entry_id;
} SymsTypeArray;

enum
{
  SYMS_MEMBER_TYPE_NULL,

  /* Member describes memory region inside UDT */
  SYMS_MEMBER_TYPE_DATA,

  /* Static data member of a UDT */
  SYMS_MEMBER_TYPE_STATIC_DATA,

  SYMS_MEMBER_TYPE_ENUM,

  /* Method info of a UDT  */
  SYMS_MEMBER_TYPE_METHOD,

  /* Type that describes layout of the virtual table.
   * Compiler might choose to store virtual table
   * at an address other than zero, so add
   * offset to the UDT base address when formatting
   * memory for it. */
  SYMS_MEMBER_TYPE_VTABLE,

  /* Base class location, use type id to format memory for it. */
  SYMS_MEMBER_TYPE_BASE_CLASS,

  /* Type that was declared inside the UDT */
  SYMS_MEMBER_TYPE_NESTED_TYPE
};
typedef syms_uint SymsMemberType;

enum
{
  SYMS_MEMBER_ACCESS_NULL,
  SYMS_MEMBER_ACCESS_PRIVATE,
  SYMS_MEMBER_ACCESS_PUBLIC,
  SYMS_MEMBER_ACCESS_PROTECTED
};
typedef syms_uint SymsMemberAccess;

enum
{
  SYMS_MEMBER_MODIFIER_NULL,

  /* No modifiers, just a regular method */
  SYMS_MEMBER_MODIFIER_VANILLA,

  /* Method marked with static */
  SYMS_MEMBER_MODIFIER_STATIC,

  /* Friend method */
  SYMS_MEMBER_MODIFIER_FRIEND,

  /* Virtual method */
  SYMS_MEMBER_MODIFIER_VIRTUAL,

  /* Virtual method provides only signature: void virtual foo(void) = 0; */
  SYMS_MEMBER_MODIFIER_PURE_INTRO,

  /* TODO: ??? */
  SYMS_MEMBER_MODIFIER_PURE_VIRTUAL,
  SYMS_MEMBER_MODIFIER_INTRO
};
typedef syms_uint SymsMemberModifier;

typedef enum
{
  SYMS_VTABLE_ENTRY_NULL,

  SYMS_VTABLE_ENTRY_PTR16,
  SYMS_VTABLE_ENTRY_PTR32,

  /* Pointer is composed out of two parts 
   * first is segment and second is offset.
   * Add them together to get final pointer. */
  SYMS_VTABLE_ENTRY_SEGOFF16, 
  SYMS_VTABLE_ENTRY_SEGOFF32,

  /* CodeView specific types, more in syms_codeview.h */
  SYMS_VTABLE_ENTRY_THIN,
  SYMS_VTABLE_ENTRY_OUTER,
  SYMS_VTABLE_ENTRY_META
} SymsVTableEntryType_e;
typedef syms_uint SymsVTableEntryType;

typedef struct
{
  /* Label given for the UDT member */
  char *label;

  /* Member type index */
  SymsTypeID *type_id;

  /* Type index for the field in the descriminated union */
  SymsMemberType type;

  /* Exports member access parameters when parsing class,
   * in other cases it is set to SYMS_MEMBER_ACCESS_NULL */
  SymsMemberAccess access;

  /* Exports member modifiers when parsing class,
   * in other cases it is set to null */
  SymsMemberModifier modifier;

  union
  {
    /* Virtual table format description. */
    struct
    {
      SymsAddr offset;            /* virtual table offset */
      syms_uint count;            /* number of entries */
      SymsVTableEntryType *base;  /* first entry */
    } vtab;

    struct
    {
      SymsUMM offset;
    } data;

    struct
    {
      SymsUMM value;
    } enum_value;

    struct
    {
      SymsUMM offset;
    } base_class;

    struct
    {
      SymsAddr vbaseoff;
    } method;
  } u;
} SymsTypeMember;

struct SymsTypeMemberNode
{
  SymsTypeMember data;
  struct SymsTypeMemberNode *next;
};

typedef struct
{
  syms_uint count;
  SymsTypeMember *base;
} SymsTypeUDT;

typedef struct
{
  SymsTypeID *base_type_id;
  syms_uint len;
  syms_uint pos;
} SymsTypeBitfield;

typedef struct
{
  char *label;
  SymsTypeID *type_id;
  SymsTypeModifier modifier;
  SymsTypeKind kind;
  syms_uint size;
  union
  {
    SymsTypeProc   proc;
    SymsTypeMethod method;
    SymsTypeArray  array;
    SymsTypeUDT    udt; // SYMS_TYPE_STRUCT, SYMS_TYPE_CLASS, SYMS_TYEPE_UNION
    SymsTypeBitfield bitfield;
  } u;
} SymsSymbolTypeInfo;

typedef struct
{
  char *name;      // section label
  SymsAddr off;    // In-file offset where section starts
  SymsAddr va;     // In-memory address where section starts
  SymsAddr length; // Number of bytes that make up section
} SymsSymbolSection;

typedef struct
{
  // [lo, hi]
  SymsAddr lo;
  SymsAddr hi;
} SymsSymbolLexicalBlock;

enum
{
  SYMS_PTR_MODE_NULL,
  SYMS_PTR_MODE_NORMAL, /* Regular pointer*/
  SYMS_PTR_MODE_LVREF,  /* L-value reference */
  SYMS_PTR_MODE_RVREF,  /* R-value reference */
  SYMS_PTR_MODE_MEM,    /* Pointer to a data member */
  SYMS_PTR_MODE_MFUNC   /* Pointer to member function */

};
typedef syms_uint SymsPointerMode;

typedef char SymsTypeImpl[128];
typedef struct SymsType 
{
  // NOTE(nick): A unique identifier for this type.
  SymsTypeID id;

  // NOTE(nick): A unique identifier for next type in the hierarchy chain.
  SymsTypeID next_id;

  SymsTypeKind kind;

  SymsTypeModifier modifier;

  // NOTE(nick): Size of the type itself.
  // If "kind" is SYMS_TYPE_ARR then this field will contain count of the elements.
  syms_uint size;

  syms_bool is_fwdref;

  // NOTE(nick): Name of a type.
  SymsStringRef name_ref;

  SymsStringRef decl_file;
  syms_uint decl_ln;

  union 
  {
    struct 
    {
      syms_uint arg_count;
      SymsTypeID arglist_type_id;
      SymsTypeID ret_type_id;
    } proc; // NOTE(nick): SYMS_TYPE_PROC

    struct
    {
      SymsPointerMode mode;
    } ptr;

    struct 
    {
      syms_uint arg_count;
      SymsTypeID ret_type_id;
      SymsTypeID class_type_id;
      SymsTypeID this_type_id;
      SymsTypeID arglist_type_id;
    } method;

    struct
    {
      SymsTypeID base_type_id;
      syms_uint len;
      syms_uint pos;
    } bitfield;

    SymsTypeID param_type;  // NOTE(nick): SYMS_TYPE_PROC_PARAM
  } u;

  SymsTypeImpl impl;
} SymsType;

typedef char SymsTypeIterImpl[64];
typedef struct SymsTypeIter 
{
  SymsDebugInfo *debug_info;
  SymsTypeIterImpl impl;
} SymsTypeIter;

SYMS_API syms_bool
syms_type_iter_init(SymsInstance *instance, SymsTypeIter *iter);

SYMS_API syms_bool
syms_type_iter_next(SymsTypeIter *iter, SymsTypeID *id_out);

SYMS_API syms_int
syms_typeid_cmp(const SymsTypeID *a, const SymsTypeID *b);

SYMS_API syms_bool
syms_infer_type(SymsInstance *instance, SymsTypeID type_id, SymsType *type_out);

SYMS_API void
syms_infer_builtin_type(SymsArch arch, SymsTypeKind kind, SymsType *type_out);

SYMS_API const char *
syms_get_type_kind_info(SymsTypeKind kind);

SYMS_API SymsType
syms_infer_base_type(SymsInstance *instance, const SymsType *type);

SYMS_API syms_uint
syms_get_type_size(SymsInstance *instance, SymsType *type);

SYMS_API SymsTypeID
syms_type_id_for_kind(SymsTypeKind kind);

SYMS_API SymsType
syms_make_ptr_type(SymsArch arch, const SymsTypeID *ref);

SYMS_API syms_bool
syms_type_from_name(SymsInstance *instance, const char *name, syms_uint name_size, SymsType *type_out);

SYMS_API SymsTypeID
syms_new_type(SymsInstance *instance);


//
// Symbols
//

enum
{
  SYMS_SYMBOL_NULL,
  SYMS_SYMBOL_SRCMAP,
  SYMS_SYMBOL_PROC,
  SYMS_SYMBOL_VAR_LOCAL,
  SYMS_SYMBOL_VAR,
  SYMS_SYMBOL_MODULE,
  SYMS_SYMBOL_SECTION,
  SYMS_SYMBOL_CONST_DATA,
  SYMS_SYMBOL_STATIC_DATA,
  SYMS_SYMBOL_TYPE_INFO,
  SYMS_SYMBOL_LEXICAL_BLOCK,
  SYMS_SYMBOL_TRAMPOLINE,
  // Special case for PDB that might have public symbol table,
  // but other symbols stripped. It exports range with label that
  // is mangled by MSVC. 
  SYMS_SYMBOL_PUBLIC
};
typedef syms_uint SymsSymbolKind;

enum 
{
  SYMS_CHECKSUM_NULL,
  SYMS_CHECKSUM_MD5,
  SYMS_CHECKSUM_SHA1,
  SYMS_CHECKSUM_SHA256
};
typedef syms_uint SymsChecksumType;

typedef struct 
{
  SymsAddr addr;
  SymsUMM ln;
  SymsUMM col;
  SymsUMM instructions_size;
  SymsUMM chksum_size;
  void *chksum;
  char *path;
  SymsChecksumType chksum_type; 
} SymsSymbolSrcmap;

enum 
{
  SYMS_VAR_FLAG_PARAM        = (1 << 0),
  SYMS_VAR_FLAG_COMPILER_GEN = (1 << 1),
  SYMS_VAR_FLAG_ALIASED      = (1 << 2),
  SYMS_VAR_FLAG_RETVAL       = (1 << 3),
  SYMS_VAR_FLAG_OPT_OUT      = (1 << 4),
  SYMS_VAR_FLAG_STATIC       = (1 << 5),
  SYMS_VAR_FLAG_GLOBAL       = (1 << 6)
};
typedef syms_uint SymsVarFlags;

typedef char SymsEncodedLocationImpl[64];
typedef struct
{
  SymsEncodedLocationImpl impl;
} SymsEncodedLocation;

typedef struct
{
  SymsAddr lo;
  SymsAddr hi;
} SymsRangeGap;

typedef struct SymsRangeData
{
  syms_uint gap_count;
  SymsRangeGap *gap_base;
  SymsEncodedLocation *encoded_va;
  SymsAddr lo;
  SymsAddr hi;
} SymsRangeData;

typedef struct SymsRangeNode
{
  SymsRangeData data;
  struct SymsRangeNode *next;
} SymsRangeNode;

typedef char SymsRangeToken[32];

typedef struct SymsSymbolVar
{
  SymsVarFlags flags;

  // user gieven/compiler generated label for variable
  char *label;

  // type might be missing and in that case it is set to null
  SymsTypeID *type_id;

  //SymsRangeIter range_iter;

  SymsRangeNode *range_list;

  // TODO: get rid of these:
  SymsEncodedLocation *encoded_va;
  struct SymsSymbolVar *next;
} SymsSymbolVar;

// Symbol for procedure exported from low-level SymsProc.
// Range is guaranteed to be present at all times and everything else is optional.
typedef struct SymsSymbolProc
{

  // Address of first instruction.
  SymsAddr range_lo;

  // Address of last instruction.
  SymsAddr range_hi;

  // Address past procedure prolog. A hint for debuggers
  // to avoid stepping through stack setup of procedures.
  SymsAddr dbg_start;

  // address before procedure epilog.
  SymsAddr dbg_end;

  // Label attached to symbol by user or compiler.
  char *label;

  syms_uint local_count;

  // Resolved inline chain.
  struct SymsSymbolProc *inline_chain;

  // Local variables with respect to lexical-blocks.
  SymsSymbolVar *locals;

  // Source file location
  SymsSymbolSrcmap srcmap;

  // Type id for symbol.
  SymsTypeID *type_id;
} SymsSymbolProc;

typedef struct
{
  SymsAddr jump_addr;
} SymsSymbolTrampoline;

typedef struct 
{
  char *name;
} SymsSymbolModule;

typedef struct
{
  char *label;
  SymsTypeID *type_id;
  SymsAddr addr;
} SymsSymbolStaticData;

typedef struct
{
  char *label;
  void *value;
  SymsTypeID *type_id;
  SymsUMM value_size;
} SymsSymbolConstData;

typedef struct SymsSymbol
{
  // Module ID where symbol is stored. When it is set to SYMS_INVALID_MOD_ID
  // symbol does not have a module.
  SymsModID mod_id;

  // Indicates which union field is valid.
  SymsSymbolKind kind;

  struct SymsSymbol *next;

  union
  {
    SymsSymbolLexicalBlock lexical_block;
    SymsSymbolVar         var_local;
    SymsSymbolProc        proc;
    SymsSymbolProc        public_proc;
    SymsSymbolModule      mod;
    SymsSymbolSection     sec;
    SymsSymbolSrcmap      source_map;
    SymsSymbolConstData   cdata;
    SymsSymbolStaticData  sdata;
    SymsSymbolTypeInfo    type_info;
    SymsSymbolTrampoline  trampoline;
    SymsSymbolSection     section;
  } u;
} SymsSymbol;

typedef struct SymsTypeIDNode
{
  SymsTypeID *data;
  struct SymsTypeIDNode *next; 
} SymsTypeIDNode;

typedef struct
{
  syms_uint count;
  SymsTypeIDNode *first;
} SymsTypeList;

// ----------------------------------------------------------------------------

enum
{
  SYMS_RANGE_NULL,
  SYMS_RANGE_PLAIN,
  SYMS_RANGE_IMPL
};
typedef syms_uint SymsRangeType;

typedef char SymsRangeImpl[64];
typedef struct SymsRange 
{
  SymsRangeType type;
  union 
  {
    struct 
    {
      SymsAddr lo;
      SymsAddr hi;
    } plain;
    SymsRangeImpl impl;
  } u;
} SymsRange;

typedef char SymsRangeIterImpl[64];
typedef struct SymsRangeIter
{
  SymsInstance *instance;
  SymsDebugInfo *debug_info;
  SymsRange range;
  SymsRangeIterImpl impl;
} SymsRangeIter;

SYMS_API syms_bool
syms_range_iter_init(SymsRangeIter *iter, SymsInstance *instance, SymsRange *range);

SYMS_API syms_bool
syms_range_iter_next(SymsRangeIter *iter, SymsAddr *lo_out, SymsAddr *hi_out);

// ----------------------------------------------------------------------------

enum 
{
  SYMS_LOCATION_NULL,
  SYMS_LOCATION_IMPLICIT,
  SYMS_LOCATION_INDIRECT,
  SYMS_LOCATION_VA
};
typedef syms_uint SymsLocationKind;

#define SYMS_LOCATION_IMPLICIT_VALUE_MAX 32
typedef struct SymsLocation 
{
  SymsLocationKind kind;
  union 
  {
    struct 
    {
      syms_uint len;
      char data[SYMS_LOCATION_IMPLICIT_VALUE_MAX];
    } implicit;

    struct 
    {
      syms_uint len;
      void const *data;
    } indirect;

    SymsAddr va;
  } u;
} SymsLocation;

#define SYMS_MEMREAD_SIG(name) SymsErrorCode name(void *user_context, SymsAddr va, void *buffer, syms_uint buffer_size)
typedef SYMS_MEMREAD_SIG(syms_memread_sig);

#define SYMS_REGREAD_SIG(name) syms_uint name(void *user_context, SymsArch arch, SymsRegID regid, void *buffer, syms_uint buffer_size)
typedef SYMS_REGREAD_SIG(syms_regread_sig);

#define SYMS_REGWRITE_SIG(name) syms_uint name(void *user_context, SymsArch arch, SymsRegID regid, void *buffer, syms_uint buffer_size)
typedef SYMS_REGWRITE_SIG(syms_regwrite_sig);

SYMS_API SymsErrorCode
syms_decode_location(SymsInstance *instance, SymsEncodedLocation *loc, void *regread_context, syms_regread_sig *regread_callback, void *memread_context, syms_memread_sig *memread_callback, SymsLocation *loc_out);

// ----------------------------------------------------------------------------

typedef char SymsModImpl[512];
typedef struct SymsMod 
{
  SymsModID id;
  SymsStringRef name;
  SymsAddr va;
  SymsUMM size;
  SymsModImpl impl;
} SymsMod;

typedef char SymsModIterImpl[128];
typedef struct SymsModIter 
{
  SymsInstance *instance;
  syms_uint index;
  SymsModIterImpl impl;
} SymsModIter;

SYMS_API syms_bool
syms_mod_iter_init(SymsInstance *instance, SymsModIter *iter);

SYMS_API syms_bool
syms_mod_iter_next(SymsModIter *iter, SymsMod *mod_out);

// ----------------------------------------------------------------------------

typedef struct SymsLine 
{
  // NOTE(nick): Address of the first instruction byte
  SymsAddr va;

  // NOTE(nick): Line number that contributed instructions
  syms_uint ln;

  // NOTE(nick): Column number
  syms_uint col;
} SymsLine;

typedef struct SymsSourceFile 
{
  SymsStringRef name;
  SymsChecksumType chksum_type;
  char chksum[32];
} SymsSourceFile;

typedef struct SymsSourceFileMap 
{
  SymsSourceFile file;
  SymsLine line;
  syms_uint instructions_size;
} SymsSourceFileMap;

typedef char SymsLineIterImpl[512];
typedef struct SymsLineIter 
{
  syms_int switched_file;
  syms_int has_line_count;
  syms_uint line_count;
  SymsInstance  *instance;
  SymsDebugInfo *debug_info;
  SymsArena     *arena;
  SymsSourceFile file;
  SymsLineIterImpl impl;
} SymsLineIter;

SYMS_API syms_bool
syms_line_iter_init(SymsInstance *instance, SymsLineIter *iter, SymsMod *mod);
// Initializes line iterator for given module.
//
// On success returns "syms_true".

SYMS_API syms_bool
syms_line_iter_next(SymsLineIter *iter, SymsLine *line_out);
// Fetches next line from the line table.
//
// On success returns "syms_true".

// ----------------------------------------------------------------------------

typedef struct SymsMethod
{
  SymsTypeID type_id;
  SymsStringRef name_ref;
  syms_uint vbaseoff;
} SymsMethod;

typedef struct SymsVirtualTable
{
  SymsAddr offset;
} SymsVirtualTable;

#define SYMS_MEMBER_ENUM_MAX 32
typedef struct SymsMember 
{
  SymsMemberAccess access;
  SymsMemberModifier modifier;
  SymsMemberType type;

  // NOTE(nick): Type of the member.
  SymsTypeID type_id;

  // NOTE(nick): Member name that was given by the user.
  SymsStringRef name_ref;

  union 
  {
    // NOTE(nick): When member owner is strcut/union/class this is offset of member.
    SymsUMM data_offset;

    SymsMethod method;

    SymsVirtualTable vtab;

    struct
    {
      SymsUMM offset;
    } base_class;

    // NOTE(nick): Format value according to type_id.
    char enum_value[SYMS_MEMBER_ENUM_MAX];
  } u;
} SymsMember;

typedef char SymsMemberIterImpl[256];
typedef struct SymsMemberIter 
{
  SymsInstance *instance;
  SymsDebugInfo *debug_info;
  SymsMemberIterImpl impl;
} SymsMemberIter;

SYMS_API syms_bool
syms_member_iter_init(SymsInstance *instance, SymsMemberIter *iter, SymsType *type);
// Parser for user defined types (struct, class, union, enum)

SYMS_API syms_bool
syms_member_iter_next(SymsMemberIter *iter, SymsMember *syms_member);

// ----------------------------------------------------------------------------

// TODO(nick): Merge this var with SymsVar
typedef struct SymsGlobal 
{
  SymsTypeID type_id;
  SymsStringRef name;
  SymsEncodedLocation encoded_va;
} SymsGlobal;
typedef struct SymsGlobal SymsLocalData;

typedef char SymsGlobalIterImpl[128];
typedef struct SymsGlobalIter 
{
  SymsDebugInfo *debug_info;
  SymsGlobalIterImpl impl;
} SymsGlobalIter;

SYMS_API syms_bool
syms_global_iter_init(SymsInstance *instance, SymsGlobalIter *iter);

SYMS_API syms_bool
syms_global_iter_next(SymsGlobalIter *iter, SymsGlobal *global_out);

typedef char SymsLocalDataIterImpl[64];
typedef struct SymsLocalDataIter 
{
  SymsModID mod_id;
  SymsDebugInfo *debug_info;
  SymsLocalDataIterImpl impl;
} SymsLocalDataIter;

SYMS_API syms_bool
syms_local_data_iter_init(SymsInstance *instance, SymsLocalDataIter *iter, SymsMod *mod);

SYMS_API syms_bool
  syms_local_data_iter_next(SymsLocalDataIter *iter, SymsLocalData *ldata_out);

SYMS_API syms_bool
syms_global_from_name(SymsInstance *instance, const char *name, syms_uint name_size, SymsGlobal *gvar_out);

// ----------------------------------------------------------------------------

typedef char SymsProcImpl[128];
typedef struct SymsProc 
{
  // NOTE(nick): Type ID of a procedure.
  SymsTypeID type_id;

  // NOTE(nick): Address of a first instruction.
  SymsAddr va;

  // NOTE(nick): Byte length of all instructions that a procedure encapsulates.
  syms_uint len;

  SymsAddr dbg_start_va;
  SymsAddr dbg_end_va;

  // NOTE(nick): Name ref of a procedure.
  SymsStringRef name_ref;

  SymsRange range;

  // NOTE(nick): For internal use only.
  SymsProcImpl impl;
} SymsProc;

typedef char SymsProcIterImpl[256];
typedef struct SymsProcIter 
{
  SymsInstance *instance;
  SymsProcIterImpl impl;
} SymsProcIter;

SYMS_API syms_bool
syms_proc_iter_init(SymsInstance *instance, SymsProcIter *iter, SymsMod *mod);

SYMS_API syms_bool
syms_proc_iter_next(SymsProcIter *iter, SymsProc *proc);

// ----------------------------------------------------------------------------

typedef char SymsScopeImpl[64];
typedef struct SymsScope
{
  SymsAddr inst_lo;
  SymsAddr inst_hi;
  SymsScopeImpl impl;
} SymsScope;

typedef struct SymsVar 
{
  // NOTE(nick): Type of a local variable.
  SymsTypeID type_id;

  SymsVarFlags flags;

  // NOTE(nick): Use "syms_decode_location" routine to decode it.
  SymsEncodedLocation encoded_va;

  SymsRange range;

  // NOTE(nick): Reference to name of a variable.
  SymsStringRef name_ref;
} SymsVar;

enum 
{
  SYMS_LOCAL_EXPORT_NULL,
  SYMS_LOCAL_EXPORT_SCOPE,
  SYMS_LOCAL_EXPORT_VAR,
  SYMS_LOCAL_EXPORT_SCOPE_END,
};
typedef syms_uint SymsLocalExportType;

typedef struct SymsLocalExport
{
  SymsLocalExportType type;
  union
  {
    SymsScope scope;
    SymsVar   var;
  } u;
} SymsLocalExport;

typedef char SymsLocalIterImpl[512];
typedef struct SymsLocalIter 
{
  SymsInstance *instance;
  SymsLocalIterImpl impl;
} SymsLocalIter;

SYMS_API syms_bool
syms_local_iter_init(SymsInstance *instance, SymsLocalIter *iter, SymsProc *proc, void *stack, syms_uint stack_size);


SYMS_API syms_bool
syms_local_iter_next(SymsLocalIter *local_iter, SymsLocalExport *export_out);
// Fetches next local-variable symbol.
//
// On successful decoding of a local-variable symbol it returns "syms_true".

// ----------------------------------------------------------------------------

typedef char SymsFileIterImpl[700];
typedef struct SymsFileIter 
{
  SymsDebugInfo *debug_info;
  SymsFileIterImpl impl;
} SymsFileIter;

SYMS_API syms_bool
syms_file_iter_init(SymsInstance *instance, SymsFileIter *iter);

SYMS_API syms_bool
syms_file_iter_next(SymsFileIter *iter, SymsStringRef *ref_out);

// ----------------------------------------------------------------------------

typedef char SymsArgIterImpl[128];
typedef struct SymsArgIter 
{
  SymsDebugInfo *debug_info;
  SymsArgIterImpl impl;
} SymsArgIter;

SYMS_API syms_bool
syms_arg_iter_init(SymsInstance *instance, SymsArgIter *iter, SymsType *proc_type);

SYMS_API syms_bool
syms_arg_iter_next(SymsArgIter *iter, SymsTypeID *type_id_out);


// ----------------------------------------------------------------------------

typedef struct SymsInlineSite 
{
  // inline site name
  SymsStringRef name;

  // type index of inline site
  SymsTypeID type_id;

  // address of first instruction in inlined block
  SymsAddr range_lo;
  // address where inlined block ends (includes last instruction)
  SymsAddr range_hi;

  // source file name where procedure is inlined
  SymsSourceFile call_file;
  // source file name where procedure is declare
  SymsSourceFile decl_file;
  // line number in source file where compiler inlined procedure
  syms_uint call_ln;
  // line number in source file inline site is declared
  syms_uint decl_ln;

  SymsAddr sort_index;

  SymsProc inlinee;
  SymsSourceFileMap src;
} SymsInlineSite;

typedef char SymsInlineIterImpl[512]; 
typedef struct SymsInlineIter 
{
  SymsInstance *instance;
  SymsDebugInfo *debug_info;
  SymsInlineIterImpl impl;
} SymsInlineIter;

SYMS_API syms_bool
syms_inline_iter_init(SymsInstance *instance, SymsInlineIter *inline_iter, SymsAddr pc);

SYMS_API syms_bool
syms_inline_iter_next(SymsInlineIter *inline_iter, SymsInlineSite *site_out);

// ----------------------------------------------------------------------------

#define SYMS_CONST_VALUE_MAX 32
typedef struct SymsConst 
{
  SymsTypeID type_id;
  SymsStringRef name;
  syms_uint value_len;
  char value[SYMS_CONST_VALUE_MAX];
} SymsConst;

typedef char SymsConstIterImpl[128];
typedef struct SymsConstIter 
{
  SymsDebugInfo *debug_info;
  SymsConstIterImpl impl;
} SymsConstIter;

SYMS_API syms_bool
syms_const_from_name(SymsInstance *instance, const char *name, syms_uint name_size, SymsConst *const_out);

SYMS_API syms_bool
  syms_const_iter_init(SymsInstance *instance, SymsConstIter *const_iter);

SYMS_API syms_bool
syms_const_iter_next(SymsConstIter *const_iter, SymsConst *const_out);

// ----------------------------------------------------------------------------

typedef char SymsSectionImpl[128];
typedef struct SymsSection 
{
  SymsString name;
  void *data;
  SymsUMM data_size;
  SymsAddr off;
  SymsAddr va;
  SymsSectionImpl impl;
} SymsSection;

typedef char SymsSecIterImpl[64];
typedef struct SymsSecIter 
{
  SymsImage *image;
  SymsSecIterImpl impl;
} SymsSecIter;

SYMS_API syms_bool
syms_sec_iter_init(SymsInstance *instance, SymsSecIter *iter);
// Initialize section iterator, on success return value is syms_true(1)
//
// On success returns "syms_true".

SYMS_API syms_bool
syms_sec_iter_next(SymsSecIter *iter, SymsSection *sec_out);
// Parses a section out of image.
// A shorthand for accessing sections of image by name.

// ----------------------------------------------------------------------------

typedef syms_uint SymsLn;
typedef syms_uint SymsCol;
typedef syms_uint SymsFileID;
typedef syms_uint SymsSourceMapID;
#define SYMS_INVALID_FILE_ID SYMS_INT_MAX

typedef struct SymsSourceMap
{
  SymsLn ln;
  SymsCol col;
  SymsFileID file;
} SymsSourceMap;

typedef struct SymsAddrMap
{
  SymsAddr addr;
  syms_uint id;
} SymsAddrMap;

typedef struct SymsLineTable
{
  syms_uint line_count;
  syms_uint line_max;
  syms_uint file_count;
  SymsAddrMap *addrs;
  SymsSourceMap *lines;
  SymsStringRef *files;
} SymsLineTable;

typedef struct
{
  syms_uint line_id;
  SymsSourceFileMap map;
} SymsLineTableQuery;

SYMS_API syms_bool
syms_line_table_map_va(SymsLineTable *lt, SymsAddr va, SymsSourceFileMap *query_out);

SYMS_API syms_bool
syms_line_table_map_va_ex(SymsLineTable *lt, SymsAddr va, SymsLineTableQuery *query_out);

SYMS_API syms_bool
syms_line_table_map_src(SymsLineTable *lt, SymsInstance *instance, SymsString filename, syms_uint ln, SymsSourceFileMap *map_out);

SYMS_API syms_bool
syms_src_to_va(SymsInstance *instance, char *filename, syms_uint filename_len, syms_uint ln, SymsSourceFileMap *map_out);

SYMS_API syms_bool
syms_va_to_src(SymsInstance *instance, SymsAddr va, SymsSourceFileMap *map_out);

// ----------------------------------------------------------------------------

SYMS_API syms_bool
syms_proc_from_name_2(SymsInstance *instance, const char *name, syms_uint name_size, SymsProc *proc_out, SymsModID *mod_out);
// Searches given module for procedure that matches name.
//
// On success returns "syms_true".

SYMS_API syms_bool
syms_proc_from_name(SymsInstance *instance, const char *name, syms_uint name_size, SymsProc *proc_out);
// Searches for all modules for procedure that matches name.
//
// On success returns "syms_true".

SYMS_API syms_bool
syms_proc_from_va(SymsInstance *instance, SymsAddr va, SymsProc *proc);

SYMS_API syms_bool
syms_proc_from_va_2(SymsInstance *instance, SymsAddr va, SymsProc *proc_out, SymsModID *mod_out);
// Searches for nearest procedure symbol for specified address.
//
// On success returns "syms_true".

SYMS_API syms_bool
syms_img_sec_from_name(SymsInstance *instance, SymsString name, SymsSection *sec_out);

SYMS_API syms_bool
syms_va_to_secoff(SymsInstance *instance, SymsAddr va, syms_uint *isec_out, syms_uint *off_out);

// ----------------------------------------------------------------------------

SYMS_API SymsSymbol *
syms_symbol_from_addr(SymsInstance *instance, SymsAddr addr, SymsArena *arena);
// Queries symbols that are at or near specified address.
// On successfull query result is a linked list of symbols.

SYMS_API SymsSymbol *
  syms_symbol_from_str(SymsInstance *instance, const char *str, SymsArena *arena);

SYMS_API SymsSymbol *
syms_symbol_from_str_ex(SymsInstance *instance, const char *str_ptr, syms_uint str_size, SymsArena *arena);
// Queries symbol that match specifed string.
// On successfull query result is a linked list of symbols.

SYMS_API const char *
syms_format_typeid(SymsInstance *instance, SymsTypeID *type_id, SymsArena *arena);
// Creates a printable representation from type identifier.

SYMS_API const char *
syms_format_type(SymsInstance *instance, SymsType *type, SymsArena *arena);

SYMS_API const char *
syms_format_symbol(SymsInstance *instance, SymsSymbol *sym, SymsArena *arena);
// Creates printable string for symbol.

SYMS_API const char *
syms_format_proc(SymsInstance *instance, SymsSymbolProc *proc, SymsArena *arena);

SYMS_API const char *
syms_format_method(SymsInstance *instance, SymsTypeMember *member, SymsArena *arena);

SYMS_API SymsErrorCode
syms_export_type(SymsInstance *instance, SymsTypeID type_id, SymsSymbolTypeInfo *type_out, SymsArena *arena);

enum
{
  SYMS_BINDATA_TYPE_NULL,
  SYMS_BINDATA_TYPE_NT32,
  SYMS_BINDATA_TYPE_NT64,
  SYMS_BINDATA_TYPE_ELF32,
  SYMS_BINDATA_TYPE_ELF64,
  SYMS_BINDATA_TYPE_DWARF,
  SYMS_BINDATA_TYPE_PDB,
  SYMS_BINDATA_TYPE_ELFSYM
};
typedef syms_uint syms_bindata_type;

SYMS_API syms_bindata_type
syms_examine_binary_data(void *buf, SymsUMM buf_size);

//
// Allocator
//

SYMS_API SymsArena *
syms_borrow_memory(SymsInstance *instance);

// TODO(nick): add parameter to configure virtual address reserve.
#if 0
syms_borrow_memory_ex(strcut SymsInstance *instance, SymsUMM reserve);
#endif

SYMS_API void
syms_return_memory(SymsInstance *instance, SymsArena **arena);

SYMS_API SymsUMM
syms_get_arena_size(SymsArena *arena);

//
//  
//

SYMS_API SymsSymbolTypeInfo *
syms_type_from_name_ex(SymsInstance *instance, const char *name, syms_uint name_size, SymsArena *arena);

SYMS_API SymsSymbolStaticData *
syms_global_from_name_ex(SymsInstance *instance, const char *name, syms_uint name_size, SymsArena *arena);

SYMS_API SymsSymbolConstData *
syms_const_from_name_ex(SymsInstance *instance, const char *name, syms_uint name_size, SymsArena *arena);

SYMS_API SymsSymbolProc *
syms_proc_from_addr_ex(SymsInstance *instance, SymsAddr va, SymsArena *arena);

SYMS_API SymsSymbolProc *
syms_proc_from_name_ex(SymsInstance *instance, const char *name, syms_uint name_size, SymsArena *arena);

SYMS_API SymsSymbolSection *
syms_img_sec_from_name_ex(SymsInstance *instance, const char *name, SymsArena *arena);

SYMS_API SymsErrorCode
syms_trampoline_from_ip(SymsInstance *instance, SymsAddr ip, SymsAddr *ip_out);

//
// Line table lookup
//

SYMS_API SymsSymbolSrcmap *
syms_src_to_va_arena(SymsInstance *instance, const char *path, syms_uint path_size, syms_uint ln, syms_uint col, SymsArena *arena);

SYMS_API SymsSymbolSrcmap *
syms_va_to_src_arena(SymsInstance *instance, SymsAddr va, SymsArena *arena);

//
//
//

typedef struct SymsModInfo
{
  SymsMod header;
  SymsBlockAllocator *procs;
  SymsBlockAllocator *rangemap;
  SymsLineTable       line_table;
} SymsModInfo;

SYMS_API SymsProc *
syms_mod_info_find_proc(SymsModInfo *m, syms_uint i);

SYMS_API SymsModInfo *
syms_mod_from_va(SymsInstance *instance, SymsAddr va);

//
// Registers
//

enum
{
  SYMS_REG_null
};

// Each register can be assigned a class.
// This provides a hint to the debugger as to how it might
// want to interpret it's contents. 
enum 
{
  SYMS_REG_CLASS_NULL,

  // Basic processor state (IP/PC/flags) 
  SYMS_REG_CLASS_STATE,

  // Integer/pointer/segment/etc
  SYMS_REG_CLASS_GPR,

  // Control register
  SYMS_REG_CLASS_CTRL,

  // Scalar floating point register.
  SYMS_REG_CLASS_FP,

  // Vector register (could be N ints/floats/etc)
  SYMS_REG_CLASS_VEC,

  SYMS_REG_CLASS_INVALID
};
typedef syms_uint SymsRegClass;

SYMS_API SymsTypeKind
syms_get_reg_type(SymsArch arch, SymsRegID regid);

SYMS_API const char *
syms_get_reg_str(SymsArch arch, SymsRegID regid);

SYMS_API syms_uint
reg_desc_count_from_arch(SymsArch arch);

SYMS_API SymsRegID
syms_regs_get_first_regid_ex(SymsArch arch);

SYMS_API SymsRegID
syms_regs_get_last_regid_ex(SymsArch arch);

SYMS_API SymsString
syms_regs_get_name_ex(SymsArch arch, SymsRegID id);

SYMS_API SymsTypeKind
syms_regs_get_type_ex(SymsArch arch, SymsRegID id);

SYMS_API SymsRegClass
syms_regs_get_reg_class_ex(SymsArch arch, SymsRegID id);

SYMS_API SymsRegID
syms_regs_get_ip_ex(SymsArch arch);

SYMS_API SymsRegID
syms_regs_get_sp_ex(SymsArch arch);

typedef struct
{
  SymsRegID aliasee;
  syms_uint bit_shift;
  syms_uint bit_count;
} SymsRegAliasInfo;

SYMS_API SymsRegAliasInfo
syms_regs_get_alias_info(SymsArch arch, SymsRegID id);

//
//
//

SYMS_API const char *
syms_member_access_to_str(SymsMemberAccess access);

SYMS_API const char *
syms_member_modifier_to_str(SymsMemberModifier modifier);

SYMS_API const char *
syms_type_modifier_to_str(SymsTypeModifier modifier);

SYMS_API const char *
syms_type_kind_to_str(SymsTypeKind kind);

SYMS_API syms_uint
syms_get_type_kind_bitcount(SymsArch arch, SymsTypeKind kind);

// --------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif /* SYMS_PUBLIC_INCLUDE_H */


