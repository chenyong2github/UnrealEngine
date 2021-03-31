// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_PDB_INCLUDE_H
#define SYMS_PDB_INCLUDE_H

#define SYMS_ASSERT_CORRUPTED_STREAM SYMS_ASSERT_FAILURE("corrupted stream")

/* Maximum number of bytes a string contains */
#define PDB_STRLEN_MAX 4096

enum
{
  PDB_ERROR_ok,
  PDB_ERROR_corrupted_data
};
typedef u32 pdb_error_code;

typedef enum 
{
  PDB_VER_VC2      = 0x13048EA,
  PDB_VER_VC4      = 0x1306C1F,
  PDB_VER_VC41     = 0x1306CDE,
  PDB_VER_VC50     = 0x13091F3,
  PDB_VER_VC98     = 0x130BA2C,
  PDB_VER_VC70     = 0x1312E94,
  PDB_VER_VC70_DEP = 0x131084C,
  PDB_VER_VC80     = 0x131A5B5,
  PDB_VER_VC110    = 0x1329141,
  PDB_VER_VC140    = 0x13351DC
} pdb_ver_e;

typedef enum 
{
  PDB_TYPED_STREAM_HEADER_BLOCK,

  /* Contains hash tables for strings and offsets. 
   * For parsing look at "pdb_init". */
  PDB_TYPED_STREAM_STRTABLE,

  /* Contains info left there by linker. */
  PDB_TYPED_STREAM_LINK_INFO,

  PDB_TYPED_STREAM_MAX
} pdb_typed_stream_e;

typedef enum 
{
  PDB_DEFAULT_STREAM_ROOT,
  PDB_DEFAULT_STREAM_STREAMS,
  PDB_DEFAULT_STREAM_TPI,
  PDB_DEFAULT_STREAM_DBI,
  PDB_DEFAULT_STREAM_IPI
} pdb_default_stream_e;
typedef pdb_sn pdb_default_stream;

typedef struct
{
  u32 count;
  pdb_cv_itype *e;
} pdb_itype_array;

#define PDB_NUMERIC_MAX 32
typedef struct pdb_numeric 
{
  pdb_cv_itype itype;
  union 
  {
    char ch;

    S8 int8;
    S16 int16;
    S32 int32;
    S64 int64;

    U8 uint8;
    U16 uint16;
    U32 uint32;
    U64 uint64;

    F32 float32;
    F64 float64;

    U8 data[PDB_NUMERIC_MAX];
  } u;
} pdb_numeric;

enum 
{
  PDB_POINTER_MODE_NULL,
  PDB_POINTER_MODE_RAW,
  PDB_POINTER_MODE_PAGES,
  PDB_POINTER_MODE_STREAM
};
typedef u32 pdb_pointer_mode;

// Maximum number of pages we can bake in one pointer.
// Imperically largerst thing that gets baked is a string,
// which are around 4k long and by default page size in PDB
// is 4K, so edge case scenario here is a string that spans 
// accross two pages.
#define PDB_POINTER_PAGE_MAX 2

typedef struct pdb_pointer 
{
  pdb_pointer_mode mode;

  union {
    struct {
      const void *data;
      pdb_uint    size;
    } raw;

    struct {
      pdb_uint offs[PDB_POINTER_PAGE_MAX];
      pdb_uint size[PDB_POINTER_PAGE_MAX];
    } pages;
  
    struct {
      pdb_sn   sn;
      pdb_uint off;
      pdb_uint size;
    } stream;
  } u;
} pdb_pointer;
typedef pdb_pointer pdb_string_ref;

PDB_API pdb_pointer
pdb_pointer_bake(pdb_stream *stream, pdb_uint num_bytes);

PDB_API pdb_pointer
pdb_pointer_bake_null(void);

PDB_API pdb_pointer
pdb_pointer_bake_one_page(void *data, U32 data_len);

PDB_API pdb_pointer
pdb_pointer_bake_str(SymsString str);

PDB_API pdb_pointer
pdb_pointer_bake_stream_str(pdb_stream *msf);

PDB_API pdb_pointer
pdb_pointer_bake_sn(struct pdb_context *pdb, pdb_sn sn, U32 off, U32 len);

PDB_API U32
pdb_pointer_get_size(pdb_pointer *pointer);

PDB_API U32
pdb_pointer_read(struct pdb_context *pdb, const pdb_pointer *pointer, U32 off, void *bf, U32 bf_max);

PDB_API U32
pdb_pointer_read_u32(struct pdb_context *pdb, pdb_pointer *pointer, U32 off);

PDB_API U16
pdb_pointer_read_u16(struct pdb_context *pdb, pdb_pointer *pointer, U32 off);

PDB_API U8
pdb_pointer_read_u08(struct pdb_context *pdb, pdb_pointer *pointer, U32 off);

PDB_API U32
pdb_pointer_read_utf8(struct pdb_context *pdb, pdb_pointer *pointer, U32 off, U32 *codepoint_out);

PDB_API syms_bool
pdb_pointer_cmp(struct pdb_context *pdb, pdb_pointer *pointer_a, pdb_pointer *pointer_b);

typedef enum 
{
  PDB_STRCMP_FLAG_NULL     = 0,
  PDB_STRCMP_FLAG_NOCASE   = (1 << 0),
  PDB_STRCMP_FLAG_NOREWIND = (1 << 1)
} pdb_strcmp_flags_e;

PDB_API syms_bool
pdb_pointer_strcmp_(struct pdb_context *pdb, pdb_pointer *pointer_a, pdb_pointer *pointer, pdb_strcmp_flags_e cmp_flags);

#define pdb_strcmp_pointer(pdb, a, b)        pdb_strcmp_pointer_(pdb, a, b, PDB_STRCMP_FLAG_NULL)
#define pdb_strcmp_pointer_nocase(pdb, a, b) pdb_strcmp_pointer_(pdb, a, b, PDB_STRCMP_FLAG_NOCASE)
PDB_API syms_bool
pdb_strcmp_pointer_(struct pdb_context *pdb, SymsString str, pdb_pointer *str_b, pdb_strcmp_flags_e cmp_flags);

