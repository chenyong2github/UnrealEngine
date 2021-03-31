// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_MSF_INCLUDE_H
#define SYMS_MSF_INCLUDE_H

#ifndef PDB_API
#define PDB_API
#endif

typedef U16 pdb_uint16;
typedef S32 pdb_int;
typedef U32 pdb_uint;
typedef U32 pdb_umm;
#define PDB_UINT_MAX SYMS_UINT32_MAX
#define pdb_trunc_uint(x) (pdb_uint)syms_trunc_u32(x)
#define pdb_trunc_int(x)  (pdb_int)syms_trunc_u32(x)

struct pdb_context;

typedef u32 pdb_offset;

/* stream number */
typedef u16 pdb_sn;

#define PDB_INVALID_SN 0xffff

typedef struct pdb_header20 
{
  U8 magic[44];
  U32 page_size;
  U16 free_page_map;
  U16 pages_used;
  U32 root_size;
  U32 reserved;
  U16 page_map_addr;
} pdb_header20;

typedef struct pdb_header70 
{
  U8 magic[32];

  /* Size of page in bytes */
  U32 page_size;

  /* Index of the free page map */
  S32 free_page_map;

  /* Number of pages used */
  S32 pages_used;

  /* Size of root directory in bytes */
  U32 root_size;

  /* Purpose of this field is unknown */
  U32 reserved;

  /* Number of page that contains page map */
  U32 page_map_addr;
} pdb_header70;

typedef enum 
{
  PDB_PAGE_INDEX16 = 2,
  PDB_PAGE_INDEX32 = 4
} pdb_page_index_type;

#define PDB_STREAM_FLAGS_READ_FAILED (1u << 0u)
#define PDB_STREAM_FLAGS_SEEK_FAILED (1u << 1u)
typedef U32 pdb_stream_flags;

#define PDB_STREAM_READ_OR_SEEK_FAILED(flags) (((flags) & PDB_STREAM_FLAGS_READ_FAILED) || ((flags) & PDB_STREAM_FLAGS_SEEK_FAILED))
#define PDB_STREAM_NO_ERROR(flags)            ((~(flags) & PDB_STREAM_FLAGS_READ_FAILED) && (~(flags) & PDB_STREAM_FLAGS_SEEK_FAILED))
typedef struct pdb_stream 
{
  struct pdb_context *pdb;

  pdb_stream_flags flags;

  /* Stream number (ID) */
  pdb_sn sn;

  U32 page_size;
  U32 page_read_lo;
  U32 page_read_hi;

  /* Indicates how many bytes read into the stream. */
  U32 off;

  /* Number of bytes read when stream was subset */
  U32 off_at_subset;

  /* Amount of bytes that can be read from this stream. */
  U32 size;

  /* Offset into the root stream where the page indices for this stream start. */
  U32 indices_off;

  /* Current location in the root stream. */
  U32 root_off;
} pdb_stream;

typedef struct pdb_symrec 
{
  U16 size;
  U16 type;
  U32 end;
} pdb_symrec;

#define pdb_stream_skip(stream, num) pdb_stream_seek(stream, (stream)->off + (num))
PDB_API syms_bool
pdb_stream_seek(pdb_stream *stream, U32 offset);

#define pdb_stream_read_struct(stream, bf, type) pdb_stream_read(stream, bf, sizeof(type))
PDB_API pdb_uint
pdb_stream_read(pdb_stream *stream, void *buff, pdb_uint buff_max);

PDB_API syms_bool
pdb_stream_can_read_bytes(pdb_stream *stream, pdb_uint num_bytes);

PDB_API syms_bool
pdb_stream_align(pdb_stream *stream, pdb_uint align);

PDB_API syms_bool
pdb_stream_init(struct pdb_context *pdb, pdb_sn sn, pdb_stream *out_stream);

PDB_API syms_bool
pdb_stream_init_at(struct pdb_context *pdb, pdb_sn sn, pdb_offset offset, pdb_stream *out_stream);

PDB_API void
pdb_stream_init_null(pdb_stream *stream);

PDB_API syms_bool
pdb_stream_is_null(pdb_stream *stream);

PDB_API pdb_stream
pdb_stream_subset(const pdb_stream *stream, pdb_uint off, pdb_uint size);

PDB_API pdb_uint
pdb_stream_get_abs_off(pdb_stream *stream);

PDB_API syms_bool
pdb_stream_read_u08(pdb_stream *stream, U8 *out_value);

PDB_API syms_bool
pdb_stream_read_u16(pdb_stream *stream, U16 *out_value);

PDB_API syms_bool
pdb_stream_read_u32(pdb_stream *stream, pdb_uint *out_value);

PDB_API syms_bool
pdb_stream_read_uleb32(pdb_stream *stream, pdb_uint *out_value);

PDB_API syms_bool
pdb_stream_read_symrec(pdb_stream *stream, pdb_symrec *out_rec);

PDB_API pdb_uint
pdb_stream_read_str(pdb_stream *stream, void *bf, pdb_uint bf_max);

PDB_API pdb_uint
pdb_stream_strlen(pdb_stream *stream);

#endif /* SYMS_MSF_INCLUDE_H */
