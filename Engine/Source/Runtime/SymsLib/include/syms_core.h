// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_CORE_INCLUDE_H
#define SYMS_CORE_INCLUDE_H

/******************************************************************************
 * File   : syms_core.h                                                       *
 * Author : Nikita Smith                                                      *
 * Created: 2020/06/20                                                        *
 * Purpose: core utilities for pdb and dwarf parsers                          *
 ******************************************************************************/

#define SYMS_PARANOID

#ifndef syms_malloc
#include <stdlib.h>
#define syms_malloc(size, ud) ((void)ud,malloc(size))
#define syms_free(ptr, ud)    ((void)ud,free(ptr))
#endif
#define syms_malloc_array(type, count, ud) (type *)syms_malloc(sizeof(type) * (count), ud)
#define syms_malloc_struct(type, ud)       syms_malloc_array(type, 1, ud)

#ifndef syms_qsort
#include <stdlib.h>
#define syms_qsort qsort
#endif

#ifndef syms_memcpy
#include <string.h>
#define syms_memcpy memcpy
#endif

#ifndef syms_memset
#include <string.h>
#define syms_memset memset
#endif

#ifndef syms_memcmp
#include <string.h>
#define syms_memcmp memcmp
#endif

#ifndef syms_strlen
#include <string.h>
#define syms_strlen strlen
#endif

#ifndef syms_strcmp
#include <string.h>
#define syms_strcmp strcmp
#endif

#ifndef syms_sprintf
#include <stdio.h>
#define syms_sprintf   sprintf
#define syms_snprintf  snprintf
#define syms_vsprintf  vsprintf
#define syms_vsnprintf vsnprintf
#endif

#define syms_memzero(dst, size) syms_memset(dst, 0, size)

#ifndef SYMS_INTERNAL
#define SYMS_INTERNAL static
#endif

#ifndef SYMS_API
#define SYMS_API
#endif

#ifndef SYMS_INLINE
#define SYMS_INLINE inline
#endif

#ifndef SYMS_CUSTOM_TYPES
#include <stdint.h>
typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef int8_t S8;
typedef int16_t S16;
typedef int32_t S32;
typedef float F32;
typedef double F64;
typedef uint64_t U64;
typedef int64_t S64;
#endif

typedef U8  u8;
typedef U16 u16;
typedef U32 u32;
typedef U64 u64;
typedef S8  s8;
typedef S16 s16;
typedef S32 s32;
typedef S64 s64;
typedef F32 f32;
typedef F64 f64;

typedef S32 syms_bool;

#define syms_false 0
#define syms_true 1

#define SYMS_INT8_MIN  0x80 // -128
#define SYMS_INT8_MAX  0x7f // +127
#define SYMS_INT16_MIN 0x8000 // -32768
#define SYMS_INT16_MAX 0x7fff // +32767
#define SYMS_INT32_MIN 0x80000000 // -2147483648
#define SYMS_INT32_MAX 0x7fffffff // +2147483647
#define SYMS_INT64_MIN 0x8000000000000000ull // -9223372036854775808
#define SYMS_INT64_MAX 0x7fffffffffffffffull // +9223372036854775807

#define SYMS_UINT8_MAX  0xFFu
#define SYMS_UINT16_MAX 0xFFFFu
#define SYMS_UINT32_MAX 0xFFFFFFFFu
#define SYMS_UINT64_MAX 0xFFFFFFFFFFFFFFFFu

#define SYMS_TOSTR_X(x) #x
#define SYMS_TOSTR(x) SYMS_TOSTR_X(x)
#define SYMS_JOIN(a,b) a ## b

#ifndef SYMS_ASSERT
#ifdef _WIN32
#include <intrin.h>
#define SYMS_ASSERT(x) do { if (!(x)) __debugbreak(); } while (0)
#else
#include <assert.h>
#define SYMS_ASSERT(x) assert(x)
#endif
#endif

#ifndef SYMS_ASSERT_ALWAYS
#define SYMS_ASSERT_ALWAYS(x) SYMS_ASSERT(x)
#endif

#ifndef SYMS_ASSERT_FAILURE
#define SYMS_ASSERT_FAILURE(x) SYMS_ASSERT(0)
#endif

#ifndef SYMS_ASSERT_FAILURE_ALWAYS
#define SYMS_ASSERT_FAILURE_ALWAYS(x) SYMS_ASSERT(0)
#endif

#if defined(SYMS_PARANOID)
#define SYMS_ASSERT_PARANOID(x)           SYMS_ASSERT(x)
#define SYMS_ASSERT_FAILURE_PARANOID(msg) SYMS_ASSERT_FAILURE(msg)
#else
#define SYMS_ASSERT_PARANOID(x)
#define SYMS_ASSERT_FAILURE_PARANOID(msg)
#endif