#define pdb_pointer_strcmp(pdb, a, b)        pdb_pointer_strcmp_(pdb, a, b, PDB_STRCMP_FLAG_NULL)
#define pdb_pointer_strcmp_nocase(pdb, a, b) pdb_pointer_strcmp_(pdb, a, b, PDB_STRCMP_FLAG_NOCASE)
PDB_API syms_bool
pdb_pointer_strcmp_(struct pdb_context *pdb, pdb_pointer *str_a, pdb_pointer *str_b, pdb_strcmp_flags_e cmp_flags);

#define pdb_strcmp_stream(a, b)        pdb_strcmp_stream_(a, b, PDB_STRCMP_FLAG_NULL)
#define pdb_strcmp_stream_nocase(a, b) pdb_strcmp_stream_(a, b, PDB_STRCMP_FLAG_NOCASE)
PDB_API syms_bool
db_strcmp_stream_(SymsString str, struct pdb_stream *stream, pdb_strcmp_flags_e cmp_flags);

#define pdb_stream_strcmp(a, b)        pdb_stream_strcmp_stream_(a, b, PDB_STRCMP_FLAG_NULL)
#define pdb_stream_strcmp_nocase(a, b) pdb_stream_strcmp_stream_(a, b, PDB_STRCMP_FLAG_NOCASE)
PDB_API syms_bool
pdb_stream_strcmp_stream_(pdb_stream *stream_a, pdb_stream *stream_b, pdb_strcmp_flags_e cmp_flags);

#define pdb_stream_strcmp_pointer(a, b)         pdb_stream_strcmp_pointer_(a, b, PDB_STRCMP_FLAG_NULL)
#define pdb_stream_strcmp_pointer_nocase(a, b)  pdb_stream_strcmp_pointer_(a, b, PDB_STRCMP_FLAG_NOCASE)
PDB_API syms_bool
pdb_stream_strcmp_pointer_(pdb_stream *str_a, pdb_pointer *str_b, pdb_strcmp_flags_e cmp_flags);

typedef enum 
{
  PDB_TYPE_NULL,
  PDB_TYPE_CHAR,
  PDB_TYPE_UCHAR,
  PDB_TYPE_INT8,
  PDB_TYPE_UINT8,
  PDB_TYPE_INT16,
  PDB_TYPE_UINT16,
  PDB_TYPE_INT32,
  PDB_TYPE_UINT32,
  PDB_TYPE_INT64,
  PDB_TYPE_UINT64,
  PDB_TYPE_INT128,
  PDB_TYPE_UINT128,
  PDB_TYPE_REAL16,
  PDB_TYPE_REAL32,
  PDB_TYPE_REAL32PP, /* 32 bit partial precision */
  PDB_TYPE_REAL64,
  PDB_TYPE_REAL80,
  PDB_TYPE_REAL128,
  PDB_TYPE_BOOL,
  PDB_TYPE_VOID,
  PDB_TYPE_WCHAR,
  PDB_TYPE_STRUCT,
  PDB_TYPE_UNION,
  PDB_TYPE_CLASS,
  PDB_TYPE_ENUM,
  PDB_TYPE_PROC,
  PDB_TYPE_ENUM_FIELD,
  PDB_TYPE_BITFIELD,
  PDB_TYPE_NESTED_UDT,
  PDB_TYPE_FWDREF,
  PDB_TYPE_VTSHAPE,
  PDB_TYPE_METHOD,
  PDB_TYPE_METHODLIST,
  PDB_TYPE_ARGLIST,
  PDB_TYPE_VARIADIC,
  PDB_TYPE_POINTER,
  PDB_TYPE_PTR,
  PDB_TYPE_ARR,
  PDB_TYPE_FUNCID,
  PDB_TYPE_MFUNCID,
  PDB_TYPE_STRINGID,
  PDB_TYPE_COMPLEX32,
  PDB_TYPE_COMPLEX64,
  PDB_TYPE_COMPLEX80,
  PDB_TYPE_COMPLEX128,
  PDB_TYPE_FIELDLIST,
  PDB_TYPE_LABEL
} pdb_type_kind_e;
typedef U32 pdb_type_kind;

typedef enum 
{
  PDB_TYPE_ATTRIB_HAS_NAME    = 1 << 0,
  PDB_TYPE_ATTRIB_CONST       = 1 << 1,
  PDB_TYPE_ATTRIB_VOLATILE    = 1 << 2,
  PDB_TYPE_ATTRIB_UNALIGNED   = 1 << 3,
  PDB_TYPE_ATTRIB_RESTRICTED  = 1 << 4,
  PDB_TYPE_ATTRIB_LREF        = 1 << 5,
  PDB_TYPE_ATTRIB_RREF        = 1 << 6,
  PDB_TYPE_ATTRIB_FWDREF      = 1 << 7
} pdb_type_attrib_e;
typedef U32 pdb_type_attrib;

typedef struct pdb_type_info_proc 
{
  /* Calling convention (pdb_cv_call_e) */
  U32 conv;

  /* itype of the return value. */
  pdb_cv_itype ret_itype;

  /* Arguments itype. */
  pdb_cv_itype arg_itype;

  U16 arg_count;
} pdb_type_info_proc_t;

typedef struct pdb_type_info_method 
{
  /* pdb_cv_call_e */
  U32 conv;           

  /* Return itype. */
  pdb_cv_itype ret_itype;

  /* Class itype to which this procedure belongs. */
  pdb_cv_itype class_itype;    

  /* Type of the "this" in C++ */
  pdb_cv_itype this_itype;

  /* Arguments itype. */
  pdb_cv_itype arg_itype;      

  U16 arg_count;
} pdb_type_info_method;

