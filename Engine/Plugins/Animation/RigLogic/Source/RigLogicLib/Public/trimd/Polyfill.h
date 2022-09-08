// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#if defined(TRIMD_ENABLE_AVX) || defined(TRIMD_ENABLE_SSE)
#include <immintrin.h>

#include <cstdint>

#if (!defined(__clang__) && defined(_MSC_VER) && _MSC_VER <= 1900) || (!defined(__clang__) && defined(__GNUC__) && \
__GNUC__ < 9)
    #define _mm_loadu_si64 _mm_loadl_epi64
#endif

#if (!defined(__clang__) && defined(_MSC_VER) && _MSC_VER <= 1900) || \
(defined(__clang__) && __clang_major__ < 8) || \
(!defined(__clang__) && defined(__GNUC__))
    inline __m128i _mm_loadu_si16(const void* source) {
        return _mm_insert_epi16(_mm_setzero_si128(), *reinterpret_cast<const std::int16_t*>(source), 0);
    }

#endif

#endif  // defined(TRIMD_ENABLE_AVX) || defined(TRIMD_ENABLE_SSE)
// *INDENT-ON*