#define SYMS_NOT_IMPLEMENTED    SYMS_ASSERT_FAILURE_ALWAYS("not implemented")
#define SYMS_INVALID_CODE_PATH  SYMS_ASSERT_FAILURE_ALWAYS("invalid code path")
#define SYMS_ASSERT_NO_SUPPORT  SYMS_INVALID_CODE_PATH

#ifndef SYMS_COMPILER_ASSERT
#define SYMS_COMPILER_ASSERT(predicate) SYMS_COMPILER_ASSERT_IMPL(predicate, __LINE__)
#define SYMS_COMPILER_ASSERT_IMPL(predicate, line) typedef char SYMS_JOIN(compiler_dummy_name, line)[(predicate) ? +1 : -1]
#endif

#ifndef SYMS_ARRAY_SIZE
#define SYMS_ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#endif

#ifndef SYMS_MIN
#define SYMS_MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef SYMS_MAX
#define SYMS_MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef SYMS_ABS
#define SYMS_ABS(x) ((x) < 0 ? -(x) : (x))
#endif

#ifndef SYMS_MEMBER_OFFSET
#define SYMS_MEMBER_OFFSET(type, member) offsetof(type, member)
#endif

#ifndef SYMS_GET_MEMBER_SIZE
#define SYMS_GET_MEMBER_SIZE(type, member) sizeof((((type*)0)->member))
#endif

#ifdef _MSC_VER
#define SYMS_PRIu64 "I64u"
#define SYMS_PRId64 "I64d"
#define SYMS_PRIx64 "I64x"
#else
#include <inttypes.h>
#define SYMS_PRIu64 PRIu64
#define SYMS_PRId64 PRId64
#define SYMS_PRIx64 PRIx64
#endif

#define SYMS_KB(num) ((num)*1024u)
#define SYMS_MB(num) (SYMS_KB(num)*1024u)
#define SYMS_GB(num) (SYMS_MB(num)*1024u)
#define SYMS_TB(num) (SYMS_TB(num)*1024u)

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define SYMS_X86
#define SYMS_X64
#define SYMS_LITTLE_ENDIAN
#elif defined(i386) || defined(__i386) || defined(__i386__) || defined(_X86_) || defined(_M_I86)
#define SYMS_X86
#define SYMS_LITTLE_ENDIAN
#elif defined(__arm__) || defined(_M_ARM) 
#define SYMS_ARM
#define SYMS_LITTLE_ENDIAN
#elif defined(__aarch64__)
#define SYMS_ARM64
#define SYMS_LITTLE_ENDIAN
#else
#error "arch not defined"
#endif

#ifndef SYMS_CUSTOM_BSWAP
#if defined(__clang__)
#define syms_bswap16 __builtin_bswap16
#define syms_bswap32 __builtin_bswap32
#define syms_bswap64 __builtin_bswap64
#elif defined(_MSC_VER)
#define syms_bswap16 _byteswap_ushort
#define syms_bswap32 _byteswap_ulong
#define syms_bswap64 _byteswap_uint64
#else
SYMS_INTERNAL U16 syms_bswap16(U16 x) 
{
    return (((x >> 8) & 0xffu) | ((x & 0xffu) << 8));
}
SYMS_INTERNAL U32 syms_swap32(U32 x) 
{
    return (((x & 0xff000000u) >> 24) |
            ((x & 0x00ff0000u) >> 8) |
            ((x & 0x0000ff00u) << 8) |
            ((x & 0x000000ffu) << 24));
}
SYMS_INTERNAL U64 syms_bswap64(U64 x) 
{
    return (((x & 0xff00000000000000) >> 56) |
            ((x & 0x00ff000000000000) >> 40) |
            ((x & 0x0000ff0000000000) >> 24) |
            ((x & 0x000000ff00000000) >> 8) |
            ((x & 0x00000000ff000000) << 8) |
            ((x & 0x0000000000ff0000) << 24) |
            ((x & 0x000000000000ff00) << 40) |
            ((x & 0x00000000000000ff) << 56));
}
#endif
#endif

#if defined(SYMS_X64) || defined(SYMS_ARM64)
typedef U64 SymsUWord;
typedef S64 SymsSWord;
#define SYMS_UWORD_MAX SYMS_UINT64_MAX
#define SYMS_SWORD_MAX SYMS_INT64_MAX
#define syms_bswapaddr(x) syms_bswap64(x)
#elif defined(SYMS_X86)
typedef U32 SymsUWord;
typedef S32 SymsSWord;
#define SYMS_UWORD_MAX SYMS_UINT32_MAX
#define SYMS_SWORD_MAX SYMS_INT32_MAX
#define syms_bswapaddr(x) syms_bswap32(x)
#else
#error "Address is not defined for this architecture"
#endif