typedef struct pdb_type_info_methodlist
{
  pdb_pointer block; /* array of pdb_ml_method */
} pdb_type_info_methodlist;

typedef struct pdb_type_info_vtshape
{
  pdb_uint count;
  /* pdb_lf_vts_desc are represented as 4bit packed integers */
  pdb_pointer ptr; 
} pdb_type_info_vtshape;

typedef struct pdb_type_info_vftable
{
  pdb_cv_itype owner_itype;
  pdb_cv_itype base_table_itype;
  pdb_uint offset_in_object_layout;
  pdb_pointer name;
  pdb_pointer method_names;
} pdb_type_info_vftable;

typedef struct pdb_type_info_label
{
  pdb_cv_ptrmode mode;
} pdb_type_info_label;

typedef struct pdb_type_info_arglist 
{
  // Entry count
  U32 count;

  // Array of arguments, each argument is described with pdb_cv_itype
  pdb_pointer itypes; 
} pdb_type_info_arglist;

typedef struct pdb_type_info_bitfield 
{
  /* Underlying type of a bitfield. */
  pdb_cv_itype base_itype;

  U8 len;
  U8 pos;
} pdb_type_info_bitfield;

typedef struct pdb_type_info_udt 
{
  /* If this field is a UDT(User Defined Type) this field will have
   * a non-zero value. */
  pdb_cv_itype field_itype;

  pdb_cv_itype dims_itype;

  /* Used only to query ENUM_UDT fields. */
  pdb_cv_itype base_itype;             

  /* If this type belongs to a UDT this field will contains number
   * of fields. */
  U16 field_count;
} pdb_type_info_udt;

typedef struct pdb_type_info_funcid 
{
  pdb_pointer name;
  pdb_cv_itype itype;
} pdb_type_info_funcid;

typedef struct pdb_type_info_mfuncid
{
  pdb_pointer name;          /* method name */
  pdb_cv_itype parent_itype; /* type index of class (parent) */
  pdb_cv_itype itype;        /* type index of method */
} pdb_type_info_mfuncid;

typedef struct pdb_type_info_stringid
{
  pdb_pointer   data;       /* string data */
  pdb_cv_itemid sub_string; /* index of sub string */
} pdb_type_info_stringid;

typedef struct pdb_type_info_fieldlist
{
  pdb_pointer data;
} pdb_type_info_fieldlist;

typedef struct pdb_type_info_ptr
{
  pdb_cv_ptrmode mode;
  pdb_cv_ptrtype type;
  pdb_cv_ptr_attrib_t attr;
} pdb_type_info_ptr;

/*  You can call pdb_infer_itype and pass next_cv_itype to
 *  resolve type information for next link in a chain of types.
 *
 *  If type was declared as 'void', pdb_infer_itype assigns zero to size.
 *
 *  When allocating storage for type's value - be careful user can declare any size. 
 *  This is legal to compile "volatile char buffer[1*1024*1024*1024*1024]".
 *  Make sure to put a cap on storage allocations to avoid out-of-memory error.
 */
typedef struct pdb_type 
{
  /* Size of the type in bytes.
   *
   * - When type is a pointer size will reflect an arch specific size,
   *   so 4 bytes on x86 and 8 bytes on x64 and etc.
   *   
   * - When type is an array you progress through a chained-link
   *   of dimensions; more info you can find
   *   in pdb_tm_infer_ti, search for PDB_LF_ARRAY.
   */
  U32 size;

  pdb_type_kind kind;

  pdb_type_attrib attribs;

  /* Unique id that is assigned to the type. */
  pdb_cv_itype cv_itype;

  /* Type is part of a hierarchy, this field indicates next itype. */
  pdb_cv_itype next_cv_itype;

  pdb_pointer name;

  union {
    /* Function ID */
    pdb_type_info_funcid funcid;

    /* Method ID */
    pdb_type_info_mfuncid mfuncid;

    /* String ID */
    pdb_type_info_stringid stringid;

    /* User-defined-type */
    pdb_type_info_udt udt;

    /* Procedure */
    pdb_type_info_proc_t proc;

    /* Pointer / Reference */
    pdb_type_info_ptr ptr;

    /* Method */
    pdb_type_info_method method;

    /* Virtual table shape */
    pdb_type_info_vtshape vtshape;

    /* Argument list */
    pdb_type_info_arglist arglist;

    /* Method list */
    pdb_type_info_methodlist methodlist;

    /* Bit-field */
    pdb_type_info_bitfield bitfield;
    
    pdb_type_info_fieldlist fieldlist;

    pdb_type_info_vftable vftable;

    pdb_type_info_label label;
  } u;
} pdb_type;

typedef struct pdb_tm_bucket 
{
  pdb_ti ti;
  struct pdb_tm_bucket *next;
} pdb_tm_bucket;

typedef struct pdb_tm 
{
  struct pdb_context *pdb;
  pdb_sn sn;
  U32 *ti_offsets;
  pdb_tm_bucket **buckets;
  pdb_tm_header header;
} pdb_tm;

PDB_API U32
pdb_calc_size_for_types(pdb_tm_header *tm_header);

PDB_API syms_bool
pdb_tm_init(pdb_tm *tm, struct pdb_context *pdb, pdb_default_stream sn, SymsArena *arena);

PDB_API syms_bool
pdb_tm_offset_for_ti(pdb_tm *tm, pdb_ti ti, U32 *ti_off_out);

PDB_API syms_bool
pdb_tm_get_itype_off(struct pdb_context *pdb, pdb_cv_itype itype, U32 *itype_off_out);

PDB_API syms_bool
pdb_tm_get_itemid_off(struct pdb_context *pdb, pdb_cv_itemid itemid, U32 *itemid_off);

PDB_API syms_bool
pdb_tm_find_ti(pdb_tm *tm, pdb_pointer *name, pdb_ti *ti_out);

PDB_API syms_bool
pdb_tm_find_itype(struct pdb_context *pdb, pdb_pointer name_ref, pdb_cv_itype *itype_out);

