// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef TRIMD_ENABLE_AVX
#include "trimd/Polyfill.h"

#include <immintrin.h>

namespace trimd {

namespace avx {

class F256 {
    public:
        F256() : data{_mm256_setzero_ps()} {
        }

        explicit F256(__m256 value) : data{value} {
        }

        explicit F256(float value) : F256{_mm256_set1_ps(value)} {
        }

        F256(float v1, float v2, float v3, float v4, float v5, float v6, float v7, float v8) :
            data{_mm256_set_ps(v8, v7, v6, v5, v4, v3, v2, v1)} {
        }

        static F256 fromAlignedSource(const float* source) {
            return F256{_mm256_load_ps(source)};
        }

        static F256 fromUnalignedSource(const float* source) {
            return F256{_mm256_loadu_ps(source)};
        }

        static F256 loadSingleValue(const float* source) {
            const __m256i mask = _mm256_set_epi32(-1, 0, 0, 0, 0, 0, 0, 0);
            return F256{_mm256_maskload_ps(source, mask)};
        }

        #ifdef TRIMD_ENABLE_F16C
            static F256 fromAlignedSource(const std::uint16_t* source) {
                __m128i halfFloats = _mm_load_si128(reinterpret_cast<const __m128i*>(source));
                return F256{_mm256_cvtph_ps(halfFloats)};
            }

            static F256 fromUnalignedSource(const std::uint16_t* source) {
                __m128i halfFloats = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
                return F256{_mm256_cvtph_ps(halfFloats)};
            }

            static F256 loadSingleValue(const std::uint16_t* source) {
                __m128i halfFloats = _mm_loadu_si16(source);
                return F256{_mm256_cvtph_ps(halfFloats)};
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
            data = _mm256_load_ps(source);
        }

        void unalignedLoad(const float* source) {
            data = _mm256_loadu_ps(source);
        }

        void alignedStore(float* dest) const {
            _mm256_store_ps(dest, data);
        }

        void unalignedStore(float* dest) const {
            _mm256_storeu_ps(dest, data);
        }

        float sum() const {
            // (data[3] + data[7], data[2] + data[6], data[1] + data[5], data[0] + data[4])
            const __m128 x128 = _mm_add_ps(_mm256_extractf128_ps(data, 1), _mm256_castps256_ps128(data));
            // (-, -, data[1] + data[3] + data[5] + data[7], data[0] + data[2] + data[4] + data[6])
            const __m128 x64 = _mm_add_ps(x128, _mm_movehl_ps(x128, x128));
            // (-, -, -, data[0] + data[1] + data[2] + data[3] + data[4] + data[5] + data[6] + data[7])
            const __m128 x32 = _mm_add_ss(x64, _mm_shuffle_ps(x64, x64, 0x55));
            return _mm_cvtss_f32(x32);
        }

        F256& operator+=(const F256& rhs) {
            data = _mm256_add_ps(data, rhs.data);
            return *this;
        }

        F256& operator-=(const F256& rhs) {
            data = _mm256_sub_ps(data, rhs.data);
            return *this;
        }

        F256& operator*=(const F256& rhs) {
            data = _mm256_mul_ps(data, rhs.data);
            return *this;
        }

        F256& operator/=(const F256& rhs) {
            data = _mm256_div_ps(data, rhs.data);
            return *this;
        }

        static constexpr std::size_t size() {
            return sizeof(decltype(data)) / sizeof(float);
        }

        static constexpr std::size_t alignment() {
            return alignof(decltype(data));
        }

    private:
        __m256 data;
};

inline F256 operator+(const F256& lhs, const F256& rhs) {
    return F256(lhs) += rhs;
}

inline F256 operator-(const F256& lhs, const F256& rhs) {
    return F256(lhs) -= rhs;
}

inline F256 operator*(const F256& lhs, const F256& rhs) {
    return F256(lhs) *= rhs;
}

inline F256 operator/(const F256& lhs, const F256& rhs) {
    return F256(lhs) /= rhs;
}

}  // namespace avx

}  // namespace trimd

#endif  // TRIMD_ENABLE_AVX