//typedef SymsUMM SymsAddr;

typedef enum 
{
    SYMS_ADDR_SIZE_NULL,
    SYMS_ADDR_SIZE_16,
    SYMS_ADDR_SIZE_32,
    SYMS_ADDR_SIZE_64,
    SYMS_ADDR_SIZE_128,
    
    SYMS_ADDR_SIZE_HOST =
#if SYMS_ADDR_MAX == SYMS_UINT16_MAX
    SYMS_ADDR_SIZE_16
#elif SYMS_ADDR_MAX == SYMS_UINT32_MAX
    SYMS_ADDR_SIZE_32
#elif SYMS_ADDR_MAX == SYMS_UINT64_MAX 
    SYMS_ADDR_SIZE_64
#elif SYMS_ADDR_MAX == SYMS_UINT64_MAX*2
    SYMS_ADDR_SIZE_128
#else
#error "Address size is not defined"
#endif
} SymsAddrSize;

typedef enum 
{
    SYMS_ENDIAN_NULL,
    SYMS_ENDIAN_LITTLE,
    SYMS_ENDIAN_BIG,
    
    SYMS_ENDIAN_HOST = SYMS_ENDIAN_LITTLE
} SymsEndian;

#if defined(SYMS_LITTLE_ENDIAN)
#define SYMS_LE16(x)     (x)
#define SYMS_LE32(x)     (x)
#define SYMS_LE64(x)     (x)
#define SYMS_LEADDR(x)   (x)
#define SYMS_BE16(x)     syms_bswap16(x)
#define SYMS_BE32(x)     syms_bswap32(x)
#define SYMS_BE64(x)     syms_bswap64(x)
#define SYMS_BEADDR(x)   syms_bswapaddr(x)
#else
#define SYMS_LE16(x)     syms_bswap16(x)
#define SYMS_LE32(x)     syms_bswap32(x)
#define SYMS_LE64(x)     syms_bswap64(x)
#define SYMS_LEADDR(x)   syms_bswapaddr(x)
#define SYMS_BE16(x)     (x)
#define SYMS_BE32(x)     (x)
#define SYMS_BE64(x)     (x)
#define SYMS_BEADDR(x)   (x)
#endif

#define SYMS_OFFSET_INVALID SYMS_UWORD_MAX
typedef SymsUWord SymsOffset;

// -------------------------------------------------------------------------------- 

typedef struct SymsBlock
{
    SymsUMM len;
    void *data;
} SymsBlock;

// -------------------------------------------------------------------------------- 

#define SYMS_STRING_LIT(x) { sizeof(x) - 1, x }
#define syms_string_lit(x) syms_string_init(x, sizeof(x) - 1)

typedef struct SymsStringArray 
{
    U32 count;
    SymsString *entries;
} SymsStringArray;

SYMS_API SymsString
syms_string_init(const char *data, U32 len);

SYMS_API SymsString
syms_string_init_lit(const char *data);

SYMS_API syms_bool
syms_string_is_null(SymsString str);

SYMS_API syms_bool
syms_string_cmp_lit(SymsString a, const char *b);

SYMS_API syms_bool
syms_is_alpha_ascii(U32 codepoint);

SYMS_API U32
syms_lowercase(U32 codepoint);

SYMS_API U32
syms_uppercase(U32 codepoint);

SYMS_API syms_bool
syms_string_cmp_nocase(SymsString a_str, SymsString b_str);

SYMS_API char
syms_string_peek_byte(SymsString str, U32 byte_offset);

SYMS_API U32
syms_string_get_size(SymsString str);

SYMS_API syms_bool
syms_string_cmp(SymsString a, SymsString b);

SYMS_API const U8 *
syms_decode_utf8(const U8 *p, U32 *dst);

SYMS_API char *
syms_string_to_cstr(SymsString str, SymsArena *arena);


// -------------------------------------------------------------------------------- 

typedef struct SymsBuffer 
{
    const void *base;
    SymsUMM size;
    SymsUMM off;
    u32 addr_size;
    u32 offs_size;
} SymsBuffer;

SYMS_API SymsBuffer
syms_buffer_init(const void *data, SymsUMM size);

SYMS_API SymsBuffer
syms_buffer_init_ex(const void *data, SymsUMM size, SymsAddrSize addr_size, SymsAddrSize offset_type);

SYMS_API syms_bool
syms_buffer_skip(SymsBuffer *buffer, SymsOffset num);

SYMS_API syms_bool
syms_buffer_seek(SymsBuffer *buffer, SymsOffset new_off);

SYMS_API U8
syms_buffer_peek_u8(SymsBuffer *bin);

SYMS_API U16
syms_buffer_peek_u16(SymsBuffer *bin);