PDB_API syms_bool
pdb_tm_find_itemid(struct pdb_context *pdb, pdb_pointer name_ref, pdb_cv_itemid *itemid_out);

PDB_API syms_bool
pdb_tm_infer_ti(pdb_tm *tm, pdb_ti ti, pdb_type *type_out);

PDB_API syms_bool 
pdb_infer_itype(struct pdb_context *pdb, pdb_cv_itype itype, pdb_type *type_out);

PDB_API syms_bool
pdb_infer_itemid(struct pdb_context *pdb, pdb_cv_itemid itemid, pdb_type *type_out);

PDB_API syms_bool
pdb_type_from_name(struct pdb_context *pdb, const char *name, U32 name_len, pdb_type *type);

typedef struct pdb_type_it 
{
  struct pdb_context *pdb;
  pdb_cv_itype next_itype;
  U32 type_map_index;
  pdb_tm *type_map[2];
} pdb_type_it;

typedef struct 
{
  pdb_pointer file;
  pdb_uint ln;
  pdb_imod mod;
} pdb_udt_srcline;

PDB_API syms_bool
pdb_find_udt_srcline(struct pdb_context *pdb, pdb_cv_itype itype, pdb_udt_srcline *srcline_out);

//

typedef struct pdb_cvdata_token 
{
  pdb_sn   sn;
  pdb_uint soffset;
} pdb_cvdata_token;

typedef struct pdb_context 
{
  void *file_data;
  pdb_uint file_size;

  pdb_uint page_size;
  pdb_uint free_page_map;
  pdb_uint pages_used;
  pdb_uint root_size;
  U8 page_index_size;
  union {
    U16 *addr16;
    U32 *addr32;
  } page_map_addr;
  pdb_uint ver;
  pdb_uint time;
  pdb_uint age;
  /* NOTE(nick): Compiler writes into the PE header same GUID as here. 
   * You can compare both to determine authenticity. */
  SymsGUID auth_guid;
  
  pdb_sn typed_streams[PDB_TYPED_STREAM_MAX];

  struct {
    pdb_dbi_header header;

    U32 ver;
    U16 machine_type;

    U32 modinfo_off;
    U32 modinfo_len;

    U32 seccon_off;
    U32 seccon_len;

    U32 secmap_off;
    U32 secmap_len;

    U32 fileinfo_off;
    U32 fileinfo_len;

    U32 ecinfo_off;
    U32 ecinfo_len;

    pdb_sn symrec_sn;
    pdb_sn pubsym_sn;
    pdb_sn globalsym_sn;

    pdb_sn dbg_streams[PDB_DBG_STREAM_MAX];

    U16 secs_num;
    pdb_img_sec *secs;

    /* NOTE(nick): Offsets into the PDB_DEFAULT_STREAM_DBI that point to modules. */
    U32 mods_num;
    pdb_uint *mods;
  } dbi;

  pdb_stream strtable;
  pdb_stream stroffs;

  struct pdb_tm tpi;
  struct pdb_tm ipi;

  U32 basic_typenames_array_num;
  SymsString *basic_typenames_array;

  U32 publics_array_num;
  pdb_gsi_hr **publics_array;

  U32 globals_array_num;
  pdb_gsi_hr **globals_array;

  // TODO(nick): trampolines are stored in a light-weight struct "pdb_cvdata_token". 
  U32 trampoline_count;
  syms_bool trampoline_contigous;
  pdb_cvdata_token trampoline_data;
} pdb_context;


/* NOTE(nick; Oct 10; 2019): Offset into the string table. */
typedef U32 pdb_stroff;

PDB_API U32
pdb_hashV1_bytes(const void *start, U32 cb, U32 mod);

PDB_API U32
pdb_hashV1_stream(pdb_stream *stream, U32 size, U32 mod);

PDB_API U32
pdb_hashV1_pointer(struct pdb_context *pdb, pdb_pointer *bytes, U32 mod);


//

typedef enum 
{
  PDB_ENCODED_LOCATION_NULL,
  PDB_ENCODED_LOCATION_ENREGED,
  PDB_ENCODED_LOCATION_REGREL,
  PDB_ENCODED_LOCATION_RVA,
  PDB_ENCODED_LOCATION_VA,
  PDB_ENCODED_LOCATION_IMPLICIT
} pdb_encoded_location_type_e;
typedef U32 pdb_encoded_location_type;

#define PDB_ENCODED_LOCATION_FLAG_NULL          0u
#define PDB_ENCODED_LOCATION_FLAG_OUTSIDE_RANGE (1u << 0u)
typedef U32 pdb_encoded_location_flags;

typedef struct pdb_regrel 
{
  /* Offset that has to be applied to the register in order to get value for the local. */
  U32 reg_off;

  /* Index of the register (see pdb_cv_reg_e). */
  U32 reg_index;
} pdb_regrel;

/* Compiler stored local in a register. */
typedef struct pdb_enreged 
{
  U32 reg_index;
} pdb_enreged;

typedef struct pdb_persist 
{
  U64 va;
} pdb_persist;

#define PDB_LOCATION_IMPLICIT_VALUE_MAX 32
typedef struct pdb_implicit 
{
  U8 len;
  U8 data[PDB_LOCATION_IMPLICIT_VALUE_MAX];
} pdb_implicit;

/* Represents an encoded location of an address or a value.
 * Use "pdb_decode_location" to resolve final address. */
typedef struct pdb_encoded_location 
{
  /* Describes type of the memory that union below points to. */
  pdb_encoded_location_type type;
  pdb_encoded_location_flags flags;

  union {
    pdb_enreged  enreged;
    pdb_regrel   regrel;
    pdb_persist  persist;
    pdb_implicit implicit;
  } u;
} pdb_encoded_location;

typedef enum 
{
  PDB_LOCATION_NULL,
  PDB_LOCATION_VA,
  PDB_LOCATION_IMPLICIT
} pdb_location_e;

