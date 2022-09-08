// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#ifdef TRIMD_ENABLE_SSE
#include "trimd/Polyfill.h"

#include <immintrin.h>

namespace trimd {

namespace sse {

struct F128 {
    __m128 data;

    F128() : data{_mm_setzero_ps()} {
    }

    explicit F128(__m128 value) : data{value} {
    }

    explicit F128(float value) : F128{_mm_set1_ps(value)} {
    }

    F128(float v1, float v2, float v3, float v4) : data{_mm_set_ps(v4, v3, v2, v1)} {
    }

    static F128 fromAlignedSource(const float* source) {
        return F128{_mm_load_ps(source)};
    }

    static F128 fromUnalignedSource(const float* source) {
        return F128{_mm_loadu_ps(source)};
    }

    static F128 loadSingleValue(const float* source) {
        return F128{_mm_load_ss(source)};
    }

    #ifdef TRIMD_ENABLE_F16C
        static F128 fromAlignedSource(const std::uint16_t* source) {
            __m128i halfFloats = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(source));
            return F128{_mm_cvtph_ps(halfFloats)};
        }

        static F128 fromUnalignedSource(const std::uint16_t* source) {
            __m128i halfFloats = _mm_loadu_si64(reinterpret_cast<const __m128i*>(source));
            return F128{_mm_cvtph_ps(halfFloats)};
        }

        static F128 loadSingleValue(const std::uint16_t* source) {
            __m128i halfFloats = _mm_loadu_si16(source);
            return F128{_mm_cvtph_ps(halfFloats)};
        }

    #endif  // TRIMD_ENABLE_F16C

    template<typename T>
    static void prefetchT0(const T* source) {
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"
        #endif
        _mm_prefetch(reinterpret_cast<const char*>(source), _MM_HINT_T0);
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
    }

    template<typename T>
    static void prefetchT1(const T* source) {
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"
        #endif
        _mm_prefetch(reinterpret_cast<const char*>(source), _MM_HINT_T1);
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
    }

    template<typename T>
    static void prefetchT2(const T* source) {
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"
        #endif
        _mm_prefetch(reinterpret_cast<const char*>(source), _MM_HINT_T2);
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
    }

    template<typename T>
    static void prefetchNTA(const T* source) {
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"
        #endif
        _mm_prefetch(reinterpret_cast<const char*>(source), _MM_HINT_NTA);
        #if defined(__clang__) || defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
    }

    void alignedLoad(const float* source) {
        data = _mm_load_ps(source);
    }

    void unalignedLoad(const float* source) {
        data = _mm_loadu_ps(source);
    }

    void alignedStore(float* dest) const {
        _mm_store_ps(dest, data);
    }

    void unalignedStore(float* dest) const {
        _mm_storeu_ps(dest, data);
    }

    float sum() const {
        __m128 temp = _mm_movehdup_ps(data);
        __m128 result = _mm_add_ps(data, temp);
        temp = _mm_movehl_ps(temp, result);
        result = _mm_add_ss(result, temp);
        return _mm_cvtss_f32(result);
    }

    F128& operator+=(const F128& rhs) {
        data = _mm_add_ps(data, rhs.data);
        return *this;
    }

    F128& operator-=(const F128& rhs) {
        data = _mm_sub_ps(data, rhs.data);
        return *this;
    }

    F128& operator*=(const F128& rhs) {
        data = _mm_mul_ps(data, rhs.data);
        return *this;
    }

    F128& operator/=(const F128& rhs) {
        data = _mm_div_ps(data, rhs.data);
        return *this;
    }

    static constexpr std::size_t size() {
        return sizeof(decltype(data)) / sizeof(float);
    }

    static constexpr std::size_t alignment() {
        return alignof(decltype(data));
    }

};

inline bool operator==(const F128& lhs, const F128& rhs) {
    return _mm_movemask_ps(_mm_cmpeq_ps(lhs.data, rhs.data)) == 0xF;
}

inline bool operator!=(const F128& lhs, const F128& rhs) {
    return !(lhs == rhs);
}

inline F128 operator+(const F128& lhs, const F128& rhs) {
    return F128(lhs) += rhs;
}

inline F128 operator-(const F128& lhs, const F128& rhs) {
    return F128(lhs) -= rhs;
}

inline F128 operator*(const F128& lhs, const F128& rhs) {
    return F128(lhs) *= rhs;
}

inline F128 operator/(const F128& lhs, const F128& rhs) {
    return F128(lhs) /= rhs;
}

inline void transpose(F128& row0, F128& row1, F128& row2, F128& row3) {
    _MM_TRANSPOSE4_PS(row0.data, row1.data, row2.data, row3.data);
}

} // namespace sse

} // namespace trimd

#endif  // TRIMD_ENABLE_SSE
// *INDENT-ON*