SYMS_API U32
syms_buffer_peek_u32(SymsBuffer *bin);

SYMS_API U64
syms_buffer_peek_u64(SymsBuffer *bin);

SYMS_API S8
syms_buffer_peek_s8(SymsBuffer *bin);

SYMS_API S16
syms_buffer_peek_s16(SymsBuffer *bin);

SYMS_API S32
syms_buffer_peek_s32(SymsBuffer *bin);

SYMS_API S64
syms_buffer_peek_s64(SymsBuffer *bin);

SYMS_API S64
syms_buffer_peek_sleb128(SymsBuffer *bin);

SYMS_API U64
syms_buffer_peek_uleb128(SymsBuffer *bin);

SYMS_API SymsString
syms_buffer_peek_string(SymsBuffer *bin);

SYMS_API SymsString
syms_buffer_peek_cstr(SymsBuffer *bin);

#define syms_buffer_push_array(buffer, type, count) (type *)syms_buffer_push(buffer, sizeof(type) * (count))
#define syms_buffer_push_struct(buffer, type)       syms_buffer_push_array(buffer, type, 1)
SYMS_API void *
syms_buffer_push(SymsBuffer *buffer, SymsUMM num_bytes);

SYMS_API syms_bool
syms_buffer_read(SymsBuffer *buffer, void *out_bf, SymsUMM size);

SYMS_API U8
syms_buffer_read_u8(SymsBuffer *buffer);

SYMS_API U16
syms_buffer_read_u16(SymsBuffer *buffer);

SYMS_API U32
syms_buffer_read_u32(SymsBuffer *buffer);

SYMS_API U32
syms_buffer_read_u32(SymsBuffer *buffer);

SYMS_API U64
syms_buffer_read_u64(SymsBuffer *buffer);

SYMS_API U64
syms_buffer_read_uleb128(SymsBuffer *bin);
// Reads an unsigned integer with variable-length encoding LEB128.

SYMS_API S8
syms_buffer_read_s8(SymsBuffer *buffer);

SYMS_API S16
syms_buffer_read_s16(SymsBuffer *buffer);

SYMS_API S32
syms_buffer_read_s32(SymsBuffer *buffer);

SYMS_API S64
syms_buffer_read_s64(SymsBuffer *buffer);

SYMS_API S64
syms_buffer_read_sleb128(SymsBuffer *bin);

SYMS_API SymsString
syms_buffer_read_string(SymsBuffer *buffer);

SYMS_API SymsString
syms_buffer_read_cstr(SymsBuffer *buffer);

SYMS_API void
syms_buffer_write_cstr(SymsBuffer *buffer, const char *cstr);

SYMS_API void
syms_buffer_write(SymsBuffer *buffer, const void *src, U32 src_size);

SYMS_API void
syms_buffer_write_u8(SymsBuffer *buffer, U8 value);

SYMS_API const char *
syms_buffer_get_cstr(SymsBuffer *buffer);

SYMS_API const void *
syms_buffer_at(SymsBuffer *buffer);

// -------------------------------------------------------------------------------- 

SYMS_API SymsString
syms_path_get_file_name(SymsString path, syms_bool strip_ext);

// -------------------------------------------------------------------------------- 

enum 
{
    SYMS_ARENA_FLAG_ALLOC_FAILED = (1 << 0)
};
typedef u32 SymsArenaFlags;

struct SymsArena 
{
    SymsArenaFlags flags;
    SymsUMM size;
    SymsUMM page_size;
    void *head;
    u8 *cursor;
};

struct SymsArenaFrame
{
    SymsArena *arena;
    SymsArenaFlags flags;
    SymsUMM size;
    void *head;
    u8 *cursor;
};

SYMS_API void
syms_arena_init(SymsArena *arena, SymsUMM page_size);

SYMS_API void
syms_arena_free(SymsArena *arena);

#define syms_arena_push_array_ex(arena, type, num, align) (type *)syms_arena_push_ex(arena, sizeof(type)*num, align)
#define syms_arena_push_struct_ex(arena, type, align) syms_arena_push_array_ex(arena, type, 1, align)

#define syms_arena_push(arena, len)             syms_arena_push_ex(arena, len, sizeof(void*))
#define syms_arena_push_array(arena, type, num) syms_arena_push_array_ex(arena, type, num, sizeof(void*))
#define syms_arena_push_struct(arena, type)     syms_arena_push_array(arena, type, 1)

SYMS_API void *
syms_arena_push_ex(SymsArena *arena, SymsUMM len, SymsUMM align);

SYMS_API char *
syms_arena_push_cstr(SymsArena *arena, const char *cstr);

SYMS_API SymsArenaFrame
syms_arena_frame_begin(SymsArena *arena);