typedef struct pdb_location 
{
  pdb_location_e type;
  union {
    SymsAddr va;
    pdb_implicit implicit;
  } u;
} pdb_location;

PDB_API pdb_encoded_location
pdb_encode_location_for_secoff(struct pdb_context *pdb, U32 sec, U32 off);

PDB_API pdb_encoded_location
pdb_encode_location_for_regrel(struct pdb_context *pdb, U32 regindex, U32 regoff);

PDB_API pdb_encoded_location
pdb_encode_location_for_datasym32(struct pdb_context *pdb, pdb_cv_datasym32 *datasym);

#define PDB_MEMREAD_SIG(name) syms_bool name(void *context, SymsAddr va, void *read_buffer, U32 read_buffer_max)
typedef PDB_MEMREAD_SIG(pdb_memread_sig);

#define PDB_REGREAD_SIG(name) U32 name(void *context, U32 reg_index, void *read_buffer, U32 read_buffer_max)
typedef PDB_REGREAD_SIG(pdb_regread_sig);

#define PDB_REGWRITE_SIG(name) U32 name(void *context, U32 reg_index, void *write_buffer, U32 write_buffer_max)
typedef PDB_REGWRITE_SIG(pdb_regwrite_sig);

PDB_API syms_bool
pdb_decode_location(pdb_encoded_location *encoded_loc,
    SymsAddr orig_rebase,
    SymsAddr rebase,
    void *memread_ctx, pdb_memread_sig *memread,
    void *regread_ctx, pdb_regread_sig *regread,
    pdb_location *decoded_loc);

PDB_API syms_bool
pdb_build_va(struct pdb_context *pdb, U32 sec, U32 off, SymsAddr *out_va);

PDB_API syms_bool
pdb_build_sec_off(struct pdb_context *pdb, SymsAddr va, pdb_isec *sec, pdb_isec_umm *off);

/* -------------------------------------------------------------------------------- */

typedef struct pdb_var 
{
  /* Type of the local, you can pass this to the "pdb_infer_itype". */
  pdb_cv_itype itype;

  pdb_cv_localsym_flags flags;

  /* Encoded address or value where variable is located in memory. */
  pdb_encoded_location encoded_va;

  pdb_pointer name;

  pdb_pointer gaps;
} pdb_var;

typedef struct pdb_sec_it 
{
  struct pdb_context *pdb;
  pdb_stream stream;
} pdb_sec_it;

PDB_API syms_bool
pdb_sec_it_init(struct pdb_context *pdb, pdb_sec_it *sec_it);

PDB_API syms_bool
pdb_sec_it_next(pdb_sec_it *sec_it, pdb_img_sec *sec);

PDB_API syms_bool
pdb_sec_from_index(struct pdb_context *pdb, U32 index, pdb_img_sec *sec_out);

typedef struct pdb_debug_sec 
{
  pdb_cv_ss_type type;
  pdb_stream stream;
} pdb_debug_sec;

typedef enum 
{
  PDB_MOD_SEC_SYMS,
  PDB_MOD_SEC_LINES_C11,
  PDB_MOD_SEC_LINES_C13,
  PDB_MOD_SEC_INLINE_LINES
} pdb_mod_sec_type_e;

typedef enum 
{
  PDB_MOD_FILECHKSUM_CACHED = (1 << 0)
} pdb_mod_flags_e;
typedef U32 pdb_mod_flags;

typedef struct pdb_mod 
{
  struct pdb_context *pdb;

  pdb_imod id;

  pdb_mod_flags flags;

  pdb_sn sn;

  pdb_isec sec;
  pdb_isec_umm sec_off;
  pdb_isec_umm sec_len;

  U32 syms_size;
  U32 c11_lines_size;
  U32 c13_lines_size;

  pdb_pointer name;
  pdb_pointer name2;

  pdb_debug_sec filechksum;
} pdb_mod;

typedef struct pdb_mod_it 
{
  struct pdb_context *pdb;
  pdb_stream dbi_data;
  pdb_imod imod;
} pdb_mod_it;

PDB_API syms_bool
pdb_mod_it_init(pdb_mod_it *mod_it, struct pdb_context *pdb);

PDB_API syms_bool
pdb_mod_it_next(pdb_mod_it *mod_it, pdb_mod *mod_out);

PDB_API syms_bool
pdb_mod_it_seek(pdb_mod_it *mod_it, pdb_imod imod);

PDB_API syms_bool
pdb_mod_get_debug_sec(pdb_mod *mod, pdb_mod_sec_type_e sec, pdb_stream *dbi_data_out);

PDB_API syms_bool
pdb_imod_from_isec(struct pdb_context *pdb, pdb_isec sec, pdb_isec_umm off, pdb_imod *imod_out);

PDB_API syms_bool
pdb_mod_init(pdb_mod *mod_out, struct pdb_context *pdb, pdb_imod imod);

typedef struct pdb_file_info 
{
  pdb_pointer         path;
  pdb_pointer         chksum;
  pdb_cv_chksum_type  chksum_type;
} pdb_file_info; // 72->56 bytes

PDB_API syms_bool
pdb_mod_get_filechksum(pdb_mod *mod, pdb_stream *stream_out);

typedef struct pdb_debug_sec_it 
{
  pdb_stream stream;
} pdb_debug_sec_it;

PDB_API syms_bool
pdb_debug_sec_it_init(pdb_debug_sec_it *it, pdb_mod *mod);

PDB_API syms_bool
pdb_debug_sec_it_next(pdb_debug_sec_it *it, pdb_debug_sec *sec_out);

typedef struct pdb_dss_it 
{
  pdb_cv_ss_type type;
  pdb_stream stream;
  syms_bool ex_mode;
} pdb_dss_it;

/* Inline data is stored in DBI stream and this is header file for it. */
typedef struct pdb_ss_inline 
{
  pdb_cv_itemid inlinee;

  U32 src_ln;
  U32 file_id;
  U32 extra_files_count;
  pdb_pointer extra_files;
} pdb_ss_inline;