SYMS_API void
syms_arena_frame_end(SymsArenaFrame frame);

// -------------------------------------------------------------------------------- 

SYMS_API U16
syms_trunc_u16(U32 value);

SYMS_API U32
syms_trunc_u32(U64 value);

U32
syms_hash_djb2(const char *str, u32 size);

U16
syms_hash_djb2_16(const char *str, u32 size);

#endif /* SYMS_CORE_INCLUDE_H */

#if defined(SYMS_CORE_IMPLEMENT)

SYMS_API U32
syms_addrsize_to_int(SymsAddrSize addr)
{
    U32 result;
    switch (addr) {
        default:
        case SYMS_ADDR_SIZE_NULL: result = 0; break;
        case SYMS_ADDR_SIZE_16:   result = 2; break;
        case SYMS_ADDR_SIZE_32:   result = 4; break;
        case SYMS_ADDR_SIZE_64:   result = 8; break;
        case SYMS_ADDR_SIZE_128:  result = 16; break;
    }
    return result;
}

SYMS_API U16
syms_trunc_u16(U32 value)
{
    U16 result;
    SYMS_ASSERT_ALWAYS(value <= SYMS_UINT16_MAX);
    result = (U16)value;
    return result;
}

SYMS_API U32
syms_trunc_u32(U64 value)
{
    U32 result;
    SYMS_ASSERT_ALWAYS(value <= SYMS_UINT32_MAX);
    result = (U32)value;
    return result;
}

SYMS_API void
syms_arena_init(SymsArena *a, SymsUMM page_size)
{
  a->flags = 0;
  a->size = 0;
  a->cursor = 0;
  a->page_size = page_size;
  a->head = 0;
}

SYMS_API void
syms_arena_free(SymsArena *a)
{
  void *p = a->head;
  while (p) {
    void *n = *(void **)p;
    *(void **)p = 0;
    syms_free_virtual_memory(p, 0);
    p = n;
  }
}

SYMS_API void *
syms_arena_push_ex(SymsArena *a, SymsUMM size, SymsUMM align)
{
  SymsUMM cursor = (SymsUMM)a->cursor;
  SymsUMM mask = align - 1;
  SymsUMM pad = 0;
  if (cursor & mask) {
    pad = align - (cursor & mask);
  }
  SymsUMM total_size = size + pad;
  SymsUMM cursor_offset = cursor - (SymsUMM)a->head;
  if (cursor_offset + total_size > a->size) {
    SymsUMM pc = (total_size + 16 + (a->page_size - 1)) / a->page_size;
    SymsUMM as = pc * a->page_size;
    u8 *p = (u8 *)syms_reserve_virtual_memory(as);
    SYMS_ASSERT(p);
    syms_commit_virtual_memory(p, as);
    ((void**)p)[0] = a->head;
    a->head = p;
    a->cursor = p + 16;
    a->size = pc * a->page_size;
  }
  void *ptr = a->cursor + pad;
  a->cursor += total_size;
  return ptr;
}
SYMS_API char *
syms_arena_push_cstr(SymsArena *arena, const char *cstr)
{
    u32 cstr_size = (u32)syms_strlen(cstr);
    char *result = syms_arena_push_array(arena, char, cstr_size);
    syms_memcpy(result, cstr, cstr_size);
    return result;
}

SYMS_API SymsArenaFrame
syms_arena_frame_begin(SymsArena *a)
{
    SymsArenaFrame f;
    f.arena = a;
    f.flags = a->flags;
    f.cursor = a->cursor;
    f.head = a->head;
    f.size = a->size;
    return f;
}

SYMS_API void
syms_arena_frame_end(SymsArenaFrame f)
{
  SymsArena *a = f.arena;
  void *p = a->head;
  while (p != f.head) {
    void *n = *(void **)p;
    *(void **)p = 0;
    syms_free_virtual_memory(p, 0);
    p = n;
  }
  a->head = f.head;
  a->cursor = f.cursor;
  a->flags = f.flags;
  a->size = f.size;
}

SYMS_API SymsString
syms_string_init(const char *data, U32 len)
{
    SymsString result;
    while (len > 0 && data[len - 1] == '\0') {
        --len;
    }
    result.data = data;
    result.len = len;
    return result;
}

SYMS_API SymsString
syms_string_init_lit(const char *data)
{
    SymsString result;
    result.len = 0;
    result.data = data;
    while (*data++) {
        ++result.len;
    }
    return result;
}
SYMS_API syms_bool
syms_string_cmp_lit(SymsString a, const char *b)
{
    for (;;) {
        if (a.len == 0 || *b == '\0')
            break;
        
        if (*a.data++ != *b++)
            break;
        
        --a.len;
    }
    
    return a.len == 0 && *b == '\0';
}
SYMS_API U32
syms_string_get_size(SymsString str)
{
    SYMS_ASSERT(str.data);
    return str.len;
}
SYMS_API syms_bool
syms_string_is_null(SymsString str)
{
    syms_bool result = str.data == 0 || str.len == 0;
    return result;
}
SYMS_API syms_bool
syms_string_cmp(SymsString a, SymsString b)
{
    syms_bool result = syms_false;
    if (a.len == b.len) {
        U32 i;
        for (i = 0; i < a.len; ++i) {
            if (a.data[i] != b.data[i])
                break;
        }
        result = i == a.len;
    }
    return result;
}
SYMS_API char *
syms_string_to_cstr(SymsString str, SymsArena *arena)
{
    char *p = syms_arena_push_array(arena, char, str.len + 1);
    if (p) {
        syms_memcpy(p, str.data, str.len);
        p[str.len] = '\0';
    }
    return p;
}