PDB_API syms_bool
pdb_dss_it_init(pdb_dss_it *it_out, pdb_debug_sec *sec);

PDB_API syms_bool
pdb_dss_it_next_inline(pdb_dss_it *it, pdb_ss_inline *inline_out);

typedef struct pdb_sym_it 
{
  syms_bool inited_from_token;
  pdb_stream stream;
} pdb_sym_it;

PDB_API syms_bool
pdb_sym_it_init(pdb_sym_it *sym_it, pdb_mod *mod);

PDB_API syms_bool
pdb_sym_it_init_token(pdb_sym_it *sym_it, struct pdb_context *pdb, pdb_cvdata_token token);

PDB_API syms_bool
pdb_sym_it_read(pdb_sym_it *sym_it, pdb_cv_sym_type *type_out, pdb_stream *stream_out);

PDB_API syms_bool
pdb_sym_it_peek(pdb_sym_it *sym_it, pdb_cv_sym_type *type_out, pdb_stream *stream_out);

PDB_API syms_bool
pdb_sym_it_next(pdb_sym_it *sym_it, pdb_cv_sym_type *type_out, pdb_stream *stream_out);

enum
{
  PDB_LOCAL_EXPORT_NULL,
  PDB_LOCAL_EXPORT_VAR,
  PDB_LOCAL_EXPORT_SCOPE,
  PDB_LOCAL_EXPORT_SCOPE_END
};
typedef pdb_uint pdb_local_export_type;

typedef struct pdb_scope
{
  SymsAddr inst_lo;
  SymsAddr inst_hi;
} pdb_scope;

typedef struct pdb_local_export
{
  pdb_local_export_type type;
  union
  {
    pdb_var var;
    pdb_scope scope;
  } u;
} pdb_local_export;

typedef struct pdb_local_it 
{
  struct pdb_context *pdb;
  syms_bool defrange_mode;
  pdb_sym_it sym_it;

  pdb_isec sec;
  pdb_isec_umm sec_off;

  U32 range_off;
  U32 range_len;

  S32 block32_count;
  S32 regrel32_count;
  S32 inlinesite_count;

  U16 arg_count;
} pdb_local_it;

PDB_API syms_bool
pdb_local_it_init_(pdb_local_it *local_it, struct pdb_context *pdb, pdb_isec sec, pdb_isec_umm sec_off, pdb_cvdata_token cvdata);

PDB_API syms_bool
pdb_local_it_init(pdb_local_it *local_it, struct pdb_context *pdb, pdb_isec sec, pdb_isec_umm sec_off);

PDB_API syms_bool
pdb_local_it_next(pdb_local_it *local_it, pdb_local_export *export_out);

typedef struct pdb_global_it 
{
  struct pdb_context *pdb;
  pdb_stream stream;
  U32 hr_index;
  pdb_gsi_hr *hr;
} pdb_global_it;

PDB_API syms_bool
pdb_global_it_init(pdb_global_it *global_it, struct pdb_context *pdb);

PDB_API syms_bool
pdb_global_it_next(pdb_global_it *global_it, pdb_var *var_out);

PDB_API syms_bool
pdb_global_from_name(struct pdb_context *pdb, const char *name, U32 name_size, pdb_var *global_out);

typedef struct pdb_file_it 
{
  struct pdb_context *pdb;
  pdb_stream strtable;
  pdb_stream stroffs;
  U32 off_count;
  U32 num_read;
} pdb_file_it;

PDB_API syms_bool
pdb_file_it_init(pdb_file_it *file_it, struct pdb_context *pdb);

PDB_API syms_bool
pdb_file_it_next(pdb_file_it *file_it, pdb_pointer *file_out);

typedef struct pdb_proc_it 
{
  pdb_sym_it sym_it;
} pdb_proc_it;

typedef struct pdb_proc 
{
  pdb_isec sec;
  pdb_isec_umm sec_off;

  /* Length in bytes, of all instruction that contribute to a procedure. */
  pdb_uint size;

  pdb_cv_itype itype;

  /* pdb_cv_proc_flags_e */
  pdb_uint flags;

  pdb_string_ref name;

  pdb_cvdata_token cvdata;
} pdb_proc;

PDB_API syms_bool
pdb_proc_it_init(pdb_proc_it *proc_it, pdb_mod *mod);

PDB_API syms_bool
pdb_proc_it_next(pdb_proc_it *proc_it, pdb_proc *out_proc);

PDB_API syms_bool
pdb_proc_from_name(pdb_context *pdb, const char *name, pdb_uint name_len, pdb_proc *proc_out);

PDB_API syms_bool
pdb_proc_from_cvdata(pdb_context *pdb, pdb_cvdata_token cvdata, pdb_proc *proc_out);

PDB_API syms_bool
pdb_proc_from_stream(pdb_cv_sym_type cvtype, pdb_stream *cvdata, pdb_proc *proc_out);

PDB_API syms_bool
pdb_proc_from_name_(pdb_context *pdb, pdb_pointer *name, pdb_proc *proc_out);

typedef struct pdb_inline_site 
{
  pdb_pointer name;

  pdb_cv_itype itype;

  pdb_isec sec;
  pdb_isec_umm sec_off;
  u32 size;

  pdb_file_info fi;

  U32 ln_at_pc;

  pdb_cvdata_token cvdata;
} pdb_inline_site;

typedef struct pdb_inline_it 
{
  S32 site_count;
  /* NOTE(nick; Oct 6 2019): Procedure relative Program Counter. */
  SymsAddr proc_pc;
  pdb_proc proc;
  pdb_mod mod;
  pdb_sym_it sym_it;
} pdb_inline_it;

PDB_API syms_bool
pdb_inline_it_init(pdb_inline_it *inline_it, struct pdb_context *pdb, pdb_cvdata_token cvproc, SymsAddr proc_rva);

PDB_API syms_bool
pdb_inline_it_read(pdb_inline_it *inline_it, pdb_cv_inlinesym *sym_out, pdb_stream *ba_out);

PDB_API syms_bool
pdb_inline_it_next(pdb_inline_it *inline_it, pdb_inline_site *site_out);

typedef struct pdb_const_value 
{
  pdb_string_ref name;
  pdb_cv_itype itype;
  pdb_numeric value;
} pdb_const_value;

typedef struct pdb_const_it 
{
  struct pdb_context *pdb;
  pdb_stream stream;
  U32 index;
  pdb_gsi_hr *hr;
} pdb_const_it;

PDB_API syms_bool
pdb_const_it_init(pdb_const_it *const_it, struct pdb_context *pdb);

PDB_API syms_bool
pdb_const_it_next(pdb_const_it *const_it, pdb_const_value *const_out);

PDB_API syms_bool
pdb_const_from_name(struct pdb_context *pdb, const char *name, U32 name_size, pdb_const_value *const_out);

typedef struct pdb_arg_it 
{
  /* parser context */
  struct pdb_context *pdb;

  /* current argument index */
  u32 idx;

  /* array of pdb_cv_itype entries */
  pdb_pointer itypes;
} pdb_arg_it;

PDB_API syms_bool
pdb_arg_it_init(pdb_arg_it *it, struct pdb_context *pdb, pdb_cv_itype arglist_itype);

PDB_API syms_bool
pdb_arg_it_next(pdb_arg_it *it, pdb_cv_itype *out_arg_type);

/* -------------------------------------------------------------------------------- */

enum 
{
  PDB_MEMBER_TYPE_NULL,

  /* Member for data that is stored inside the UDT  */
  PDB_MEMBER_TYPE_DATA,

  /* Class method */
  PDB_MEMBER_TYPE_METHOD,

  /* Enum member */
  PDB_MEMBER_TYPE_ENUMERATOR,

  /* Type that was declared inside a class */
  PDB_MEMBER_TYPE_NESTED_TYPE,

  /* Inherited class */
  PDB_MEMBER_TYPE_BASE_CLASS,

  /* Describes memory region for virtual functions.
   * Memory is packed as an array of 4 bit integers,
   * each integer maps into pdb_lf_vts_desc. */
  PDB_MEMBER_TYPE_VIRTUAL_TABLE,
  
  /* Static data member of a class */
  PDB_MEMBER_TYPE_STATIC_DATA,

  /* 
   * CodeView packs methods with matching identifiers into 
   * an array of methods. I am sorry for ugly hack - we need 
   * it to hide in the interface layer codeview shenanigans. 
   *
   * Also we dont have an allocator in the API layer, 
   * iterators, symbols, line info, and so on are allocated 
   * on the stack. Library allocates memory just for lookup
   * acceleration and it is possible to parse entire pdb
   * with zero memory allocations! 
   *
   * */
  PDB_MEMBER_TYPE_METHODLIST
};
typedef u32 pdb_member_type;

/* Contains anything that resembles data member in struct, class, enum, and union */
typedef struct pdb_member 
{
  pdb_pointer name;

  pdb_member_type type;

  pdb_cv_itype itype;

  pdb_cv_fldattr_t attr;

  union 
  {
    struct
    {
      /* Parent-relative offset where this member is located in memory. */
      U32 offset; 
    } data;

    struct 
    {
      u32 count;
    } methodlist;

    struct
    {
      pdb_uint vbaseoff;
    } method;

    struct 
    {
      /* Use this field only if the "type" field is set to the PDB_MEMBER_TYPE_ENUMERATOR.
       * This field indicates value that was assigned to enum field. */
      pdb_numeric value;
    } enumerator;

    struct 
    {
      u32 offset;
    } base_class;

    struct
    {
      u32 offset;
    } vtab;
  } u;
} pdb_member;

typedef struct pdb_member_it 
{
  U32 stream_end;
  struct pdb_context *pdb;
  pdb_type_info_udt *udt;
  pdb_stream stream;
} pdb_member_it;

PDB_API syms_bool
pdb_member_it_init(pdb_member_it *member_it, struct pdb_context *pdb, pdb_type_info_udt *udt);

PDB_API syms_bool
pdb_member_it_next(pdb_member_it *member_it, pdb_member *member_out);

/* -------------------------------------------------------------------------------- */

/* Indicates if we need to read another line section. */
#define PDB_LINE_IT_FLAGS_NEW_SECTION (1u << 0u)
typedef U32 pdb_line_it_flags;

typedef enum 
{
  PDB_LINE_FORMAT_NULL,
  PDB_LINE_FORMAT_C11,
  PDB_LINE_FORMAT_C13
} pdb_line_format;

typedef struct pdb_line_it 
{
  /* Iterator for the modules. */
  pdb_mod mod;
  pdb_file_info fi;
  pdb_line_format format;
  pdb_line_it_flags flags;
  pdb_stream stream;

  U16 sec;
  U32 sec_off;
  U32 sec_size;

  U32 last_read_ln;

  union {
    struct {
      /* Count for the total number of lines in current section. */
      U32 line_index_max;

      /* When a new module comes, we scan it to get offset of the checksum 
       * section because all lines instead of providing offset where the 
       * file name is in the name-stream, they give offset into the 
       * checksum section and from there we have to resolve the file name
       * for given lines block. */
      U32 chksum_off;

      /* Offset where current line section ends. */
      U32 sec_end;

      pdb_stream cv_lines;
    } c13;

    struct {
      U32 file_index;
      U32 file_count;

      U16 filesec_index;
      U16 filesec_count;

      U16 pair_index;
      U16 pair_count;

      pdb_stream secrange_stream;
      pdb_stream off_stream;
      pdb_stream ln_stream;
    } c11;
  } u;
} pdb_line_it;

typedef struct pdb_line 
{
  U16 sec;
  U32 off;
  U32 ln;
} pdb_line;