SYMS_API syms_bool
syms_is_alpha_ascii(U32 codepoint)
{
    return (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z');
}
SYMS_API U32
syms_lowercase(U32 codepoint)
{
    if (codepoint >= 'A' && codepoint <= 'Z')
        codepoint += 'a' - 'A';
    return codepoint;
}
SYMS_API U32
syms_uppercase(U32 codepoint)
{
    if (codepoint >= 'a' && codepoint <= 'z')
        codepoint -= 'a' - 'A';
    return codepoint;
}
SYMS_API syms_bool
syms_string_cmp_nocase(SymsString a_str, SymsString b_str)
{
    syms_bool result = syms_false;
    if (a_str.len == b_str.len) {
        U32 i;
        for (i = 0; i < a_str.len; ++i) {
            U32 a = syms_lowercase((U32)a_str.data[i]);
            U32 b = syms_lowercase((U32)b_str.data[i]);
            if (a != b) break;
        }
        result = (i == a_str.len);
    }
    return result;
}
SYMS_API char
syms_string_peek_byte(SymsString str, U32 byte_offset)
{
    char result = 0;
    if (byte_offset < str.len) {
        result = ((const char *)str.data)[byte_offset];
    }
    return result;
}

SYMS_API const U8 *
syms_decode_utf8(const U8 *p, U32 *dst)
{
    U32 res, n;
    switch (*p & 0xf0) {
        case 0xf0 :  res = *p & 0x07;  n = 3;  break;
        case 0xe0 :  res = *p & 0x0f;  n = 2;  break;
        case 0xd0 :
        case 0xc0 :  res = *p & 0x1f;  n = 1;  break;
        default   :  res = *p;         n = 0;  break;
    }
    while (n--) {
        res = (res << 6) | (*(++p) & 0x3f);
    }
    *dst = res;
    return p + 1;
}

SYMS_API SymsString
syms_path_get_file_name(SymsString path, syms_bool strip_ext)
{
    const U8 *p = (const U8 *)path.data;
    const U8 *p_end = p + path.len;
    const U8 *start = p;
    const U8 *end = p_end;
    u32 fname_size;
    SymsString fname;
    
    while (p != p_end) {
        U32 c;
        p = syms_decode_utf8(p, &c);
        if (c == '\0') {
            break;
        }
        if (c == '\\' || c == '/') {
            start = p;
        }
    }
    if (strip_ext) {
        p = start;
        p_end = end;
        while (p != p_end) {
            U32 c;
            p = syms_decode_utf8(p, &c);
            if (c == '.') {
                end = p;
            }
        }
    }
    
    fname_size = (U32)(end - start);
    fname = syms_string_init((const char *)start, fname_size);
    return fname;
}

SYMS_API syms_bool
syms_buffer_read(SymsBuffer *bin, void *out_bf, SymsUMM size)
{
    syms_bool done = syms_false;
    if (bin->off + size <= bin->size) {
        const void *src = (void *)((const char *)bin->base + bin->off);
        syms_memcpy(out_bf, src, size);
        bin->off += size;
        done = syms_true;
    }
    return done;
}

SYMS_API void *
syms_buffer_push(SymsBuffer *bin, SymsUMM size)
{
    void *result = 0;
    if (bin->off + size <= bin->size) {
        if (bin->base) {
            result = (void *)((const U8 *)bin->base + bin->off);
        }
        bin->off += size;
    }
    return result;
}

SYMS_API SymsBuffer
syms_buffer_init_ex(const void *base, SymsUMM size, SymsAddrSize addr_type, SymsAddrSize offs_type)
{
    SymsBuffer result;
    
    result.base      = base;
    result.size      = size;
    result.off       = 0;
    result.addr_size = syms_addrsize_to_int(addr_type);
    result.offs_size = syms_addrsize_to_int(offs_type);
    
#if defined(SYMS_PARANOID)
    SYMS_ASSERT(result.addr_size > 0);
    SYMS_ASSERT(result.offs_size > 0);
#endif
    
    return result;
}

SYMS_API  SymsBuffer
syms_buffer_init(const void *data, SymsUMM size)
{
    return syms_buffer_init_ex(data, size, SYMS_ADDR_SIZE_HOST, SYMS_ADDR_SIZE_HOST);
}

SYMS_API syms_bool
syms_buffer_skip(SymsBuffer *bin, SymsOffset num)
{
    syms_bool result = syms_false;
    if (bin->off + num <= bin->size) {
        bin->off += num;
        result = syms_true;
    }
    return result;
}

SYMS_API syms_bool
syms_buffer_seek(SymsBuffer *bin, SymsOffset new_off)
{
    syms_bool result = syms_false;
    if (new_off <= bin->size) {
        bin->off = new_off;
        result = syms_true;
    }
    return result;
}

#define syms_buffer_peek(bin, a) syms_buffer_peek_ex(bin, a, a)
#define syms_buffer_peek_ex(bin, a, b) \
do { \
SymsOffset off = bin->off; \
a result = syms_buffer_read_##b(bin); \
bin->off = off; \
return result; \
} while (0)


SYMS_API U8
syms_buffer_peek_u8(SymsBuffer *bin)
{
    syms_buffer_peek(bin, u8);
}

SYMS_API U16
syms_buffer_peek_u16(SymsBuffer *bin)
{
    syms_buffer_peek(bin, u16);
}

SYMS_API U32
syms_buffer_peek_u32(SymsBuffer *bin)
{
    syms_buffer_peek(bin, u32);
}

SYMS_API U64
syms_buffer_peek_u64(SymsBuffer *bin)
{
    syms_buffer_peek(bin, u64);
}

SYMS_API S8
syms_buffer_peek_s8(SymsBuffer *bin)
{
    syms_buffer_peek(bin, s8);
}

SYMS_API S16
syms_buffer_peek_s16(SymsBuffer *bin)
{
    syms_buffer_peek(bin, s16);
}

SYMS_API S32
syms_buffer_peek_s32(SymsBuffer *bin)
{
    syms_buffer_peek(bin, s32);
}

SYMS_API S64
syms_buffer_peek_s64(SymsBuffer *bin)
{
    syms_buffer_peek(bin, s64);
}

SYMS_API S64
syms_buffer_peek_sleb128(SymsBuffer *bin)
{
    syms_buffer_peek_ex(bin, s64, sleb128);
}

SYMS_API U64
syms_buffer_peek_uleb128(SymsBuffer *bin)
{
    syms_buffer_peek_ex(bin, u64, uleb128);
}

SYMS_API SymsString
syms_buffer_peek_string(SymsBuffer *bin)
{
    syms_buffer_peek_ex(bin, SymsString, string);
}

SYMS_API SymsString
syms_buffer_peek_cstr(SymsBuffer *bin)
{
    syms_buffer_peek_ex(bin, SymsString, cstr);
}

SYMS_API void
syms_buffer_write(SymsBuffer *bin, const void *src, U32 src_size)
{
    void *dst = syms_buffer_push(bin, src_size);
    if (dst) {
        syms_memcpy(dst, src, src_size);
    }
}
SYMS_API void
syms_buffer_write_cstr(SymsBuffer *bin, const char *cstr)
{
    U32 cstr_size = (U32)syms_strlen(cstr);
    syms_buffer_write(bin, cstr, cstr_size);
}
SYMS_API const char *
syms_buffer_get_cstr(SymsBuffer *buffer)
{
    char *base = (char *)buffer->base;
    if (buffer->off >= buffer->size) {
        SYMS_ASSERT(buffer->size > 0);
        syms_memset((char*)base + buffer->size - 1, '\0', 1);
    } else {
        syms_memset((char*)base + buffer->off, '\0', 1);
    }
    return base;
}
SYMS_API const void *
syms_buffer_at(SymsBuffer *buffer)
{
    const void *result = (const void *)((const char *)buffer->base + buffer->off);
    return result;
}

SYMS_API void
syms_buffer_write_u8(SymsBuffer *buffer, U8 value)
{
    syms_buffer_write(buffer, &value, sizeof(value));
}

SYMS_API void
syms_buffer_write_u32(SymsBuffer *buffer, U32 value)
{
    syms_buffer_write(buffer, &value, sizeof(value));
}

SYMS_API SymsString
syms_buffer_read_cstr(SymsBuffer *buffer)
{
    const char *p = (const char *)buffer->base + buffer->off;
    SymsUWord off = buffer->off;
    u32 size;
    SymsString result;
    while (off < buffer->size) {
        if (p[off] == '\0') {
            break;
        }
        off += 1;
    }
    size = syms_trunc_u32(off - buffer->off);
    buffer->off = off;
    result = syms_string_init(p, size);
    return result;
}

SYMS_API SymsString
syms_buffer_read_string(SymsBuffer *buffer)
{
    SymsUWord off_start = buffer->off;
    SymsUWord off_end = buffer->off;
    const char *ptr = (const char *)buffer->base + buffer->off;
    SymsString result;
    while (buffer->off < buffer->size) {
        if (((const char *)buffer->base)[buffer->off++] == '\0')
            break;
    }
    off_end = buffer->off;
    result = syms_string_init(ptr, (U32)(off_end - off_start));
    return result;
}

SYMS_API U8
syms_buffer_read_u8(SymsBuffer *buffer)
{
    U8 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API U16
syms_buffer_read_u16(SymsBuffer *buffer)
{
    U16 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API U32
syms_buffer_read_u24(SymsBuffer *buffer)
{
    U32 result;
    U32 a = (U32)syms_buffer_read_u8(buffer);
    U32 b = (U32)syms_buffer_read_u8(buffer);
    U32 c = (U32)syms_buffer_read_u8(buffer);
    result = (a << 0) | (b << 8) | (c << 16);
    return result;
}

SYMS_API U32
syms_buffer_read_u32(SymsBuffer *buffer)
{
    U32 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API U64
syms_buffer_read_u64(SymsBuffer *buffer)
{
    U64 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API S8
syms_buffer_read_s8(SymsBuffer *buffer)
{
    S8 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API S16
syms_buffer_read_s16(SymsBuffer *buffer)
{
    S16 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API S32
syms_buffer_read_s32(SymsBuffer *buffer)
{
    S32 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API S64
syms_buffer_read_s64(SymsBuffer *buffer)
{
    S64 result = 0;
    syms_buffer_read(buffer, &result, sizeof(result));
    return result;
}

SYMS_API U64
syms_buffer_read_uleb128(SymsBuffer *buffer)
{
    U64 res = 0;
    U64 shift = 0;
    
    const U8 *data = (const U8 *)buffer->base + buffer->off;
    const U8 *start = data;
    
    U32 num_used;
    
    while (buffer->off < buffer->size) {
        U8 i = *data++;
        U8 val = i & 0x7fu;
        res |= (U64)val << shift;
        if ((i & 0x80u) == 0) {
            break;
        }
        shift += 7u;
    }
    
    num_used = (U32)((const U8 *)data - (const U8 *)start);
    buffer->off += num_used;
    
    return res;
}

SYMS_API S64
syms_buffer_read_sleb128(SymsBuffer *buffer)
{
    S64 result = 0;
    U32 shift = 0;
    
    const U8 *data = (const U8 *)buffer->base + buffer->off;
    const U8 *end = (const U8 *)buffer->base + buffer->size;
    const U8 *start = data;
    
    while (data < end) {
        U8 byte = *data;
        data += 1;
        
        result |= ((U64)(byte & 0x7f)) << shift;
        shift += 7;
        
        if ((byte & 0x80) == 0) {
            U32 num_used;
            
            if (shift < (sizeof(result) * 8) && (byte & 0x40)) {
                result |= -((S64)(1u << shift));
            }
            
            num_used = (U32)((const U8 *)data - (const U8 *)start);
            buffer->off += num_used;
            
            return (S64)(result);
        }
    }
    
    return 0;
}

SYMS_API SymsAddr
syms_buffer_read_addr(SymsBuffer *buffer)
{
    SymsAddr result = 0;
    SYMS_ASSERT(buffer->addr_size > 0);
    SYMS_ASSERT(buffer->addr_size <= sizeof(SymsAddr));
    syms_buffer_read(buffer, &result, buffer->addr_size);
    return result;
}

SYMS_API SymsAddr
syms_buffer_read_offs(SymsBuffer *buffer)
{
    SymsAddr result = 0;
    SYMS_ASSERT(buffer->offs_size > 0);
    SYMS_ASSERT(buffer->offs_size <= sizeof(SymsAddr));
    syms_buffer_read(buffer, &result, buffer->offs_size);
    return result;
}

U32
syms_hash_djb2(const char *str, u32 size)
{
    U32 hash = 5381;
    const char *str_end = str + size;
    while (str < str_end) {
        U32 c = (U32)*str++;
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

U16
syms_hash_djb2_16(const char *str, U32 size)
{
    U32 hash = syms_hash_djb2(str, size);
    U16 result = (U16)(((hash & 0xFFFF0000) >> 16) + (hash & 0x0000FFFF));
    return result;
}

#endif /* SYMS_CORE_IMPLEMENT */