PDB_API syms_bool
pdb_line_it_init(pdb_line_it *line_it, pdb_mod *mod);

PDB_API syms_bool
pdb_line_it_next(pdb_line_it *line_it, pdb_line *line);

typedef struct pdb_map 
{
  /* section and offset for the first instruction in the source line. */
  pdb_isec sec;
  pdb_isec_umm sec_off;

  /* Size of all instructions that contribute to the source line. */
  U32 size;

  /* Line number to which VA was mapped. */
  U32 ln;

  pdb_file_info fi;
} pdb_map;

PDB_API syms_bool
pdb_src_to_va(struct pdb_context *pdb, char *file_name, U32 file_name_len, U32 ln,
        pdb_map *map);

PDB_API syms_bool
pdb_va_to_src(struct pdb_context *pdb, SymsAddr line_va, pdb_map *map);

/* -------------------------------------------------------------------------------- */

PDB_API syms_bool
pdb_find_nearest_sym(struct pdb_context *pdb, SymsAddr va, pdb_pointer *name_out);

PDB_API syms_bool
pdb_find_nearest_sc(struct pdb_context *pdb, SymsAddr va, pdb_sc *sc_out);

PDB_API syms_bool
pdb_find_nearest_sc_ex(struct pdb_context *pdb, U32 sec, U32 off, pdb_sc *sc_out);

// File Info section of DBI packs unique strings of source paths. It has a 16bit counter
// -- hello from good old days -- and today medium size program that link with CRT
// have 30K files when you unpack file info section.
typedef struct pdb_fileinfo
{
  u32 mod_count;
  u32 src_count;
  pdb_stream imod_block;        // pdb_uint16[mod_count] module indices
  pdb_stream count_block;       // source count per module, counts are packed as variable-length arrays.
  pdb_stream ich_block;         // offsets into str_block
  pdb_stream str_block;         // char * array
} pdb_fileinfo;

PDB_API syms_bool
pdb_fileinfo_init(struct pdb_context *pdb, pdb_fileinfo *fi);

PDB_API syms_bool
pdb_fileinfo_get_src_count(pdb_fileinfo *fi, pdb_imod imod, pdb_uint *count_out);

PDB_API syms_bool
pdb_fileinfo_lookup(pdb_fileinfo *fi, pdb_imod imod, pdb_uint index, pdb_pointer *cptr_out);

typedef struct pdb_strtable
{
  u32 magic;
  u32 version;
  u32 bucket_count;
  pdb_stream strblock;
  pdb_stream buckets;
} pdb_strtable;

PDB_API syms_bool
pdb_init_strtable(struct pdb_strtable *st, struct pdb_stream *stream);

PDB_API pdb_stream
pdb_get_strtable(struct pdb_context *pdb);

PDB_API pdb_stream
pdb_get_stroffs(struct pdb_context *pdb);

PDB_API syms_bool
pdb_strtable_at(struct pdb_context *pdb, pdb_ni ni, pdb_pointer *str_out);

PDB_API syms_bool
pdb_strtable_off_to_str(struct pdb_context *pdb, pdb_stroff stroff, pdb_pointer *str_out);

PDB_API syms_bool
pdb_strtable_find(struct pdb_context *pdb, pdb_pointer *name, pdb_strcmp_flags_e cmp_flags, pdb_ni *ni);

PDB_API syms_bool
pdb_strtable_find_str(struct pdb_context *pdb, SymsString name, pdb_strcmp_flags_e cmp_flags, pdb_ni *ni);


PDB_API U32
pdb_parse_gsi_hash_table(pdb_stream *gsi_stream, SymsArena *arena, pdb_gsi_hr ***out_table, U32 *out_num_table);

PDB_API syms_bool
pdb_init(struct pdb_context *pdb, void *data, U32 data_size);

PDB_API SymsUMM
pdb_load_types(pdb_context *pdb, struct SymsArena *arena);

PDB_API SymsUMM
pdb_load_publics(pdb_context *pdb, struct SymsArena *arena);

PDB_API SymsUMM
pdb_load_globals(pdb_context *pdb, struct SymsArena *arena);

PDB_API SymsUMM
pdb_load_dbi(pdb_context *pdb, struct SymsArena *arena);

PDB_API SymsUMM
pdb_load_trampolines(pdb_context *pdb, struct SymsArena *arena);

PDB_API SymsUMM
pdb_calc_size_for_aux_data(pdb_context *pdb);

PDB_API syms_bool
pdb_load_aux_data(pdb_context *pdb, SymsArena *arena);

PDB_API syms_bool
pdb_trampoline_from_ip(pdb_context *pdb, pdb_isec sec, pdb_isec_umm sec_off, pdb_isec *sec_out, pdb_isec_umm *off_out);

PDB_API syms_bool
pdb_stream_read_numeric(pdb_stream *stream, pdb_numeric *out_num);

PDB_API syms_bool
pdb_stream_read_numeric_u32(pdb_stream *stream, U32 *out_value);

PDB_API syms_bool
pdb_stream_read_numeric_u64(pdb_stream *stream, U64 *out_value);

PDB_API SymsNTFileHeaderMachineType
pdb_get_machine_type(struct pdb_context *pdb);

PDB_API syms_bool
pdb_type_it_init(pdb_type_it *it, struct pdb_context *pdb);

PDB_API syms_bool
pdb_type_it_next(pdb_type_it *it, pdb_cv_itype *itype_out);

PDB_API const char *
pdb_ver_to_str(pdb_context *pdb);

PDB_API const char *
pdb_dbi_ver_to_str(pdb_context *pdb);

PDB_API const char *
pdb_basic_itype_to_str(pdb_basic_type itype);

PDB_API syms_bool
pdb_fileinfo_get_strblock(pdb_fileinfo *fi, pdb_imod imod, u32 *count_out, pdb_stream *strblock_out);

#endif /* SYMS_PDB_INCLUDE_H */
